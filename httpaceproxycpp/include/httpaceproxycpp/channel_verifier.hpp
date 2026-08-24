#pragma once

// ---------------------------------------------------------------------------
// channel_verifier.hpp  —  v08.24.02
//
// Worker Pool asíncrono con concurrencia máxima configurable (default 2) para
// verificar la reproducibilidad real de Content IDs de AceStream via la API
// HTTP interna del motor (http://<host>:<ace_http_port>/ace/getstream).
//
// Pipeline de 4 fases:
//   1. Handshake      – GET /ace/getstream?id=<cid>&format=json   (timeout 3s)
//   2. Torrent Resolve – Validar stat_url + command_url, sin error torrent
//   3. DHT/Swarm      – Poll stat_url durante 2.5–3.0 s            (peers, status)
//   4. Bitrate Real   – Leer speed_down
//   CLOSE             – GET command_url?method=stop                 (timeout 1.5s)
//                       SIEMPRE ejecutado, incluso en error.
//
// Clasificación final:
//   ONLINE    : status=="dl"             && peers>=2 && speed_down > kSpeedThreshold
//   LOW_PEERS : (status=="dl"|"prebuf")  && peers>=1 && speed_down <= kSpeedThreshold
//   OFFLINE   : peers==0 && speed_down==0  (no se elimina el ID, sólo se marca)
//   BLOCKED   : timeout handshake / "Cannot retrieve torrent"
//   ERROR     : excepción / respuesta inesperada
//   PENDING   : en cola, aún no verificado
//   UNKNOWN   : nunca verificado
// ---------------------------------------------------------------------------

#include "httpaceproxycpp/config.hpp"
#include "httpaceproxycpp/http_client.hpp"
#include "httpaceproxycpp/json.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace httpace {

// ---------------------------------------------------------------------------
// Umbral de velocidad para clasificar como ONLINE (bytes/s).
// 102400 bytes/s ≈ 100 KB/s ≈ 800 Kbps.
// Sobreescribible en runtime mediante ChannelVerifier::set_speed_threshold().
// ---------------------------------------------------------------------------
inline constexpr long long kDefaultSpeedThreshold = 102400LL;

// ---------------------------------------------------------------------------
// Concurrencia máxima del Worker Pool.
// ---------------------------------------------------------------------------
inline constexpr int kDefaultMaxWorkers = 2;

// ---------------------------------------------------------------------------
// Enum de estado de salud de un Content ID.
// ---------------------------------------------------------------------------
enum class ChannelHealth {
    UNKNOWN,    ///< Nunca verificado
    PENDING,    ///< En cola o verificación en curso
    ONLINE,     ///< Reproducible: dl + peers>=2 + speed_down > threshold
    LOW_PEERS,  ///< Marginal: dl|prebuf + peers>=1 + speed bajo
    OFFLINE,    ///< Sin peers, sin velocidad (no eliminar ID)
    BLOCKED,    ///< Timeout / torrent no recuperable
    ERROR,      ///< Excepción inesperada
};

/// Convierte ChannelHealth a string legible (para JSON).
const char* health_to_string(ChannelHealth h) noexcept;

/// Convierte string a ChannelHealth (para reconstrucción desde JSON).
ChannelHealth health_from_string(const std::string& s) noexcept;

// ---------------------------------------------------------------------------
// Resultado completo de una verificación.
// ---------------------------------------------------------------------------
struct VerifyResult {
    std::string      content_id;
    ChannelHealth    health       = ChannelHealth::UNKNOWN;
    int              peers        = 0;
    long long        speed_down   = 0;   ///< bytes/s leídos de speed_down en stat_url
    std::string      status_text;        ///< "dl", "prebuf", "buf", etc.
    std::string      error;              ///< Descripción del error si aplica
    std::int64_t     checked_at  = 0;   ///< unix timestamp de fin de verificación
    int              phase_reached = 0;  ///< Última fase completada (1-4, 0=ninguna)

    /// Serializa el resultado a Json::object compatible con el resto del backend.
    Json to_json() const;
};

// ---------------------------------------------------------------------------
// ChannelVerifier — Worker Pool con semáforo de concurrencia estricta.
//
// Uso típico (síncrono):
//   VerifyResult r = verifier.verify_sync("abc...hash...", 8000 /*ms*/);
//
// Uso asíncrono:
//   verifier.enqueue("abc...hash...", [](VerifyResult r){ /* callback */ });
//   Json state = verifier.get_cached("abc...hash...");
//
// Consulta del mapa completo (para EPG / selector de mejor stream):
//   Json map = verifier.get_health_map();
// ---------------------------------------------------------------------------
class ChannelVerifier {
public:
    /// @param http_client  Referencia a HttpClient del proxy (compartida).
    /// @param ace_host     Host del motor AceStream (config_.ace_host).
    /// @param ace_http_port Puerto HTTP del motor (config_.ace_http_port).
    /// @param max_workers  Concurrencia máxima (1 o 2, según especificación).
    explicit ChannelVerifier(const HttpClient& http_client,
                             std::string ace_host,
                             int ace_http_port,
                             int max_workers = kDefaultMaxWorkers);

    ~ChannelVerifier();

    // No copyable, no movable (tiene threads internos).
    ChannelVerifier(const ChannelVerifier&) = delete;
    ChannelVerifier& operator=(const ChannelVerifier&) = delete;

    // ------------------------------------------------------------------
    // Verificación síncrona
    // Bloquea hasta obtener resultado o hasta que expire timeout_ms.
    // Si ya hay un resultado en caché reciente (< max_cache_age_s), lo retorna.
    // ------------------------------------------------------------------
    VerifyResult verify_sync(const std::string& content_id,
                             int timeout_ms = 10000,
                             int max_cache_age_s = 0);

    // ------------------------------------------------------------------
    // Encola una verificación asíncrona.
    // El callback se ejecuta desde el worker thread al completar.
    // Si content_id ya está PENDING, se ignora (no se encola dos veces).
    // ------------------------------------------------------------------
    void enqueue(const std::string& content_id,
                 std::function<void(VerifyResult)> callback = {});

    // ------------------------------------------------------------------
    // Consulta del estado en memoria (sin lanzar nueva verificación).
    // ------------------------------------------------------------------
    VerifyResult get_cached(const std::string& content_id) const;

    // ------------------------------------------------------------------
    // Devuelve el mapa completo de estados.
    // Formato JSON: { "<cid>": { "health": "ONLINE", "peers": 5, ... }, ... }
    // Pensado para el selector de mejor stream por canal EPG.
    // ------------------------------------------------------------------
    Json get_health_map() const;

    // ------------------------------------------------------------------
    // Configuración en runtime.
    // ------------------------------------------------------------------
    void set_speed_threshold(long long bytes_per_second);
    void set_ace_engine(const std::string& host, int http_port);

    // ------------------------------------------------------------------
    // Limpia el estado en memoria de un CID específico o de todos.
    // ------------------------------------------------------------------
    void clear_state(const std::string& content_id);
    void clear_all_state();

private:
    // ------------------------------------------------------------------
    // Implementación del pipeline de 4 fases + cierre.
    // Siempre ejecuta stop_session() antes de retornar.
    // ------------------------------------------------------------------
    VerifyResult run_pipeline(const std::string& content_id);

    // ------------------------------------------------------------------
    // Helpers internos del pipeline.
    // ------------------------------------------------------------------
    struct SessionUrls {
        std::string stat_url;
        std::string command_url;
    };

    /// Fase 1+2: Handshake + resolución del torrent.
    /// Retorna SessionUrls si OK, lanza std::runtime_error en fallo.
    SessionUrls phase_handshake(const std::string& content_id);

    /// Fase 3+4: Poll DHT/swarm + lectura de bitrate.
    /// Modifica result en función de lo observado.
    void phase_observe(const std::string& stat_url, VerifyResult& result);

    /// Cierre obligatorio: GET command_url?method=stop  (timeout 1.5s).
    void stop_session(const std::string& command_url) noexcept;

    /// Clasifica el resultado final en base a peers, speed_down y status_text.
    static ChannelHealth classify(int peers, long long speed_down,
                                  const std::string& status_text,
                                  long long threshold) noexcept;

    // ------------------------------------------------------------------
    // Construcción de la URL base del motor AceStream.
    // ------------------------------------------------------------------
    std::string engine_base_url() const;

    // ------------------------------------------------------------------
    // Worker loop — se ejecuta en cada thread del pool.
    // ------------------------------------------------------------------
    void worker_loop();

    // ------------------------------------------------------------------
    // Estado interno protegido.
    // ------------------------------------------------------------------
    const HttpClient&               http_client_;

    mutable std::mutex              engine_mutex_;
    std::string                     ace_host_;
    int                             ace_http_port_;

    std::atomic<long long>          speed_threshold_{ kDefaultSpeedThreshold };

    // Workers
    int                             max_workers_;
    std::counting_semaphore<kDefaultMaxWorkers> semaphore_;
    std::vector<std::thread>        workers_;
    std::atomic<bool>               stop_{ false };

    // Cola de tareas
    struct Task {
        std::string                          content_id;
        std::function<void(VerifyResult)>    callback;
    };
    mutable std::mutex              queue_mutex_;
    std::condition_variable         queue_cv_;
    std::queue<Task>                task_queue_;

    // Estado persistente en memoria (readers-writer lock)
    mutable std::shared_mutex       state_mutex_;
    std::unordered_map<std::string, VerifyResult> state_;

    // Notificación para verify_sync (por CID)
    mutable std::mutex              sync_mutex_;
    std::condition_variable         sync_cv_;
};

} // namespace httpace
