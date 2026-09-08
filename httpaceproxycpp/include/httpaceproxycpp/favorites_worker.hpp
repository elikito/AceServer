#pragma once

// ---------------------------------------------------------------------------
// favorites_worker.hpp  —  v09.08.05
//
// Servicio en segundo plano dedicado exclusivamente a la monitorización y
// mantenimiento de salud de los canales Favoritos (epg_favorites.json /
// channel_order.json).
//
// Características clave:
//   1. Arranque: Calienta la caché sondeando de forma secuencial los candidatos
//      de los canales favoritos (con 250ms de pausa entre consultas para no saturar AceStream).
//   2. Ciclo periódico: Re-sondea la salud de favoritos cada 10-15 minutos (configurable).
//   3. Evento Alta en Favoritos / On-Demand: Comprobación prioritaria inmediata al
//      añadir canales a favoritos o al solicitar resolución en frío.
//   4. Exclusión de no favoritos: Canales fuera de favoritos quedan fuera del sondeo
//      periódico para economizar CPU y ancho de banda.
// ---------------------------------------------------------------------------

#include "httpaceproxycpp/channel_verifier.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace httpace {

class Proxy;

class FavoritesHealthWorker {
public:
    explicit FavoritesHealthWorker(Proxy& proxy, ChannelVerifier& verifier, int interval_minutes = 15);
    ~FavoritesHealthWorker();

    FavoritesHealthWorker(const FavoritesHealthWorker&) = delete;
    FavoritesHealthWorker& operator=(const FavoritesHealthWorker&) = delete;

    /// Inicia el hilo de trabajo en segundo plano
    void start();

    /// Detiene de manera limpia el worker y espera su finalización
    void stop();

    /// Notifica que se han añadido nuevos canales a favoritos para sondeo prioritario inmediato
    void notify_favorites_changed(const std::vector<std::string>& newly_added_favorites = {});

    /// Encola una comprobación puntual de un canal (por demanda o al agregarse)
    void request_channel_probe(const std::string& slug_or_channel, bool priority = true);

    /// Estado del worker
    bool is_running() const;
    int get_interval_minutes() const;
    void set_interval_minutes(int mins);
    std::int64_t get_last_probe_time() const;

    /// Realiza un ciclo de sondeo completo sobre los canales favoritos (síncrono desde el hilo llamante)
    void probe_all_favorites_sync();

private:
    void run();
    void probe_channel(const std::string& slug);
    void probe_all_favorites();
    std::vector<std::string> get_favorites_list() const;

    Proxy& proxy_;
    ChannelVerifier& verifier_;
    int interval_minutes_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> priority_queue_;
    std::unordered_set<std::string> pending_slugs_;
    std::chrono::steady_clock::time_point last_full_probe_{};
    std::int64_t last_probe_unix_time_ = 0;
};

} // namespace httpace
