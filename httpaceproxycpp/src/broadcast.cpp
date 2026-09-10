#include "httpaceproxycpp/broadcast.hpp"
#include "httpaceproxycpp/util.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace httpace {

ChunkQueue::ChunkQueue(std::size_t max_chunks, std::size_t max_bytes)
    : max_chunks_(std::max<std::size_t>(2, max_chunks)),
      max_bytes_(std::max<std::size_t>(1024 * 1024, max_bytes)) {}

PushResult ChunkQueue::push(std::vector<char> chunk, std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) return PushResult::Closed;
    std::size_t new_len = chunk.size();
    if ((chunks_.size() >= max_chunks_ || total_bytes_ + new_len > max_bytes_) && wait.count() > 0) {
        cv_space_.wait_for(lock, wait, [&] {
            return closed_ || (chunks_.size() < max_chunks_ && total_bytes_ + new_len <= max_bytes_);
        });
        if (closed_) return PushResult::Closed;
    }
    PushResult result = PushResult::Ok;
    while (!chunks_.empty() && (chunks_.size() >= max_chunks_ || total_bytes_ + new_len > max_bytes_)) {
        total_bytes_ -= chunks_.front().size();
        chunks_.pop_front();
        result = PushResult::DroppedOldest;
    }
    total_bytes_ += new_len;
    chunks_.push_back(std::move(chunk));
    cv_data_.notify_one();
    return result;
}

bool ChunkQueue::pop(std::vector<char>& chunk) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_data_.wait(lock, [&] { return closed_ || !chunks_.empty(); });
    if (chunks_.empty()) return false;
    chunk = std::move(chunks_.front());
    total_bytes_ -= chunk.size();
    chunks_.pop_front();
    cv_space_.notify_one();
    return true;
}

bool ChunkQueue::pop_timeout(std::vector<char>& chunk, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_data_.wait_for(lock, timeout, [&] { return closed_ || !chunks_.empty(); })) {
        return false;
    }
    if (chunks_.empty()) return false;
    chunk = std::move(chunks_.front());
    total_bytes_ -= chunk.size();
    chunks_.pop_front();
    cv_space_.notify_one();
    return true;
}

void ChunkQueue::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_data_.notify_all();
    cv_space_.notify_all();
}

bool ChunkQueue::is_closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::size_t ChunkQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

std::size_t ChunkQueue::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

Broadcast::Broadcast(std::string infohash, Config config, HttpClient& http_client,
                     std::map<std::string, std::string> start_params)
    : infohash_(std::move(infohash)),
      config_(std::move(config)),
      http_client_(http_client),
      start_params_(std::move(start_params)),
      ace_(std::make_shared<AceClient>(config_, "Broadcast_" + infohash_.substr(0, 8))) {
    ace_->authenticate();
}

Broadcast::~Broadcast() { stop(); }

namespace {

std::string detect_client_type(const std::string& user_agent, const std::string& referer) {
    std::string ua_lower = lower(user_agent);
    std::string ref_lower = lower(referer);

    if (ref_lower.find("/player/") != std::string::npos ||
        ua_lower.find("mozilla") != std::string::npos ||
        ua_lower.find("chrome") != std::string::npos ||
        ua_lower.find("safari") != std::string::npos ||
        ua_lower.find("firefox") != std::string::npos ||
        ua_lower.find("edge") != std::string::npos ||
        ua_lower.find("opera") != std::string::npos) {
        return "Web Player";
    }
    if (ua_lower.find("vlc") != std::string::npos) {
        return "VLC Player";
    }
    if (ua_lower.find("tivimate") != std::string::npos) return "TiviMate";
    if (ua_lower.find("kodi") != std::string::npos) return "Kodi";
    if (ua_lower.find("iptv") != std::string::npos) return "Cliente IPTV";
    if (ua_lower.find("ffmpeg") != std::string::npos) return "FFmpeg";
    if (ua_lower.find("ott-navigator") != std::string::npos || ua_lower.find("ott navigator") != std::string::npos) return "OTT Navigator";
    if (ua_lower.find("wiseplay") != std::string::npos) return "Wiseplay";
    if (!user_agent.empty()) return "Cliente IPTV";
    return "Desconocido";
}

} // namespace

std::shared_ptr<StreamClient> Broadcast::add_client(const std::string& client_ip,
                                                    const std::string& channel_name,
                                                    const std::string& channel_icon,
                                                    const std::string& user_agent,
                                                    const std::string& referer,
                                                    const std::string& stream_url,
                                                    const std::string& epg_title,
                                                    const std::string& epg_icon) {
    auto client = std::make_shared<StreamClient>();
    client->session_id = std::to_string(reinterpret_cast<std::uintptr_t>(client.get()));
    client->client_ip = client_ip;
    client->channel_name = channel_name;
    client->channel_icon = channel_icon.empty() ? "http://static.acestream.net/sites/acestream/img/ACE-logo.png" : channel_icon;
    client->user_agent = user_agent;
    client->referer = referer;
    client->client_type = detect_client_type(user_agent, referer);
    client->stream_url = stream_url;
    client->content_id = infohash_;
    client->epg_title = epg_title;
    client->epg_icon = epg_icon.empty() ? client->channel_icon : epg_icon;
    client->connection_time = unix_time();
    client->last_activity = client->connection_time;
    client->queue = std::make_shared<ChunkQueue>(static_cast<std::size_t>(std::max(2, config_.client_queue_size)), 8 * 1024 * 1024);
    client->ace = ace_;
    client->current_broadcast = shared_from_this();
    subscribers_.fetch_add(1, std::memory_order_relaxed);
    zero_subscribers_time_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.push_back(client);
    }
    return client;
}

void Broadcast::remove_client(const std::shared_ptr<StreamClient>& client) {
    if (!client) return;
    client->queue->close();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [&](const auto& weak) {
            auto locked = weak.lock();
            return !locked || locked == client;
        }), clients_.end());
    }
    int remaining = subscribers_.fetch_sub(1, std::memory_order_relaxed) - 1;
    if (remaining <= 0) {
        subscribers_.store(0, std::memory_order_relaxed);
        zero_subscribers_time_.store(unix_time(), std::memory_order_relaxed);
    }
}

void Broadcast::detach_client_for_migration(const std::shared_ptr<StreamClient>& client) {
    if (client) {
        // Desvincular el cliente sin cerrar su ChunkQueue para handoff limpio
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [&](const auto& weak) {
            auto locked = weak.lock();
            return !locked || locked == client;
        }), clients_.end());
        int remaining = subscribers_.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (remaining <= 0) {
            subscribers_.store(0, std::memory_order_relaxed);
            zero_subscribers_time_.store(unix_time(), std::memory_order_relaxed);
        }
    } else {
        if (subscribers_.load(std::memory_order_relaxed) <= 0 && zero_subscribers_time_.load(std::memory_order_relaxed) == 0) {
            zero_subscribers_time_.store(unix_time(), std::memory_order_relaxed);
        }
    }
}

void Broadcast::attach_migrated_client(const std::shared_ptr<StreamClient>& client) {
    if (!client) return;
    client->content_id = infohash_;
    client->ace = ace_;
    client->current_broadcast = shared_from_this();
    subscribers_.fetch_add(1, std::memory_order_relaxed);
    zero_subscribers_time_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.push_back(client);
    }
    // Reinyección limpia de PAT/PMT del nuevo stream
    {
        std::lock_guard<std::mutex> lock(pat_pmt_mutex_);
        if (!latest_pat_pmt_.empty()) {
            client->queue->push(latest_pat_pmt_, std::chrono::milliseconds(50));
        }
    }
}

double Broadcast::get_bitrate_kbps() {
    auto now = unix_time();
    auto last_time = last_bitrate_calc_time_.load(std::memory_order_relaxed);
    if (last_time == 0) {
        last_bitrate_calc_time_.store(now, std::memory_order_relaxed);
        last_calc_bytes_.store(total_bytes_received_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return current_bitrate_kbps_.load(std::memory_order_relaxed);
    }
    auto dt = now - last_time;
    if (dt >= 2) {
        auto current_bytes = total_bytes_received_.load(std::memory_order_relaxed);
        auto prev_bytes = last_calc_bytes_.exchange(current_bytes, std::memory_order_relaxed);
        last_bitrate_calc_time_.store(now, std::memory_order_relaxed);
        long long delta = current_bytes - prev_bytes;
        double kbps = (dt > 0 && delta >= 0) ? (static_cast<double>(delta) / dt / 1024.0) : 0.0;
        current_bitrate_kbps_.store(kbps, std::memory_order_relaxed);

        if (kbps < 450.0 && total_bytes_received_.load() > 64 * 1024) {
            if (low_bitrate_start_time_.load(std::memory_order_relaxed) == 0) {
                low_bitrate_start_time_.store(now, std::memory_order_relaxed);
            }
        } else {
            low_bitrate_start_time_.store(0, std::memory_order_relaxed);
        }
    }
    return current_bitrate_kbps_.load(std::memory_order_relaxed);
}

bool Broadcast::is_bitrate_degraded(int seconds_threshold) {
    get_bitrate_kbps();
    auto low_start = low_bitrate_start_time_.load(std::memory_order_relaxed);
    if (low_start > 0) {
        auto now = unix_time();
        if ((now - low_start) >= seconds_threshold) {
            return true;
        }
    }
    return false;
}

std::size_t Broadcast::client_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& weak : clients_) if (!weak.expired()) ++count;
    return count;
}

std::vector<std::shared_ptr<StreamClient>> Broadcast::clients() const {
    std::vector<std::shared_ptr<StreamClient>> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& weak : clients_) {
        if (auto client = weak.lock()) out.push_back(client);
    }
    return out;
}

std::map<std::string, std::string> Broadcast::get_p2p_status() const {
    std::map<std::string, std::string> st;
    if (ace_) {
        st = ace_->get_cached_status();
    }
    if (running_ && client_count() > 0) {
        if (!st.contains("status") || st["status"].empty() || st["status"] == "error" || st["status"] == "idle") {
            st["status"] = "DL";
        }
    }
    return st;
}

void Broadcast::start_once() {
    bool expected = false;
    if (started_.compare_exchange_strong(expected, true)) {
        running_ = true;
        stream_thread_ = std::thread(&Broadcast::stream_loop, this);
        keepalive_thread_ = std::thread(&Broadcast::keepalive_loop, this);
    }
}

void Broadcast::stop() {
    // Bloquear terminantemente el comando STOP si todavía existen suscriptores activos o clientes conectados
    if (subscribers_.load(std::memory_order_relaxed) > 0 || client_count() > 0) {
        log_line("WARNING", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
                 "] Broadcast::stop bloqueado: sesión con " + std::to_string(subscribers_.load()) + " suscriptores y " +
                 std::to_string(client_count()) + " clientes activos. PROHIBIDO enviar STOP a AceStream.");
        return;
    }

    // Verificar período de gracia linger_timeout (mínimo 60s)
    auto zero_time = zero_subscribers_time_.load(std::memory_order_relaxed);
    auto now = unix_time();
    int linger = std::max(60, config_.linger_timeout);
    if (zero_time > 0 && (now - zero_time) < linger) {
        log_line("INFO", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
                 "] Broadcast::stop aplazado: sesión en período de gracia (" + std::to_string(now - zero_time) + "/" +
                 std::to_string(linger) + "s). Stream de fondo protegido.");
        return;
    }

    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(ts_residual_mutex_);
        if (!ts_residual_.empty()) {
            for (auto& client : clients()) {
                client->queue->push(ts_residual_, std::chrono::milliseconds(50));
            }
            ts_residual_.clear();
        }
    }
    for (auto& client : clients()) client->queue->close();
    if (ace_) {
        try { ace_->stop_broadcast(); } catch (...) {}
        try { ace_->shutdown(); } catch (...) {}
    }

    if (stream_thread_.joinable() && stream_thread_.get_id() != std::this_thread::get_id()) stream_thread_.join();
    if (keepalive_thread_.joinable() && keepalive_thread_.get_id() != std::this_thread::get_id()) keepalive_thread_.join();
}

void Broadcast::keepalive_loop() {
    while (running_) {
        for (int i = 0; i < 50 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!running_) break;
        if (ace_) {
            try {
                ace_->status(3);
            } catch (...) {}
        }
    }
}

void Broadcast::stream_loop() {
    try {
        auto started = ace_->start_broadcast(start_params_);
        auto url = rewrite_url_host_port(url_decode(started.url), config_.ace_host, std::to_string(config_.ace_http_port));
        log_line("INFO", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) + "] stream URL " + url);
        if (ends_with(parse_url(url).path, ".m3u8")) stream_hls_url(url);
        else stream_http_url(url);
    } catch (const std::exception& e) {
        log_line("ERROR", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) + "] stream failed: " + std::string(e.what()));
    } catch (...) {
        log_line("ERROR", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) + "] stream failed with unknown error");
    }
    running_ = false;
    // Solo enviar STOP al motor AceStream si NO quedan suscriptores ni clientes conectados
    if (subscribers_.load(std::memory_order_relaxed) <= 0 && client_count() == 0) {
        if (ace_) {
            try { ace_->stop_broadcast(); } catch (...) {}
            try { ace_->shutdown(); } catch (...) {}
        }
    }
    for (auto& client : clients()) client->queue->close();
}

void Broadcast::stream_http_url(const std::string& url) {
    http_client_.stream(url, [&](const char* data, std::size_t size) {
        broadcast_chunk(data, size);
        return running_.load();
    }, running_, 5, config_.video_timeout, std::max(1, config_.curl_stream_buffer));
}

void Broadcast::stream_hls_url(const std::string& url) {
    std::vector<std::string> seen;
    while (running_) {
        try {
            auto response = http_client_.get(url, {}, config_.video_timeout);
            auto base = parse_url(url);
            for (const auto& raw_line : split(response.body, '\n', false)) {
                auto line = trim(raw_line);
                if (line.empty() || starts_with(line, "#")) continue;
                std::string segment = line;
                if (!starts_with(segment, "http://") && !starts_with(segment, "https://")) {
                    auto base_path = base.path;
                    auto slash = base_path.find_last_of('/');
                    base_path = slash == std::string::npos ? "/" : base_path.substr(0, slash + 1);
                    segment = base.scheme + "://" + base.authority + base_path + segment;
                }
                if (std::find(seen.begin(), seen.end(), segment) != seen.end()) continue;
                stream_http_url(segment);
                seen.push_back(segment);
                if (seen.size() > 50) seen.erase(seen.begin());
                if (!running_) break;
            }
        } catch (const std::exception& e) {
            log_line("ERROR", "HLS refresh failed: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void Broadcast::broadcast_chunk(const char* data, std::size_t size) {
    if (!data || size == 0) return;

    std::vector<char> chunk_to_push;
    {
        std::lock_guard<std::mutex> lock(ts_residual_mutex_);
        ts_residual_.insert(ts_residual_.end(), data, data + size);
        std::size_t total = ts_residual_.size();
        std::size_t aligned_size = (total / 188) * 188;

        if (aligned_size > 0) {
            chunk_to_push.assign(ts_residual_.begin(), ts_residual_.begin() + aligned_size);
            ts_residual_.erase(ts_residual_.begin(), ts_residual_.begin() + aligned_size);
        } else {
            return;
        }
    }

    total_bytes_received_.fetch_add(chunk_to_push.size(), std::memory_order_relaxed);

    // Cache latest PAT / PMT packets from stream (PAT PID is 0)
    {
        for (std::size_t offset = 0; offset + 188 <= chunk_to_push.size(); offset += 188) {
            const unsigned char* pkt = reinterpret_cast<const unsigned char*>(chunk_to_push.data() + offset);
            if (pkt[0] == 0x47) {
                int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
                if (pid == 0) {
                    std::lock_guard<std::mutex> lock(pat_pmt_mutex_);
                    std::size_t pmt_len = (offset + 376 <= chunk_to_push.size()) ? 376 : 188;
                    latest_pat_pmt_.assign(chunk_to_push.begin() + offset, chunk_to_push.begin() + offset + pmt_len);
                    break;
                }
            }
        }
    }

    auto write_timeout = std::max(1, config_.client_write_timeout);
    auto wait = std::chrono::milliseconds(write_timeout * 1000 / 4);
    auto now = unix_time();
    for (auto& client : clients()) {
        auto result = client->queue->push(chunk_to_push, wait);
        if (result == PushResult::Closed) continue;
        if (result == PushResult::DroppedOldest) {
            client->dropped_chunks.fetch_add(1, std::memory_order_relaxed);
            if (now - client->last_activity.load() > write_timeout) {
                if (!client->stuck_logged.exchange(true)) {
                    log_line("WARNING", "[" + client->client_ip + "] client too slow ("
                             + std::to_string(client->dropped_chunks.load()) + " chunks dropped in "
                             + std::to_string(write_timeout) + "s), closing");
                    client->queue->close();
                }
            }
        } else {
            client->dropped_chunks.store(0, std::memory_order_relaxed);
        }
    }
}

BroadcastManager::BroadcastManager(Config config, HttpClient& http_client)
    : config_(std::move(config)), http_client_(http_client) {
    start_reaper();
}

BroadcastManager::~BroadcastManager() {
    stop_reaper();
    stop_all();
}

void BroadcastManager::start_reaper() {
    bool expected = false;
    if (reaper_running_.compare_exchange_strong(expected, true)) {
        reaper_thread_ = std::thread([this]() {
            while (reaper_running_) {
                {
                    std::unique_lock<std::mutex> lock(reaper_mutex_);
                    reaper_cv_.wait_for(lock, std::chrono::seconds(15), [this] {
                        return !reaper_running_;
                    });
                }
                if (!reaper_running_) break;
                try {
                    reap_inactive_sessions(20);
                } catch (...) {}
            }
        });
    }
}

void BroadcastManager::stop_reaper() {
    reaper_running_ = false;
    reaper_cv_.notify_all();
    if (reaper_thread_.joinable() && reaper_thread_.get_id() != std::this_thread::get_id()) {
        reaper_thread_.join();
    }
}

void BroadcastManager::reap_inactive_sessions(std::int64_t max_idle_seconds) {
    auto now = unix_time();
    int linger = std::max(60, config_.linger_timeout);
    std::vector<std::shared_ptr<Broadcast>> to_stop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = broadcasts_.begin(); it != broadcasts_.end(); ) {
            auto broadcast = it->second;
            if (!broadcast) {
                it = broadcasts_.erase(it);
                continue;
            }
            int subs = broadcast->get_subscribers();
            std::size_t c_count = broadcast->client_count();
            // Ambos deben ser cero: no deben quedar suscriptores ni clientes conectados
            if (subs <= 0 && c_count == 0) {
                auto zero_time = broadcast->get_zero_subscribers_time();
                if (zero_time == 0) {
                    // Primer avistamiento sin suscriptores: marcar inicio de gracia linger_timeout (60s)
                    broadcast->detach_client_for_migration(nullptr);
                    ++it;
                } else if ((now - zero_time) >= linger) {
                    log_line("INFO", "[" + broadcast->infohash().substr(0, std::min<std::size_t>(8, broadcast->infohash().size())) +
                                     "] Reaper: período de gracia linger (" + std::to_string(linger) + "s) expirado. Deteniendo sesión.");
                    to_stop.push_back(broadcast);
                    it = broadcasts_.erase(it);
                } else {
                    // En período de gracia (linger_timeout >= 60s)
                    ++it;
                }
            } else {
                // Hay clientes o suscriptores activos: mantener vivo y cancelar temporizador
                broadcast->reset_zero_subscribers_time();
                ++it;
            }
        }
    }
    for (auto& b : to_stop) {
        b->stop();
    }
}

std::shared_ptr<Broadcast> BroadcastManager::get_or_create(const std::string& infohash,
                                                           const std::map<std::string, std::string>& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = broadcasts_.find(infohash);
    if (it != broadcasts_.end()) return it->second;
    auto broadcast = std::make_shared<Broadcast>(infohash, config_, http_client_, params);
    broadcasts_[infohash] = broadcast;
    return broadcast;
}

std::shared_ptr<Broadcast> BroadcastManager::find(const std::string& infohash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = broadcasts_.find(infohash);
    return it == broadcasts_.end() ? nullptr : it->second;
}

void BroadcastManager::remove_if_empty(const std::string& infohash) {
    std::shared_ptr<Broadcast> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = broadcasts_.find(infohash);
        if (it != broadcasts_.end()) {
            if (it->second->get_subscribers() <= 0 && it->second->client_count() == 0) {
                auto now = unix_time();
                auto zero_time = it->second->get_zero_subscribers_time();
                int linger = std::max(60, config_.linger_timeout);
                if (zero_time > 0 && (now - zero_time) >= linger) {
                    removed = it->second;
                    broadcasts_.erase(it);
                }
            }
        }
    }
    if (removed) removed->stop();
}

std::size_t BroadcastManager::broadcast_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return broadcasts_.size();
}

std::size_t BroadcastManager::client_count() const {
    std::size_t total = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [_, broadcast] : broadcasts_) total += broadcast->client_count();
    return total;
}

std::vector<std::shared_ptr<StreamClient>> BroadcastManager::all_clients() const {
    std::vector<std::shared_ptr<StreamClient>> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [_, broadcast] : broadcasts_) {
        auto clients = broadcast->clients();
        out.insert(out.end(), clients.begin(), clients.end());
    }
    return out;
}

void BroadcastManager::stop_all() {
    std::map<std::string, std::shared_ptr<Broadcast>> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy.swap(broadcasts_);
    }
    for (auto& [_, broadcast] : copy) broadcast->stop();
}

} // namespace httpace
