#pragma once

#include "httpaceproxycpp/ace_client.hpp"
#include "httpaceproxycpp/broadcast.hpp"
#include "httpaceproxycpp/channel_verifier.hpp"
#include "httpaceproxycpp/config.hpp"
#include "httpaceproxycpp/http_client.hpp"
#include "httpaceproxycpp/http_server.hpp"
#include "httpaceproxycpp/plugins.hpp"
#include "httpaceproxycpp/stream_scorer.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace httpace {

class Proxy {
public:
    explicit Proxy(Config config);
    ~Proxy();

    void start();
    void stop();
    void handle_http(const HttpRequest& request, ClientConnection& connection);

    Config& config() { return config_; }
    const Config& config() const { return config_; }
    BroadcastManager& broadcasts() { return broadcasts_; }
    PluginRegistry& plugins() { return plugins_; }
    HttpClient& http_client() { return http_client_; }

    Json status_json();
    Json plugins_json();
    Json check_channel_light(const std::string& plugin, const std::string& channel, const std::string& content_id);
    Json check_channel_peers(const std::string& content_id, int max_wait, const std::string& engine_name = "");
    Json check_epg_url(const std::string& url);

    // -----------------------------------------------------------------------
    // v08.24.03 — API de verificación de salud y diagnóstico de red
    // -----------------------------------------------------------------------
    /// Verifica un Content ID de forma síncrona (espera hasta timeout_ms).
    Json verify_channel(const std::string& content_id, int timeout_ms = 10000);

    /// Encola múltiples CIDs para verificación asíncrona.
    /// Devuelve inmediatamente el estado actual (PENDING / cached) de cada ID.
    Json verify_channels_batch(const std::vector<std::string>& ids);

    /// Retorna el mapa completo de estados de salud en memoria.
    /// Formato: { "<cid>": { "health": "ONLINE", "peers": N, ... }, ... }
    /// Preparado para selección de mejor stream por canal EPG.
    Json get_channel_health_map();

    /// Retorna el estado en memoria de un único CID sin lanzar nueva verificación.
    Json get_channel_health_one(const std::string& content_id);

    /// Diagnóstico de protección de red (Cloudflare WARP, Tailscale, IP de salida, Ruta Segura).
    Json get_network_diagnostics();

    // -----------------------------------------------------------------------
    // v08.25.06 — Motor de Selección Automática y Fallback de Canales
    // -----------------------------------------------------------------------
    std::vector<ChannelCandidate> find_candidates_for_channel(const std::string& query_or_slug);
    std::optional<ChannelCandidate> resolve_best_candidate(const std::string& query_or_slug);
    std::string generate_auto_playlist(const std::string& hostport, const std::string& specific_slug = "");

    bool is_plugin_enabled(const std::string& name) const;
    void set_plugin_enabled(const std::string& name, bool enabled);
    std::string get_plugin_url(const std::string& name, const std::string& fallback) const;
    void set_plugin_url(const std::string& name, const std::string& url);
    void load_plugins_state();
    void save_plugins_state();
    Json plugins_state_json() const;
    void add_custom_list_plugin(const std::string& name, const std::string& url);
    void remove_custom_list_plugin(const std::string& name);

    std::string cpu_detected() const;
    std::string selected_engine() const;
    std::string engine_mode() const;
    void set_engine(const std::string& name_or_mode);

    void add_bunker_log(const std::string& message);
    Json get_bunker_logs_json() const;
    void set_limits(int max_connections, int max_concurrent_channels);

private:
    struct CpuInfoResult {
        std::string cpu_detected;
        bool has_avx_or_sse42 = false;
    };
    static CpuInfoResult detect_cpu_info();
    bool is_fake_request(const HttpRequest& request) const;
    bool check_firewall(const std::string& client_ip) const;
    void handle_static(const HttpRequest& request, ClientConnection& connection, const std::string& root_prefix);
    void handle_core_stream(RequestContext& ctx);
    Json get_content_info(const std::map<std::string, std::string>& params);
    Json acestream_engine_status();
    void send_error(ClientConnection& connection, int status, const std::string& message);

    Config config_;
    HttpClient http_client_;
    BroadcastManager broadcasts_;
    PluginRegistry plugins_;
    std::unique_ptr<HttpServer> server_;
    std::mutex idle_mutex_;
    std::unique_ptr<AceClient> idle_ace_;
    std::mutex ace_status_mutex_;
    std::chrono::steady_clock::time_point ace_status_time_{};
    Json ace_status_cache_;
    Json plugins_state_json_;
    mutable std::mutex plugins_state_mutex_;
    CpuInfoResult cpu_info_;
    std::string engine_mode_{"auto"};
    mutable std::mutex bunker_mutex_;
    std::vector<std::string> bunker_logs_;

    // v08.24.02 — Worker Pool de verificación de canales
    ChannelVerifier channel_verifier_;
};

} // namespace httpace
