// ---------------------------------------------------------------------------
// channel_verifier.cpp  —  v08.24.06
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
    constexpr long kHandshakeTimeoutSec = 4;       // Fase 1: getstream handshake
    constexpr long kStatPollTimeoutSec  = 2;       // Fase 3: cada poll stat_url
    constexpr long kStopTimeoutSec      = 2;       // Cierre: command_url?method=stop
    constexpr int  kObserveTotalMs      = 3500;    // Fase 3: ventana de observación (ms)
    constexpr int  kObservePollMs       = 350;     // Fase 3: intervalo entre polls (ms)
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
    // Resolver host primario mediante env var ACE_HOST / ACESTREAM_HOST si ace_host_ está vacío o es default
    if (const char* env_host = std::getenv("ACE_HOST")) {
        if (*env_host) ace_host_ = env_host;
    } else if (const char* env_host2 = std::getenv("ACESTREAM_HOST")) {
        if (*env_host2) ace_host_ = env_host2;
    }

    if (ace_host_.empty()) {
        ace_host_ = "aceserve-modern";
    }

    if (ace_http_port_ <= 0) {
        ace_http_port_ = 6878;
    }

    // Leer override del puerto HTTP del motor desde variable de entorno.
    if (const char* env_port = std::getenv("ACE_ENGINE_HTTP_PORT")) {
        try {
            int p = std::stoi(env_port);
            if (p > 0 && p < 65536) ace_http_port_ = p;
        } catch (...) {}
    }

    // Arrancar threads de worker.
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

void ChannelVerifier::set_active_stream_checker(ActiveChecker checker) {
    active_checker_ = std::move(checker);
}

// ---------------------------------------------------------------------------
// Estado en memoria
// ---------------------------------------------------------------------------

void ChannelVerifier::update_state(const VerifyResult& result) {
    std::unique_lock<std::shared_mutex> lk(state_mutex_);
    state_[result.content_id] = result;
    sync_cv_.notify_all();
}

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

    // 1. Bypass para streams activamente reproduciéndose en el proxy
    if (active_checker_) {
        auto active_res = active_checker_(content_id);
        if (active_res.has_value()) {
            std::unique_lock<std::shared_mutex> lk(state_mutex_);
            state_[content_id] = *active_res;
            sync_cv_.notify_all();
            return *active_res;
        }
    }

    // 2. Comprobar caché válida
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

    // 3. Comprobar si ya hay una verificación en curso y esperar su finalización
    {
        std::unique_lock<std::shared_mutex> lk(state_mutex_);
        auto it = state_.find(content_id);
        if (it != state_.end() && it->second.health == ChannelHealth::PENDING) {
            int64_t pending_age = unix_time() - it->second.checked_at;
            if (pending_age < 12) {
                // Tarea en curso legítima: esperar notificación de finalización
                lk.unlock();
                auto deadline = std::chrono::steady_clock::now()
                                + std::chrono::milliseconds(timeout_ms);
                std::unique_lock<std::mutex> slk(sync_mutex_);
                bool finished = sync_cv_.wait_until(slk, deadline, [&] {
                    std::shared_lock<std::shared_mutex> rlk(state_mutex_);
                    auto sit = state_.find(content_id);
                    return sit != state_.end()
                           && sit->second.health != ChannelHealth::PENDING;
                });
                if (finished) {
                    return get_cached(content_id);
                }
                // Si venció el timeout de espera de la verificación previa, forzamos nueva ejecución
            }
        }
        // Marcar como PENDING con timestamp actual
        VerifyResult& r = state_[content_id];
        r.content_id = content_id;
        r.health     = ChannelHealth::PENDING;
        r.checked_at = unix_time();
    }

    // 4. Encolar tarea y esperar por condition_variable con punteros compartidos seguros
    auto res_mutex = std::make_shared<std::mutex>();
    auto res_cv = std::make_shared<std::condition_variable>();
    auto completed = std::make_shared<bool>(false);
    auto sync_result = std::make_shared<VerifyResult>();

    auto cb = [res_mutex, res_cv, completed, sync_result](VerifyResult res) {
        {
            std::lock_guard<std::mutex> lk(*res_mutex);
            *sync_result = res;
            *completed   = true;
        }
        res_cv->notify_all();
    };

    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        task_queue_.push(Task{content_id, std::move(cb), true});
    }
    queue_cv_.notify_one();

    std::unique_lock<std::mutex> lk(*res_mutex);
    bool ok = res_cv->wait_for(lk,
                                 std::chrono::milliseconds(timeout_ms),
                                 [&] { return *completed; });

    if (!ok) {
        VerifyResult timeout_r;
        timeout_r.content_id  = content_id;
        timeout_r.health      = ChannelHealth::ERROR;
        timeout_r.error       = "timeout de verificación síncrona";
        timeout_r.checked_at  = unix_time();
        {
            std::unique_lock<std::shared_mutex> slk(state_mutex_);
            state_[content_id] = timeout_r;
        }
        sync_cv_.notify_all();
        return timeout_r;
    }

    return *sync_result;
}

// ---------------------------------------------------------------------------
// Encolado asíncrono
// ---------------------------------------------------------------------------

void ChannelVerifier::enqueue(const std::string& content_id,
                               std::function<void(VerifyResult)> callback) {
    if (content_id.empty()) return;

    // Bypass para streams activos
    if (active_checker_) {
        auto active_res = active_checker_(content_id);
        if (active_res.has_value()) {
            {
                std::unique_lock<std::shared_mutex> lk(state_mutex_);
                state_[content_id] = *active_res;
            }
            sync_cv_.notify_all();
            if (callback) {
                try { callback(*active_res); } catch (...) {}
            }
            return;
        }
    }

    {
        std::unique_lock<std::shared_mutex> lk(state_mutex_);
        auto it = state_.find(content_id);
        if (it != state_.end() && it->second.health == ChannelHealth::PENDING) {
            // Si ya está pendiente y tiene menos de 12 segundos, no duplicar encolado
            if (unix_time() - it->second.checked_at < 12) {
                return;
            }
        }
        VerifyResult& r = state_[content_id];
        r.content_id = content_id;
        r.health     = ChannelHealth::PENDING;
        r.checked_at = unix_time();
    }

    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        task_queue_.push(Task{content_id, std::move(callback), false});
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

        // Bypass para streams activos antes de solicitar semáforo o motor
        if (active_checker_) {
            auto active_res = active_checker_(task.content_id);
            if (active_res.has_value()) {
                {
                    std::unique_lock<std::shared_mutex> lk(state_mutex_);
                    state_[task.content_id] = *active_res;
                }
                sync_cv_.notify_all();
                if (task.callback) {
                    try { task.callback(*active_res); } catch (...) {}
                }
                continue;
            }
        }

        // Adquirir semáforo con timeout de seguridad (8s) para evitar bloqueos perpetuos
        bool acquired = semaphore_.try_acquire_for(std::chrono::seconds(8));
        if (!acquired) {
            VerifyResult r;
            r.content_id  = task.content_id;
            r.health      = ChannelHealth::ERROR;
            r.error       = "timeout esperando worker disponible";
            r.checked_at  = unix_time();
            {
                std::unique_lock<std::shared_mutex> lk(state_mutex_);
                state_[task.content_id] = r;
            }
            sync_cv_.notify_all();
            if (task.callback) {
                try { task.callback(r); } catch (...) {}
            }
            continue;
        }

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

        // Liberar semáforo
        semaphore_.release();

        // Persistir resultado en estado
        {
            std::unique_lock<std::shared_mutex> lk(state_mutex_);
            state_[task.content_id] = result;
        }

        // Notificar a verify_sync y otros waiters
        sync_cv_.notify_all();

        // Ejecutar callback si lo hay
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
    std::string base_url = engine_base_url();
    std::string url = base_url + "/ace/getstream?id=" + content_id + "&format=json";

    HttpClientResponse resp;
    bool connected = false;
    try {
        resp = http_client_.get_single(url, {}, kHandshakeTimeoutSec, false);
        if (resp.status == 200) {
            connected = true;
        }
    } catch (const std::exception& e) {
        log_line("ERROR", "[verifier] Error de conexión al motor AceStream en Fase 1 (" + url + "): " + e.what());
    }

    // Si falló la conexión inicial, intentar resolver fallback host si se usaba 127.0.0.1 / localhost o aceserve-modern
    if (!connected) {
        std::string current_host;
        int current_port;
        {
            std::lock_guard<std::mutex> lk(engine_mutex_);
            current_host = ace_host_;
            current_port = ace_http_port_;
        }

        std::string fallback_host;
        if (current_host == "127.0.0.1" || current_host == "localhost" || current_host.empty()) {
            fallback_host = "aceserve-modern";
        } else if (current_host == "aceserve-modern") {
            fallback_host = "127.0.0.1";
        }

        if (!fallback_host.empty() && fallback_host != current_host) {
            std::string fallback_url = "http://" + fallback_host + ":" + std::to_string(current_port)
                                      + "/ace/getstream?id=" + content_id + "&format=json";
            try {
                resp = http_client_.get_single(fallback_url, {}, kHandshakeTimeoutSec, false);
                if (resp.status == 200) {
                    log_line("INFO", "[verifier] Fallback exitoso de host en Fase 1: cambiado de " + current_host + " a " + fallback_host);
                    set_ace_engine(fallback_host, current_port);
                    connected = true;
                }
            } catch (const std::exception& fb_err) {
                log_line("ERROR", "[verifier] Fallback de conexión (" + fallback_url + ") también falló: " + fb_err.what());
            }
        }
    }

    if (!connected) {
        if (resp.status > 0 && resp.status != 200) {
            log_line("ERROR", "[verifier] Fase 1 HTTP status " + std::to_string(resp.status) + " en URL: " + url + " - body: " + resp.body.substr(0, 100));
            throw std::runtime_error("Fase1 HTTP " + std::to_string(resp.status));
        }
        throw std::runtime_error("Fase1 handshake fallo conexion motor (" + url + ")");
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
    if ((status_text == "dl" || status_text == "prebuf" || status_text == "buf") && (peers >= 2 || speed_down > threshold)) {
        return ChannelHealth::ONLINE;
    }

    // LOW_PEERS: algún peer disponible o estado de buffer/descarga activo en motor.
    if ((status_text == "dl" || status_text == "prebuf" || status_text == "buf") || peers >= 1 || speed_down > 0) {
        return ChannelHealth::LOW_PEERS;
    }

    // OFFLINE: sin respuesta de estado, sin peers y sin velocidad (no eliminar el CID).
    if (peers == 0 && speed_down == 0 && status_text.empty()) {
        return ChannelHealth::OFFLINE;
    }

    // Fallback conservador: si el motor no reportó datos.
    return ChannelHealth::LOW_PEERS;
}

} // namespace httpace
