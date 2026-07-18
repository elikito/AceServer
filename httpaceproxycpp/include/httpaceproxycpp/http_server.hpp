#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace httpace {

// ---------------------------------------------------------------------------
// ThreadPool — pool de hilos fijo con cola acotada.
//
// Diseñado para hardware de bajos recursos (MiniPC / 4 GB RAM):
//   - Número de workers: min(hardware_concurrency × 2, MAX_WORKERS)
//   - Cola máxima: MAX_QUEUE_DEPTH tareas pendientes
//   - Si la cola está llena se rechaza la conexión (503) en O(1) sin bloquear
//     el accept-loop ni crear hilos adicionales.
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    // max_workers = 0  → auto (hardware_concurrency × 2, capped a MAX_WORKERS)
    // max_queue   = 0  → usa DEFAULT_QUEUE_DEPTH
    explicit ThreadPool(std::size_t max_workers = 0, std::size_t max_queue = 0);
    ~ThreadPool();

    // No copiable ni movible
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Encola una tarea. Devuelve true si fue aceptada, false si la cola está
    // llena (el llamador debe rechazar la conexión).
    bool try_submit(std::function<void()> task);

    // Detiene el pool: vacía la cola y espera a que todos los workers terminen.
    void shutdown();

    // Estadísticas en tiempo real (lock-free)
    std::size_t worker_count()   const noexcept { return workers_.size(); }
    std::size_t queue_depth()    const noexcept { return queue_size_.load(std::memory_order_relaxed); }
    std::size_t active_workers() const noexcept { return active_.load(std::memory_order_relaxed); }

private:
    static constexpr std::size_t MAX_WORKERS     = 16;
    static constexpr std::size_t DEFAULT_QUEUE_DEPTH = 256;

    void worker_loop();

    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    mutable std::mutex                 mutex_;
    std::condition_variable            cv_;
    std::atomic<bool>                  stop_{false};
    std::atomic<std::size_t>           queue_size_{0};
    std::atomic<std::size_t>           active_{0};
    std::size_t                        max_queue_;
};

// ---------------------------------------------------------------------------
// HttpRequest / HttpResponse
// ---------------------------------------------------------------------------
struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string client_ip;

    std::string header(const std::string& name, const std::string& fallback = "") const;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    bool close = true;
    bool raw_sent = false;
};

// ---------------------------------------------------------------------------
// ClientConnection
// ---------------------------------------------------------------------------
class ClientConnection {
public:
    explicit ClientConnection(int fd);
    ~ClientConnection();

    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

    int fd() const { return fd_; }
    bool send_all(const void* data, std::size_t size);
    bool send_text(const std::string& value);
    bool send_response_headers(int status, const std::string& reason,
                               const std::map<std::string, std::string>& headers);
    void close();

private:
    int fd_ = -1;
};

using HttpHandler = std::function<void(const HttpRequest&, ClientConnection&)>;

// ---------------------------------------------------------------------------
// HttpServer — servidor HTTP con ThreadPool integrado
// ---------------------------------------------------------------------------
class HttpServer {
public:
    HttpServer(std::string host, int port, HttpHandler handler);
    ~HttpServer();

    void start();
    void stop();
    void join();
    void set_client_send_timeout(int seconds) { client_send_timeout_ = seconds; }

    // Ajuste del pool antes de llamar a start():
    //   max_workers = 0 → auto
    //   max_queue   = 0 → DEFAULT_QUEUE_DEPTH (256)
    void set_thread_pool_size(std::size_t max_workers, std::size_t max_queue = 0) {
        pool_max_workers_ = max_workers;
        pool_max_queue_   = max_queue;
    }

    // Estadísticas del pool (accesibles para el endpoint /stat)
    std::size_t pool_worker_count()   const { return pool_ ? pool_->worker_count()   : 0; }
    std::size_t pool_queue_depth()    const { return pool_ ? pool_->queue_depth()    : 0; }
    std::size_t pool_active_workers() const { return pool_ ? pool_->active_workers() : 0; }

private:
    void accept_loop();
    void handle_client(int client_fd, std::string client_ip);
    void send_overload_response(int fd);

    std::string  host_;
    int          port_;
    HttpHandler  handler_;
    int          listen_fd_         = -1;
    int          client_send_timeout_ = 0;
    std::size_t  pool_max_workers_  = 0;
    std::size_t  pool_max_queue_    = 0;
    std::atomic<bool>  running_{false};
    std::thread        accept_thread_;
    std::unique_ptr<ThreadPool> pool_;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string status_reason(int status);
bool parse_http_request(int fd, const std::string& client_ip, HttpRequest& request);
void send_simple_response(ClientConnection& connection, int status, const std::string& content_type,
                          const std::string& body,
                          std::map<std::string, std::string> extra_headers = {});

} // namespace httpace
