#pragma once

#include "httpaceproxycpp/ace_client.hpp"
#include "httpaceproxycpp/config.hpp"
#include "httpaceproxycpp/http_client.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace httpace {

enum class PushResult {
    Ok,
    DroppedOldest,
    Closed,
};

class ChunkQueue {
public:
    explicit ChunkQueue(std::size_t max_chunks = 512, std::size_t max_bytes = 8 * 1024 * 1024);
    PushResult push(std::vector<char> chunk, std::chrono::milliseconds wait);
    bool pop(std::vector<char>& chunk);
    bool pop_timeout(std::vector<char>& chunk, std::chrono::milliseconds timeout);
    void close();
    bool is_closed() const;
    std::size_t size() const;
    std::size_t bytes() const;

private:
    std::size_t max_chunks_;
    std::size_t max_bytes_;
    std::size_t total_bytes_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable cv_data_;
    std::condition_variable cv_space_;
    std::deque<std::vector<char>> chunks_;
    bool closed_ = false;
};

class Broadcast;

struct StreamClient {
    std::string session_id;
    std::string client_ip;
    std::string channel_name;
    std::string channel_icon;
    std::string user_agent;
    std::string referer;
    std::string client_type;
    std::string stream_url;
    std::string content_id;
    std::string epg_title;
    std::string epg_icon;
    std::string auto_slug;
    std::string req_quality;
    std::int64_t connection_time = 0;
    std::shared_ptr<ChunkQueue> queue;
    std::weak_ptr<AceClient> ace;
    std::weak_ptr<Broadcast> current_broadcast;
    std::atomic<std::int64_t> last_activity{0};
    std::atomic<int> dropped_chunks{0};
    std::atomic<bool> stuck_logged{false};
};

class Broadcast : public std::enable_shared_from_this<Broadcast> {
public:
    Broadcast(std::string infohash, Config config, HttpClient& http_client,
              std::map<std::string, std::string> start_params);
    ~Broadcast();

    std::shared_ptr<StreamClient> add_client(const std::string& client_ip,
                                             const std::string& channel_name,
                                             const std::string& channel_icon,
                                             const std::string& user_agent = "",
                                             const std::string& referer = "",
                                             const std::string& stream_url = "",
                                             const std::string& epg_title = "",
                                             const std::string& epg_icon = "");
    void remove_client(const std::shared_ptr<StreamClient>& client);
    void detach_client_for_migration(const std::shared_ptr<StreamClient>& client);
    void attach_migrated_client(const std::shared_ptr<StreamClient>& client);
    std::size_t client_count() const;
    std::vector<std::shared_ptr<StreamClient>> clients() const;
    void start_once();
    void stop();
    std::shared_ptr<AceClient> ace() const { return ace_; }
    std::string infohash() const { return infohash_; }
    std::map<std::string, std::string> get_p2p_status() const;
    bool is_running() const { return running_.load(); }

    // v09.09.01 — Safe Reaper & Dynamic Stream Upgrader helpers
    int get_subscribers() const { return subscribers_.load(); }
    std::int64_t get_zero_subscribers_time() const { return zero_subscribers_time_.load(); }
    void reset_zero_subscribers_time() { zero_subscribers_time_.store(0, std::memory_order_relaxed); }
    bool has_valid_ts_data() const { return total_bytes_received_.load() >= 188; }
    double get_bitrate_kbps();
    bool is_bitrate_degraded(int seconds_threshold = 20);
    std::int64_t get_start_time() const { return start_time_.load(std::memory_order_relaxed); }

private:
    void stream_loop();
    void keepalive_loop();
    void stream_http_url(const std::string& url);
    void stream_hls_url(const std::string& url);
    void broadcast_chunk(const char* data, std::size_t size);

    std::string infohash_;
    Config config_;
    HttpClient& http_client_;
    std::map<std::string, std::string> start_params_;
    std::shared_ptr<AceClient> ace_;
    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<StreamClient>> clients_;
    std::atomic<bool> running_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
    std::thread stream_thread_;
    std::thread keepalive_thread_;
    std::mutex ts_residual_mutex_;
    std::vector<char> ts_residual_;

    // v09.09.01 — Ref-counting atómico y gracia linger_timeout
    std::atomic<int> subscribers_{0};
    std::atomic<std::int64_t> zero_subscribers_time_{0};
    std::atomic<std::int64_t> start_time_{0};

    // PAT/PMT reinjection buffer
    mutable std::mutex pat_pmt_mutex_;
    std::vector<char> latest_pat_pmt_;

    // Bitrate tracking
    std::atomic<long long> total_bytes_received_{0};
    std::atomic<long long> last_calc_bytes_{0};
    std::atomic<std::int64_t> last_bitrate_calc_time_{0};
    std::atomic<double> current_bitrate_kbps_{0.0};
    std::atomic<std::int64_t> low_bitrate_start_time_{0};
};

class BroadcastManager {
public:
    BroadcastManager(Config config, HttpClient& http_client);
    ~BroadcastManager();

    Config& config() { return config_; }
    const Config& config() const { return config_; }
    std::shared_ptr<Broadcast> get_or_create(const std::string& infohash,
                                             const std::map<std::string, std::string>& params);
    std::shared_ptr<Broadcast> find(const std::string& infohash) const;
    void remove_if_empty(const std::string& infohash);
    std::size_t broadcast_count() const;
    std::size_t client_count() const;
    std::vector<std::shared_ptr<StreamClient>> all_clients() const;
    void reap_inactive_sessions(std::int64_t max_idle_seconds = 20);
    void start_reaper();
    void stop_reaper();
    void stop_all();

private:
    Config config_;
    HttpClient& http_client_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Broadcast>> broadcasts_;
    std::atomic<bool> reaper_running_{false};
    std::thread reaper_thread_;
    std::condition_variable reaper_cv_;
    std::mutex reaper_mutex_;
};

} // namespace httpace
