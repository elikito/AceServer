// ---------------------------------------------------------------------------
// channel_verifier.cpp  —  v08.24.02
//
// Implementación del Worker Pool asíncrono y del pipeline de verificación
// de Content IDs de AceStream en 4 fases.
//
// Arquitectura:
//   - counting_semaphore<kDefaultMaxWorkers> garantiza ≤ max_workers tareas
//     simultáneas accediendo al motor AceStream.
//   - N threads de worker leen de task_queue_ (FIFO), pero la semáforo limita
//     cuántos ejecutan run_pipeline() al mismo tiempo.
//   - Estado persistente en state_ (shared_mutex): lecturas concurrentes sin
//     bloqueo entre sí; escrituras exclusivas sólo al actualizar un estado.
//   - verify_sync() encola la tarea y espera por condition_variable hasta que
//     el worker notifica la finalización mediante el callback interno.
// ---------------------------------------------------------------------------

#include "httpaceproxycpp/channel_verifier.hpp"
#include "httpaceproxycpp/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

namespace httpace {

// ---------------------------------------------------------------------------
// Timeouts del pipeline (en segundos para get_single)
// ---------------------------------------------------------------------------
namespace {
    constexpr long kHandshakeTimeoutSec = 3;       // Fase 1: getstream handshake
    constexpr long kStatPollTimeoutSec  = 2;       // Fase 3: cada poll stat_url
    constexpr long kStopTimeoutSec      = 2;       // Cierre: command_url?method=stop
    constexpr int  kObserveTotalMs      = 2750;    // Fase 3: ventana de observación (ms)
    constexpr int  kObservePollMs       = 400;     // Fase 3: intervalo entre polls (ms)
}

// ---------------------------------------------------------------------------
// ChannelHealth helpers
// ---------------------------------------------------------------------------

const char* health_to_string(ChannelHealth h) noexcept {
    switch (h) {
        case ChannelHealth::ONLINE:    return "ONLINE";
        case ChannelHealth::LOW_PEERS: return "LOW_PEERS";
        case ChannelHealth::OFFLINE:   return "OFFLINE";
        case ChannelHealth::BLOCKED:   return "BLOCKED";
        case ChannelHealth::ERROR:     return "ERROR";
        case ChannelHealth::PENDING:   return "PENDING";
        default:                       return "UNKNOWN";
    }
}

ChannelHealth health_from_string(const std::string& s) noexcept {
    if (s == "ONLINE")    return ChannelHealth::ONLINE;
    if (s == "LOW_PEERS") return ChannelHealth::LOW_PEERS;
    if (s == "OFFLINE")   return ChannelHealth::OFFLINE;
    if (s == "BLOCKED")   return ChannelHealth::BLOCKED;
    if (s == "ERROR")     return ChannelHealth::ERROR;
    if (s == "PENDING")   return ChannelHealth::PENDING;
    return ChannelHealth::UNKNOWN;
}

// ---------------------------------------------------------------------------
// VerifyResult::to_json
// ---------------------------------------------------------------------------

Json VerifyResult::to_json() const {
    return Json::object{
        {"content_id",    content_id},
        {"health",        health_to_string(health)},
        {"peers",         static_cast<double>(peers)},
        {"speed_down",    static_cast<double>(speed_down)},
        {"status_text",   status_text},
        {"error",         error},
        {"checked_at",    static_cast<double>(checked_at)},
        {"phase_reached", static_cast<double>(phase_reached)},
    };
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ChannelVerifier::ChannelVerifier(const HttpClient& http_client,
                                 std::string ace_host,
                                 int ace_http_port,
                                 int max_workers)
    : http_client_(http_client),
      ace_host_(std::move(ace_host)),
      ace_http_port_(ace_http_port),
      max_workers_(std::max(1, std::min(max_workers, kDefaultMaxWorkers))),
      semaphore_(kDefaultMaxWorkers)   // semáforo con capacidad fija en tiempo de compilación
{
    // Leer override del puerto HTTP del motor desde variable de entorno.
    if (const char* env_port = std::getenv("ACE_ENGINE_HTTP_PORT")) {
        try {
            int p = std::stoi(env_port);
            if (p > 0 && p < 65536) ace_http_port_ = p;
        } catch (...) {}
    }

    // Arrancar threads de worker.
    // Usamos max_workers_ + 1 threads: los workers extra están siempre en espera
    // bloqueados en queue_cv_; la semáforo limita los que ejecutan el pipeline.
    int thread_count = max_workers_ + 1;
    workers_.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }

    log_line("INFO", "[verifier] ChannelVerifier iniciado: "
             + ace_host_ + ":" + std::to_string(ace_http_port_)
             + " max_workers=" + std::to_string(max_workers_));
}

ChannelVerifier::~ChannelVerifier() {
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        stop_ = true;
    }
    queue_cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

// ---------------------------------------------------------------------------
// Configuración en runtime
// ---------------------------------------------------------------------------

void ChannelVerifier::set_speed_threshold(long long bytes_per_second) {
    speed_threshold_.store(bytes_per_second);
}

void ChannelVerifier::set_ace_engine(const std::string& host, int http_port) {
    std::lock_guard<std::mutex> lk(engine_mutex_);
    ace_host_      = host;
    ace_http_port_ = http_port;
}

// ---------------------------------------------------------------------------
// Estado en memoria
// ---------------------------------------------------------------------------

void ChannelVerifier::clear_state(const std::string& content_id) {
    std::unique_lock<std::shared_mutex> lk(state_mutex_);
    state_.erase(content_id);
}

void ChannelVerifier::clear_all_state() {
    std::unique_lock<std::shared_mutex> lk(state_mutex_);
    state_.clear();
}

VerifyResult ChannelVerifier::get_cached(const std::string& content_id) const {
    std::shared_lock<std::shared_mutex> lk(state_mutex_);
    auto it = state_.find(content_id);
    if (it != state_.end()) return it->second;
    VerifyResult r;
    r.content_id = content_id;
    r.health     = ChannelHealth::UNKNOWN;
    return r;
}

Json ChannelVerifier::get_health_map() const {
    std::shared_lock<std::shared_mutex> lk(state_mutex_);
    Json::object map;
    for (const auto& [cid, result] : state_) {
        map[cid] = result.to_json();
    }
    return Json(map);
}

// ---------------------------------------------------------------------------
// Verificación síncrona
// ---------------------------------------------------------------------------

VerifyResult ChannelVerifier::verify_sync(const std::string& content_id,
                                           int timeout_ms,
                                           int max_cache_age_s) {
    if (content_id.empty()) {
        VerifyResult r;
        r.content_id  = content_id;
        r.health      = ChannelHealth::ERROR;
        r.error       = "content_id vacío";
        r.checked_at  = unix_time();
        return r;
    }

    // Comprobar caché válida.
    if (max_cache_age_s > 0) {
        auto cached = get_cached(content_id);
        if (cached.health != ChannelHealth::UNKNOWN &&
            cached.health != ChannelHealth::PENDING) {
            auto age = unix_time() - cached.checked_at;
            if (age < static_cast<std::int64_t>(max_cache_age_s)) {
                return cached;
            }
        }
    }

    // Marcar como PENDING en el estado.
    {
        std::unique_lock<std::shared_mutex> lk(state_mutex_);
        auto& r = state_[content_id];
        if (r.health == ChannelHealth::PENDING) {
            // Ya hay una verificación en curso: esperar a que termine.
            lk.unlock();
            auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeout_ms);
            std::unique_lock<std::mutex> slk(sync_mutex_);
            bool finished = sync_cv_.wait_until(slk, deadline, [&] {
                std::shared_lock<std::shared_mutex> rlk(state_mutex_);
                auto it = state_.find(content_id);
                return it != state_.end()
                       && it->second.health != ChannelHealth::PENDING;
            });
            if (!finished) {
                VerifyResult timeout_r;
                timeout_r.content_id = content_id;
                timeout_r.health     = ChannelHealth::ERROR;
                timeout_r.error      = "timeout esperando verificación en curso";
                timeout_r.checked_at = unix_time();
                return timeout_r;
            }
            return get_cached(content_id);
        }
        r.content_id = content_id;
        r.health     = ChannelHealth::PENDING;
    }

    // Resultado compartido entre lambda de callback y este hilo.
    bool completed = false;
    VerifyResult sync_result;
    std::mutex result_mutex;
    std::condition_variable result_cv;

    enqueue(content_id, [&](VerifyResult res) {
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            sync_result = res;
            completed   = true;
        }
        result_cv.notify_all();
    });

    // Esperar resultado con timeout.
    {
        std::unique_lock<std::mutex> lk(result_mutex);
        bool ok = result_cv.wait_for(lk,
                                     std::chrono::milliseconds(timeout_ms),
                                     [&] { return completed; });
        if (!ok) {
            VerifyResult timeout_r;
            timeout_r.content_id  = content_id;
            timeout_r.health      = ChannelHealth::ERROR;
            timeout_r.error       = "timeout de verificación síncrona";
            timeout_r.checked_at  = unix_time();
            return timeout_r;
        }
    }
    return sync_result;
}

// ---------------------------------------------------------------------------
// Encolado asíncrono
// ---------------------------------------------------------------------------

void ChannelVerifier::enqueue(const std::string& content_id,
                               std::function<void(VerifyResult)> callback) {
    if (content_id.empty()) return;

    {
        // Marcar como PENDING si no está ya marcado.
        std::unique_lock<std::shared_mutex> lk(state_mutex_);
        auto& r = state_[content_id];
        if (r.health == ChannelHealth::PENDING) return;  // ya encolado
        r.content_id = content_id;
        r.health     = ChannelHealth::PENDING;
    }

    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        task_queue_.push(Task{content_id, std::move(callback)});
    }
    queue_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// Worker loop
// ---------------------------------------------------------------------------

void ChannelVerifier::worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this] {
                return stop_.load() || !task_queue_.empty();
            });
            if (stop_.load() && task_queue_.empty()) break;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        // Adquirir semáforo: bloquea si ya hay max_workers_ verificaciones activas.
        semaphore_.acquire();

        VerifyResult result;
        try {
            result = run_pipeline(task.content_id);
        } catch (const std::exception& e) {
            result.content_id  = task.content_id;
            result.health      = ChannelHealth::ERROR;
            result.error       = std::string("worker exception: ") + e.what();
            result.checked_at  = unix_time();
        } catch (...) {
            result.content_id  = task.content_id;
            result.health      = ChannelHealth::ERROR;
            result.error       = "worker exception desconocida";
            result.checked_at  = unix_time();
        }

        // Persistir resultado en estado.
        {
            std::unique_lock<std::shared_mutex> lk(state_mutex_);
            state_[task.content_id] = result;
        }

        // Liberar semáforo.
        semaphore_.release();

        // Notificar a verify_sync y otros waiters.
        sync_cv_.notify_all();

        // Ejecutar callback si lo hay.
        if (task.callback) {
            try { task.callback(result); } catch (...) {}
        }
    }
}

// ---------------------------------------------------------------------------
// Construcción de la URL base del motor
// ---------------------------------------------------------------------------

std::string ChannelVerifier::engine_base_url() const {
    std::lock_guard<std::mutex> lk(engine_mutex_);
    return "http://" + ace_host_ + ":" + std::to_string(ace_http_port_);
}

// ---------------------------------------------------------------------------
// Pipeline de verificación  (4 fases + cierre obligatorio)
// ---------------------------------------------------------------------------

VerifyResult ChannelVerifier::run_pipeline(const std::string& content_id) {
    VerifyResult result;
    result.content_id  = content_id;
    result.checked_at  = unix_time();

    std::string command_url;   // guardado para cierre garantizado

    try {
        // -----------------------------------------------------------------------
        // FASE 1 + 2: Handshake y resolución del torrent
        // -----------------------------------------------------------------------
        SessionUrls session = phase_handshake(content_id);
        result.phase_reached = 2;
        command_url = session.command_url;

        // -----------------------------------------------------------------------
        // FASE 3 + 4: Observación DHT / swarm y lectura de bitrate
        // -----------------------------------------------------------------------
        phase_observe(session.stat_url, result);
        // phase_reached actualizado internamente hasta 4

    } catch (const std::runtime_error& e) {
        // Error en handshake o resolución de torrent.
        result.health      = ChannelHealth::BLOCKED;
        result.error       = e.what();
        result.checked_at  = unix_time();
        // Cierre si tenemos command_url.
        if (!command_url.empty()) stop_session(command_url);
        return result;
    } catch (const std::exception& e) {
        result.health     = ChannelHealth::ERROR;
        result.error      = e.what();
        result.checked_at = unix_time();
        if (!command_url.empty()) stop_session(command_url);
        return result;
    }

    // -----------------------------------------------------------------------
    // CIERRE OBLIGATORIO (siempre, incluso si la observación fue exitosa)
    // -----------------------------------------------------------------------
    if (!command_url.empty()) stop_session(command_url);

    result.checked_at = unix_time();
    return result;
}

// ---------------------------------------------------------------------------
// Fase 1 + 2: Handshake → resolución del torrent
// ---------------------------------------------------------------------------

ChannelVerifier::SessionUrls ChannelVerifier::phase_handshake(const std::string& content_id) {
    const std::string url = engine_base_url()
                            + "/ace/getstream?id=" + content_id
                            + "&format=json";

    HttpClientResponse resp;
    try {
        resp = http_client_.get_single(url, {}, kHandshakeTimeoutSec, false);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Fase1 handshake timeout: ") + e.what());
    }

    if (resp.status != 200) {
        throw std::runtime_error("Fase1 HTTP " + std::to_string(resp.status));
    }

    // Parsear JSON de respuesta del motor.
    Json j;
    try {
        j = Json::parse(resp.body);
    } catch (...) {
        throw std::runtime_error("Fase2 JSON parse error: " + resp.body.substr(0, 120));
    }

    // Detectar error de torrent antes de buscar URLs.
    if (j.contains("error")) {
        const std::string err = j["error"].as_string();
        if (!err.empty()) {
            throw std::runtime_error("Fase2 error motor: " + err);
        }
    }

    // Buscar respuesta bajo la clave "response" (estructura getstream).
    const Json& response_node = j.contains("response") ? j["response"] : j;

    const std::string stat_url    = response_node["stat_url"].as_string();
    const std::string command_url = response_node["command_url"].as_string();

    // Validar que no contengan el mensaje de error conocido.
    auto contains_torrent_error = [](const std::string& s) {
        return s.find("Cannot retrieve torrent") != std::string::npos;
    };

    if (stat_url.empty() || contains_torrent_error(stat_url)) {
        throw std::runtime_error("Fase2 stat_url inválida: " + stat_url.substr(0, 80));
    }
    if (command_url.empty()) {
        throw std::runtime_error("Fase2 command_url ausente");
    }

    return SessionUrls{stat_url, command_url};
}

// ---------------------------------------------------------------------------
// Fase 3 + 4: Observación DHT/swarm y lectura de bitrate real
// ---------------------------------------------------------------------------

void ChannelVerifier::phase_observe(const std::string& stat_url, VerifyResult& result) {
    using Clock = std::chrono::steady_clock;

    auto deadline = Clock::now() + std::chrono::milliseconds(kObserveTotalMs);
    int best_peers     = 0;
    long long best_speed = 0;
    std::string last_status;

    while (Clock::now() < deadline) {
        // Poll stat_url
        try {
            auto resp = http_client_.get_single(stat_url, {}, kStatPollTimeoutSec, false);
            if (resp.status == 200 && !resp.body.empty()) {
                result.phase_reached = 3;
                Json stat;
                try { stat = Json::parse(resp.body); } catch (...) { /* ignorar */ }

                // La respuesta puede estar envuelta en {"response": {...}}
                const Json& node = stat.contains("response") ? stat["response"] : stat;

                int    peers     = static_cast<int>(node["peers"].as_number(0));
                long long speed  = static_cast<long long>(node["speed_down"].as_number(0));
                std::string sts  = node["status"].as_string();

                if (peers     > best_peers)  best_peers  = peers;
                if (speed     > best_speed)  best_speed  = speed;
                if (!sts.empty())            last_status = sts;

                // Si ya tenemos señal positiva, no hace falta esperar más.
                if (last_status == "dl" && best_speed > 0) {
                    result.phase_reached = 4;
                    break;
                }
            }
        } catch (...) {
            // stat_url puede tardar en responder; continuamos el bucle.
        }

        // Esperar intervalo de poll.
        auto remaining = deadline - Clock::now();
        auto sleep_time = std::chrono::milliseconds(kObservePollMs);
        if (remaining < sleep_time) {
            std::this_thread::sleep_for(remaining);
            break;
        }
        std::this_thread::sleep_for(sleep_time);
    }

    // Fase 4 completada si llegamos hasta aquí con datos.
    if (result.phase_reached >= 3) result.phase_reached = 4;

    result.peers       = best_peers;
    result.speed_down  = best_speed;
    result.status_text = last_status;
    result.health      = classify(best_peers, best_speed, last_status,
                                  speed_threshold_.load());
}

// ---------------------------------------------------------------------------
// Cierre obligatorio de sesión AceStream
// ---------------------------------------------------------------------------

void ChannelVerifier::stop_session(const std::string& command_url) noexcept {
    // Construir URL de stop: command_url + "?method=stop" (o "&method=stop").
    std::string stop_url = command_url;
    if (stop_url.find('?') == std::string::npos)
        stop_url += "?method=stop";
    else
        stop_url += "&method=stop";

    try {
        http_client_.get_single(stop_url, {}, kStopTimeoutSec, false);
    } catch (...) {
        // El cierre es best-effort; ignorar errores de red.
    }
}

// ---------------------------------------------------------------------------
// Clasificador de estado de salud
// ---------------------------------------------------------------------------

ChannelHealth ChannelVerifier::classify(int peers,
                                         long long speed_down,
                                         const std::string& status_text,
                                         long long threshold) noexcept {
    // ONLINE: descarga activa, suficientes peers, bitrate mínimo superado.
    if (status_text == "dl" && peers >= 2 && speed_down > threshold) {
        return ChannelHealth::ONLINE;
    }

    // LOW_PEERS: algún peer disponible, descarga o prebuffering, pero velocidad baja.
    if ((status_text == "dl" || status_text == "prebuf" || status_text == "buf")
        && peers >= 1
        && speed_down <= threshold) {
        return ChannelHealth::LOW_PEERS;
    }

    // LOW_PEERS adicional: velocidad presente pero pocos peers.
    if ((status_text == "dl" || status_text == "prebuf") && speed_down > 0 && peers < 2) {
        return ChannelHealth::LOW_PEERS;
    }

    // OFFLINE: sin peers, sin velocidad (no eliminar el CID).
    if (peers == 0 && speed_down == 0) {
        return ChannelHealth::OFFLINE;
    }

    // Fallback conservador: si tenemos status sin peers claros.
    if (status_text == "prebuf" && peers == 0) {
        return ChannelHealth::OFFLINE;
    }

    // En cualquier otro caso (estado ambiguo con algo de actividad).
    return ChannelHealth::LOW_PEERS;
}

} // namespace httpace
