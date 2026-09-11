#include "httpaceproxycpp/favorites_worker.hpp"
#include "httpaceproxycpp/proxy.hpp"
#include "httpaceproxycpp/stream_scorer.hpp"
#include "httpaceproxycpp/util.hpp"
#include "httpaceproxycpp/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace httpace {

FavoritesHealthWorker::FavoritesHealthWorker(Proxy& proxy, ChannelVerifier& verifier, int interval_minutes)
    : proxy_(proxy)
    , verifier_(verifier)
    , interval_minutes_(std::max(1, interval_minutes)) {
}

FavoritesHealthWorker::~FavoritesHealthWorker() {
    stop();
}

void FavoritesHealthWorker::start() {
    if (running_.exchange(true)) {
        return; // Ya en ejecución
    }
    worker_thread_ = std::thread(&FavoritesHealthWorker::run, this);
}

void FavoritesHealthWorker::stop() {
    if (!running_.exchange(false)) {
        return; // Ya detenido
    }
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool FavoritesHealthWorker::is_running() const {
    return running_.load();
}

int FavoritesHealthWorker::get_interval_minutes() const {
    return interval_minutes_;
}

void FavoritesHealthWorker::set_interval_minutes(int mins) {
    interval_minutes_ = std::max(1, mins);
}

std::int64_t FavoritesHealthWorker::get_last_probe_time() const {
    return last_probe_unix_time_;
}

void FavoritesHealthWorker::notify_favorites_changed(const std::vector<std::string>& newly_added_favorites) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& fav : newly_added_favorites) {
            if (!fav.empty() && pending_slugs_.insert(fav).second) {
                priority_queue_.push_back(fav);
            }
        }
    }
    cv_.notify_one();
}

void FavoritesHealthWorker::request_channel_probe(const std::string& slug_or_channel, bool priority) {
    if (slug_or_channel.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_slugs_.insert(slug_or_channel).second) {
            if (priority) {
                priority_queue_.push_front(slug_or_channel);
            } else {
                priority_queue_.push_back(slug_or_channel);
            }
        }
    }
    cv_.notify_one();
}

void FavoritesHealthWorker::probe_channel(const std::string& slug) {
    if (!running_ || slug.empty()) return;

    // v09.11.03 — Pausar temporalmente si hay al menos un cliente consumiendo stream
    while (running_ && proxy_.get_active_client_count() > 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1), [this] {
            return !running_ || proxy_.get_active_client_count() == 0;
        });
    }
    if (!running_) return;

    auto candidates = proxy_.find_candidates_for_channel(slug);
    if (candidates.empty()) return;

    StreamScorer::rank_candidates(candidates);

    // Sondear los mejores candidatos del canal (Top 5 con Pre-flight Probe real)
    size_t count = 0;
    for (const auto& cand : candidates) {
        if (!running_) break;
        if (cand.is_disabled) continue;
        if (count >= 5) break;

        // Si ya está activo en vivo dentro de BroadcastManager, está confirmado online
        if (cand.is_active_stream) continue;

        // Verificación de salud con Pre-flight Probe (timeout 4500ms, max_cache_age 90s para proteger motor)
        verifier_.verify_sync(cand.content_id, 4500, 90);
        count++;

        // Retardo estricto de 250 ms entre consultas para proteger el motor AceStream de saturación
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(250), [this] { return !running_; });
    }
}

std::vector<std::string> FavoritesHealthWorker::get_favorites_list() const {
    std::vector<std::string> result = proxy_.get_epg_favorites();
    std::unordered_set<std::string> seen;
    for (const auto& s : result) seen.insert(canonical_slug(s));

    // Revisar si existe un archivo channel_order.json adicional
    auto cfg_dir = proxy_.get_config().get_config_dir();
    std::vector<std::filesystem::path> order_paths = {
        cfg_dir / "channel_order.json",
        std::filesystem::path(proxy_.get_config().root_dir) / "http" / "listas" / "channel_order.json"
    };

    for (const auto& p : order_paths) {
        if (std::filesystem::exists(p)) {
            try {
                auto content = read_file_binary(p.string());
                if (!content.empty()) {
                    auto j = Json::parse(content);
                    std::vector<std::string> from_file;
                    if (j.is_array()) {
                        for (const auto& el : j.as_array()) {
                            if (el.is_string() && !el.as_string().empty()) from_file.push_back(el.as_string());
                        }
                    } else if (j.is_object()) {
                        auto obj = j.as_object();
                        if (obj.contains("favorites") && obj["favorites"].is_array()) {
                            for (const auto& el : obj["favorites"].as_array()) {
                                if (el.is_string() && !el.as_string().empty()) from_file.push_back(el.as_string());
                            }
                        } else if (obj.contains("order") && obj["order"].is_array()) {
                            for (const auto& el : obj["order"].as_array()) {
                                if (el.is_string() && !el.as_string().empty()) from_file.push_back(el.as_string());
                            }
                        }
                    }
                    for (const auto& f : from_file) {
                        auto cslug = canonical_slug(f);
                        if (!cslug.empty() && seen.find(cslug) == seen.end()) {
                            seen.insert(cslug);
                            result.push_back(f);
                        }
                    }
                }
            } catch (...) {}
        }
    }
    return result;
}

void FavoritesHealthWorker::probe_all_favorites() {
    if (!running_) return;

    auto favs = get_favorites_list();
    if (favs.empty()) {
        last_full_probe_ = std::chrono::steady_clock::now();
        last_probe_unix_time_ = unix_time();
        return;
    }

    log_line("INFO", "[FavoritesWorker] Iniciando sondeo de salud para " +
                     std::to_string(favs.size()) + " canales favoritos");

    for (const auto& fav_slug : favs) {
        if (!running_) break;

        // v09.11.03 — Pausar si hay clientes activos consumiendo un stream
        while (running_ && proxy_.get_active_client_count() > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !running_ || proxy_.get_active_client_count() == 0;
            });
        }
        if (!running_) break;

        // Procesar cualquier petición prioritaria urgente acumulada antes del siguiente canal
        while (running_) {
            std::string urgent_slug;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!priority_queue_.empty()) {
                    urgent_slug = priority_queue_.front();
                    priority_queue_.pop_front();
                    pending_slugs_.erase(urgent_slug);
                }
            }
            if (urgent_slug.empty()) break;
            probe_channel(urgent_slug);
        }

        probe_channel(fav_slug);
    }

    last_full_probe_ = std::chrono::steady_clock::now();
    last_probe_unix_time_ = unix_time();
    log_line("INFO", "[FavoritesWorker] Sondeo de salud de canales favoritos completado.");
}

void FavoritesHealthWorker::probe_all_favorites_sync() {
    probe_all_favorites();
}

void FavoritesHealthWorker::run() {
    log_line("INFO", "[FavoritesWorker] Servicio en background para favoritos activo (intervalo: " +
                     std::to_string(interval_minutes_) + " min)");

    // Pausa inicial de 2 segundos para dar tiempo a que los plugins de listas terminen de cargar
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return !running_; });
    }
    if (!running_) return;

    // Calentamiento inicial de la caché con los canales favoritos
    while (running_ && proxy_.get_active_client_count() > 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1), [this] {
            return !running_ || proxy_.get_active_client_count() == 0;
        });
    }
    if (!running_) return;
    probe_all_favorites();

    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto interval = std::chrono::minutes(std::max(1, interval_minutes_));
        cv_.wait_for(lock, interval, [this] {
            return !running_ || !priority_queue_.empty();
        });

        if (!running_) break;

        // Atender peticiones prioritarias si las hay
        while (!priority_queue_.empty() && running_) {
            std::string slug = priority_queue_.front();
            priority_queue_.pop_front();
            pending_slugs_.erase(slug);
            lock.unlock();

            probe_channel(slug);

            lock.lock();
        }

        // Si ha transcurrido el intervalo configurado desde el último sondeo completo, ejecutar ciclo
        auto now = std::chrono::steady_clock::now();
        if (now - last_full_probe_ >= interval) {
            lock.unlock();
            probe_all_favorites();
            lock.lock();
        }
    }

    log_line("INFO", "[FavoritesWorker] Servicio en background para favoritos finalizado");
}

} // namespace httpace
