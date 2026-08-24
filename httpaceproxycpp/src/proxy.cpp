#include "httpaceproxycpp/proxy.hpp"
#include "httpaceproxycpp/util.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

#include <sys/utsname.h>

namespace httpace {
namespace {

bool is_video_extension(const std::string& path) {
    auto ext = extension_of(path);
    if (ext.empty()) return true;
    static const std::set<std::string> exts = {
        "avi", "flv", "m2ts", "mkv", "mpeg", "mpeg4", "mpegts", "mpg4", "mp4",
        "mpg", "mov", "mpv", "qt", "ts", "wmv", "m3u8"
    };
    return exts.contains(ext);
}

std::string header_host(const HttpRequest& request) {
    return request.header("host", "localhost:8888");
}

std::string map_reqtype_alias(std::string reqtype) {
    if (reqtype == "torrent") return "url";
    if (reqtype == "pid") return "content_id";
    return reqtype;
}

bool is_core_reqtype(const std::string& reqtype) {
    static const std::set<std::string> types = {"content_id", "url", "infohash", "direct_url", "data", "efile_url"};
    return types.contains(reqtype);
}

std::string value_or(const std::map<std::string, std::string>& values, const std::string& key, const std::string& fallback = "") {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

std::string os_platform() {
    struct utsname info {};
    if (uname(&info) == 0) {
        return std::string(info.sysname) + " " + info.release + " " + info.machine;
    }
    return "unknown";
}

Json::object memory_info() {
    double total = 0.0;
    double available = 0.0;
    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    double value = 0.0;
    std::string unit;
    while (meminfo >> key >> value >> unit) {
        if (key == "MemTotal:") total = value * 1024.0;
        else if (key == "MemAvailable:") available = value * 1024.0;
    }
    double used = total > available ? total - available : 0.0;
    return Json::object{{"total", total}, {"used", used}, {"available", available}};
}

Json::object disk_info() {
    try {
        auto space = std::filesystem::space("/");
        double total = static_cast<double>(space.capacity);
        double free = static_cast<double>(space.available);
        double used = total > free ? total - free : 0.0;
        return Json::object{{"total", total}, {"used", used}, {"free", free}};
    } catch (...) {
        return Json::object{{"total", 0}, {"used", 0}, {"free", 0}};
    }
}

Json::array cpu_percent() {
    struct CpuSample {
        unsigned long long idle = 0;
        unsigned long long total = 0;
    };

    std::ifstream stat("/proc/stat");
    std::vector<CpuSample> samples;
    std::string label;
    while (stat >> label) {
        if (!starts_with(label, "cpu") || label == "cpu") {
            std::string rest;
            std::getline(stat, rest);
            continue;
        }
        unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
        unsigned long long irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
        stat >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;
        samples.push_back(CpuSample{
            idle + iowait,
            user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice
        });
    }

    static std::mutex cpu_mutex;
    static std::vector<CpuSample> previous;
    std::lock_guard<std::mutex> lock(cpu_mutex);

    Json::array out;
    if (samples.empty()) {
        auto cores = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned int i = 0; i < cores; ++i) out.push_back(0);
        return out;
    }

    for (std::size_t i = 0; i < samples.size(); ++i) {
        double percent = 0.0;
        if (i < previous.size()) {
            auto total_delta = samples[i].total - previous[i].total;
            auto idle_delta = samples[i].idle - previous[i].idle;
            if (total_delta > 0 && total_delta >= idle_delta) {
                percent = (static_cast<double>(total_delta - idle_delta) * 100.0) / static_cast<double>(total_delta);
            }
        }
        out.push_back(percent);
    }
    previous = std::move(samples);
    return out;
}

Json system_info() {
    auto cpu = cpu_percent();
    return Json::object{
        {"os_platform", os_platform()},
        {"cpu_nums", static_cast<double>(cpu.size())},
        {"cpu_percent", cpu},
        {"cpu_freq", nullptr},
        {"cpu_temp", nullptr},
        {"mem_info", memory_info()},
        {"disk_info", disk_info()}
    };
}

} // namespace

Proxy::CpuInfoResult Proxy::detect_cpu_info() {
    CpuInfoResult res;
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::string model_name;
    std::string flags_str;

    while (std::getline(cpuinfo, line)) {
        if (starts_with(line, "model name")) {
            auto colon = line.find(':');
            if (colon != std::string::npos && model_name.empty()) {
                model_name = trim(line.substr(colon + 1));
            }
        } else if (starts_with(line, "flags") || starts_with(line, "Features")) {
            auto colon = line.find(':');
            if (colon != std::string::npos && flags_str.empty()) {
                flags_str = line.substr(colon + 1);
            }
        }
    }

    if (model_name.empty()) model_name = "x86_64 Processor";

    auto lower_flags = lower(flags_str);
    bool found_avx = lower_flags.find("avx") != std::string::npos;
    bool found_sse42 = lower_flags.find("sse4_2") != std::string::npos || lower_flags.find("sse4.2") != std::string::npos;

    res.has_avx_or_sse42 = found_avx || found_sse42;

    std::string critical;
    if (found_avx) critical += "AVX ";
    if (found_sse42) critical += "SSE4.2 ";
    if (critical.empty()) critical = "Sin AVX/SSE4.2";
    else critical = "Flags: " + trim(critical);

    res.cpu_detected = model_name + " (" + critical + ")";
    return res;
}

std::string Proxy::cpu_detected() const {
    return cpu_info_.cpu_detected;
}

std::string Proxy::selected_engine() const {
    return config_.ace_host;
}

std::string Proxy::engine_mode() const {
    return engine_mode_;
}

void Proxy::add_bunker_log(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm tm_buf{};
    localtime_r(&in_time_t, &tm_buf);
    ss << "[" << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
       << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
       << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "] " << message;

    {
        std::lock_guard<std::mutex> lock(bunker_mutex_);
        bunker_logs_.push_back(ss.str());
        if (bunker_logs_.size() > 200) {
            bunker_logs_.erase(bunker_logs_.begin());
        }
    }
    log_line("BUNKER", message);
}

Json Proxy::get_bunker_logs_json() const {
    std::vector<std::string> logs_copy;
    {
        std::lock_guard<std::mutex> lock(bunker_mutex_);
        logs_copy = bunker_logs_;
    }
    Json::array logs_arr;
    for (const auto& log : logs_copy) {
        logs_arr.push_back(log);
    }
    return Json::object{
        {"status", "success"},
        {"logs", logs_arr}
    };
}

void Proxy::set_limits(int max_connections, int max_concurrent_channels) {
    bool changed = false;
    if (max_connections >= 1 && max_connections <= 10 && config_.max_connections != max_connections) {
        config_.max_connections = max_connections;
        changed = true;
    }
    if (max_concurrent_channels >= 1 && max_concurrent_channels <= 5 && config_.max_concurrent_channels != max_concurrent_channels) {
        config_.max_concurrent_channels = max_concurrent_channels;
        changed = true;
    }
    if (changed) {
        add_bunker_log("Configuración HTTPAceProxy actualizada -> Conexiones Máximas: " +
                       std::to_string(config_.max_connections) +
                       ", Canales Máximos: " +
                       std::to_string(config_.max_concurrent_channels));
    }
}

void Proxy::set_engine(const std::string& name_or_mode) {
    std::string target_engine;
    std::string mode_label;

    if (name_or_mode.empty() || name_or_mode == "auto") {
        mode_label = "auto";
        target_engine = cpu_info_.has_avx_or_sse42 ? "aceserve-modern" : "aceserve-compat-light";
    } else if (name_or_mode == "aceserve-modern" || name_or_mode == "modern") {
        mode_label = "modern";
        target_engine = "aceserve-modern";
    } else if (name_or_mode == "aceserve-compat-light" || name_or_mode == "light") {
        mode_label = "compat-light";
        target_engine = "aceserve-compat-light";
    } else if (name_or_mode == "aceserve-compat-stable" || name_or_mode == "stable" || name_or_mode == "compat") {
        mode_label = "compat-stable";
        target_engine = "aceserve-compat-stable";
    } else {
        mode_label = "custom";
        target_engine = name_or_mode;
    }

    {
        std::lock_guard<std::mutex> lock(ace_status_mutex_);
        engine_mode_ = mode_label;
        config_.ace_host = target_engine;
        broadcasts_.config().ace_host = target_engine;
        ace_status_cache_ = Json();
        ace_status_time_ = {};
    }

    {
        std::lock_guard<std::mutex> lock(idle_mutex_);
        if (idle_ace_) {
            idle_ace_->shutdown();
            idle_ace_.reset();
        }
    }

    add_bunker_log("Cambio de motor solicitado: " + target_engine + " (modo: " + mode_label + ")");

    std::thread([this, target_engine]() {
        add_bunker_log("Verificando respuesta del motor " + target_engine + " en el puerto API 62062...");
        
        bool port_open = false;
        for (int i = 0; i < 15; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            try {
                AceClient probe(config_);
                probe.authenticate();
                port_open = true;
                probe.shutdown();
                break;
            } catch (...) {}
        }

        if (port_open) {
            add_bunker_log("Motor " + target_engine + " verificado con exito. Puerto 62062 respondiendo.");
        } else {
            add_bunker_log("Advertencia: Tiempo de espera agotado probando " + target_engine + " en puerto 62062.");
        }

        {
            std::lock_guard<std::mutex> lock(ace_status_mutex_);
            ace_status_cache_ = Json();
            ace_status_time_ = {};
        }
    }).detach();
}

Proxy::Proxy(Config config)
    : config_(std::move(config)), broadcasts_(config_, http_client_) {
    cpu_info_ = detect_cpu_info();
    add_bunker_log("Búnker HTTPAceProxy iniciado.");
    add_bunker_log("CPU Detectada: " + cpu_info_.cpu_detected);

    if (config_.ace_host == "auto" || config_.ace_host == "aceserve-engine") {
        set_engine("auto");
    } else {
        engine_mode_ = "manual";
        add_bunker_log("Motor configurado de forma manual: " + config_.ace_host);
    }

    load_plugins_state();
    auto plugins = create_plugins(config_, http_client_, *this);
    for (auto& plugin : plugins) plugins_.add(plugin);
}

Proxy::~Proxy() { stop(); }

void Proxy::start() {
    server_ = std::make_unique<HttpServer>(config_.http_host, config_.http_port,
        [this](const HttpRequest& request, ClientConnection& connection) { handle_http(request, connection); });
    server_->set_client_send_timeout(config_.client_write_timeout);
    server_->start();
    log_line("INFO", "HTTPAceProxyCPP started at " + config_.http_host + ":" + std::to_string(config_.http_port));
    server_->join();
}

void Proxy::stop() {
    broadcasts_.stop_all();
    if (server_) server_->stop();
}

void Proxy::handle_http(const HttpRequest& request, ClientConnection& connection) {
    log_line("INFO", "[" + request.client_ip + "] " + request.method + " " + request.target);
    if (config_.firewall && !check_firewall(request.header("x-forwarded-for", request.client_ip))) {
        send_error(connection, 401, "Dropping connection due to firewall rules");
        return;
    }
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST") {
        send_error(connection, 400, "Bad Request");
        return;
    }
    if (request.method == "HEAD" || is_fake_request(request)) {
        connection.send_response_headers(200, status_reason(200), {
            {"Content-Type", "video/mp2t"},
            {"Connection", "close"}
        });
        return;
    }

    RequestContext ctx{request, connection, request.path, request.query, split(request.path, '/', true), "", "m3u8"};

    if (ctx.parts.size() > 1 && ctx.parts[1] == "config") {
        auto action = query_get(ctx.query, "action");
        std::map<std::string, std::string> headers = {
            {"Access-Control-Allow-Origin", "*"},
            {"Content-Type", "application/json; charset=utf-8"},
            {"Connection", "close"}
        };
        
        if (action == "get_config") {
            Json config_obj = plugins_state_json_.is_null() ? Json::object{} : plugins_state_json_;
            if (config_obj.is_object()) {
                auto obj = config_obj.as_object();
                obj["version"] = kAppVersion;
                config_obj = obj;
            }
            connection.send_response_headers(200, status_reason(200), headers);
            connection.send_text(config_obj.dump(2));
            return;
        }
        else if (action == "upload_list") {
            std::string name = query_get(ctx.query, "name");
            std::string title = query_get(ctx.query, "title");
            std::string filename = query_get(ctx.query, "filename");
            std::string content = request.body;

            if (starts_with(trim(content), "{")) {
                try {
                    auto j = Json::parse(content);
                    if (j.is_object()) {
                        if (j.contains("name") && name.empty()) name = j["name"].as_string();
                        if (j.contains("title") && title.empty()) title = j["title"].as_string();
                        if (j.contains("filename") && filename.empty()) filename = j["filename"].as_string();
                        if (j.contains("content")) content = j["content"].as_string();
                    }
                } catch (...) {}
            }

            if (name.empty()) name = "custom_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            if (title.empty()) title = name;
            if (filename.empty()) filename = name + ".m3u";

            filename = replace_all(filename, "..", "");
            filename = replace_all(filename, "/", "");
            filename = replace_all(filename, "\\", "");
            if (!ends_with(filename, ".m3u") && !ends_with(filename, ".m3u8") && !ends_with(filename, ".txt")) {
                filename += ".m3u";
            }

            auto locales_dir = std::filesystem::path(config_.root_dir) / "http" / "listas" / "locales";
            std::filesystem::create_directories(locales_dir);
            auto file_path = locales_dir / filename;

            std::ofstream out(file_path, std::ios::binary);
            out << content;
            out.close();

            std::string relative_url = "/listas/locales/" + filename;

            {
                std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                Json::object obj;
                if (plugins_state_json_.is_object()) {
                    obj = plugins_state_json_.as_object();
                }
                Json::array arr;
                if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
                    arr = obj["custom_lists"].as_array();
                }
                bool found = false;
                for (auto& item : arr) {
                    if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                        Json::object item_obj = item.as_object();
                        item_obj["title"] = title;
                        item_obj["url"] = relative_url;
                        item_obj["enabled"] = true;
                        item = item_obj;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    arr.push_back(Json::object{
                        {"name", name},
                        {"title", title},
                        {"url", relative_url},
                        {"enabled", true}
                    });
                }
                obj["custom_lists"] = arr;
                plugins_state_json_ = obj;
            }
            save_plugins_state();
            add_custom_list_plugin(name, relative_url);

            Json res = Json::object{
                {"status", "success"},
                {"name", name},
                {"title", title},
                {"url", relative_url}
            };
            connection.send_response_headers(200, status_reason(200), headers);
            connection.send_text(res.dump(2));
            return;
        }
        else if (action == "save_internal") {
            std::string text_body = request.body;
            if (starts_with(trim(text_body), "{")) {
                try {
                    auto j = Json::parse(text_body);
                    if (j.is_object() && j.contains("content")) {
                        text_body = j["content"].as_string();
                    }
                } catch (...) {}
            }

            std::istringstream stream(text_body);
            std::string line;
            std::ostringstream m3u_out;
            m3u_out << "#EXTM3U url-tvg=\"https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz\"\n";

            int channel_count = 0;
            std::regex hash_regex(R"([a-fA-F0-9]{40})");
            std::string pending_name;
            std::string pending_extinf;
            std::string pending_group = "Interna";
            std::string pending_logo;
            std::string pending_tvg;

            auto reset_pending = [&]() {
                pending_name.clear();
                pending_extinf.clear();
                pending_group = "Interna";
                pending_logo.clear();
                pending_tvg.clear();
            };

            while (std::getline(stream, line)) {
                line = trim(line);
                if (line.empty() || line == "#EXTM3U") continue;
                if (starts_with(line, "#EXTVLCOPT:") || starts_with(line, "#EXTHTTP:") || starts_with(line, "#EXT-X-")) continue;

                if (starts_with(line, "#EXTINF:")) {
                    pending_extinf = line;
                    auto attrs = parse_extinf_attrs(line);
                    if (attrs.contains("group-title") && !attrs["group-title"].empty()) {
                        pending_group = attrs["group-title"];
                    }
                    if (attrs.contains("tvg-logo")) pending_logo = attrs["tvg-logo"];
                    if (attrs.contains("tvg-name")) pending_tvg = attrs["tvg-name"];
                    auto parsed_name = parse_extinf_name(line);
                    if (!parsed_name.empty() && parsed_name != "Unknown Channel") {
                        pending_name = parsed_name;
                    }
                    continue;
                }

                if (starts_with(line, "#EXTGRP:")) {
                    auto grp = trim(line.substr(8));
                    if (!grp.empty()) pending_group = grp;
                    continue;
                }

                std::smatch match;
                if (std::regex_search(line, match, hash_regex)) {
                    std::string hash = match.str();
                    std::string name = pending_name;

                    if (name.empty()) {
                        std::string clean_line = line;
                        clean_line = replace_all(clean_line, "acestream://", "");
                        clean_line = replace_all(clean_line, hash, "");
                        clean_line = trim(clean_line);
                        while (!clean_line.empty() && (clean_line.front() == '-' || clean_line.front() == ',' || clean_line.front() == ':' || clean_line.front() == '"' || clean_line.front() == '\'')) {
                            clean_line = trim(clean_line.substr(1));
                        }
                        while (!clean_line.empty() && (clean_line.back() == '-' || clean_line.back() == ',' || clean_line.back() == ':' || clean_line.back() == '"' || clean_line.back() == '\'')) {
                            clean_line = trim(clean_line.substr(0, clean_line.size() - 1));
                        }
                        name = clean_line;
                    }

                    channel_count++;
                    if (name.empty()) {
                        name = "Canal " + std::to_string(channel_count);
                    }
                    if (pending_tvg.empty()) pending_tvg = name;

                    m3u_out << "#EXTINF:-1 group-title=\"" << (pending_group.empty() ? "Interna" : pending_group) << "\""
                            << " tvg-name=\"" << pending_tvg << "\""
                            << (pending_logo.empty() ? "" : " tvg-logo=\"" + pending_logo + "\"")
                            << ", " << name << "\n";
                    m3u_out << "acestream://" << hash << "\n";

                    reset_pending();
                } else if (starts_with(line, "http://") || starts_with(line, "https://") || starts_with(line, "infohash://")) {
                    std::string name = pending_name;
                    channel_count++;
                    if (name.empty()) {
                        name = "Canal " + std::to_string(channel_count);
                    }
                    if (pending_tvg.empty()) pending_tvg = name;

                    m3u_out << "#EXTINF:-1 group-title=\"" << (pending_group.empty() ? "Interna" : pending_group) << "\""
                            << " tvg-name=\"" << pending_tvg << "\""
                            << (pending_logo.empty() ? "" : " tvg-logo=\"" + pending_logo + "\"")
                            << ", " << name << "\n";
                    m3u_out << line << "\n";

                    reset_pending();
                } else {
                    if (!starts_with(line, "#")) {
                        pending_name = line;
                    }
                }
            }

            auto locales_dir = std::filesystem::path(config_.root_dir) / "http" / "listas" / "locales";
            std::filesystem::create_directories(locales_dir);
            auto file_path = locales_dir / "Interna.m3u";

            std::ofstream out(file_path, std::ios::binary);
            out << m3u_out.str();
            out.close();

            std::string relative_url = "/listas/locales/Interna.m3u";

            {
                std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                Json::object obj;
                if (plugins_state_json_.is_object()) {
                    obj = plugins_state_json_.as_object();
                }
                Json::array arr;
                if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
                    arr = obj["custom_lists"].as_array();
                }
                bool found = false;
                for (auto& item : arr) {
                    if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == "Interna") {
                        Json::object item_obj = item.as_object();
                        item_obj["url"] = relative_url;
                        item_obj["enabled"] = true;
                        item = item_obj;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    arr.push_back(Json::object{
                        {"name", "Interna"},
                        {"url", relative_url},
                        {"enabled", true}
                    });
                }
                obj["custom_lists"] = arr;
                plugins_state_json_ = obj;
            }
            save_plugins_state();
            add_custom_list_plugin("Interna", relative_url);

            Json res = Json::object{
                {"status", "success"},
                {"name", "Interna"},
                {"channels_count", channel_count},
                {"url", relative_url}
            };
            connection.send_response_headers(200, status_reason(200), headers);
            connection.send_text(res.dump(2));
            return;
        }
        else if (action == "refresh_plugin") {
            auto plugin_name = query_get(ctx.query, "plugin");
            if (plugin_name.empty()) plugin_name = query_get(ctx.query, "name");
            if (!plugin_name.empty()) {
                std::size_t channel_cnt = 0;
                std::size_t group_cnt = 0;
                bool success = false;

                auto lower_target = lower(plugin_name);
                for (auto& plugin : plugins_.unique_plugins()) {
                    if (lower(plugin->name()) == lower_target) {
                        if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
                            success = playlist->refresh_if_needed(true);
                            channel_cnt = playlist->channel_count();
                            std::set<std::string> groups;
                            for (const auto& item : playlist->playlist_items()) {
                                if (!item.group.empty()) groups.insert(item.group);
                            }
                            group_cnt = groups.size();
                        } else {
                            success = true;
                        }
                        break;
                    }
                }

                Json res = Json::object{
                    {"status", success ? "success" : "error"},
                    {"plugin", plugin_name},
                    {"channels_count", static_cast<double>(channel_cnt)},
                    {"groups_count", static_cast<double>(group_cnt)}
                };

                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text(res.dump(2));
            } else {
                send_error(connection, 400, "Missing plugin parameter");
            }
            return;
        }
        else if (action == "set_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            if (engine.empty()) engine = query_get(ctx.query, "mode");
            if (!engine.empty()) {
                set_engine(engine);
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\",\"engine\":\"" + engine_mode() + "\"}");
                return;
            }
        }
        else if (action == "set_url") {
            auto plugin = query_get(ctx.query, "plugin");
            auto url = query_get(ctx.query, "url");
            if (!plugin.empty() && !url.empty()) {
                set_plugin_url(plugin, url_decode(url));
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing plugin or url parameter");
            }
            return;
        }
        else if (action == "save_custom_list") {
            auto name = query_get(ctx.query, "name");
            auto title = query_get(ctx.query, "title");
            auto url = query_get(ctx.query, "url");
            auto enabled_str = query_get(ctx.query, "enabled");
            if (!name.empty() && !url.empty()) {
                bool enabled = (enabled_str != "false");
                if (title.empty()) title = name;
                {
                    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                    Json::object obj;
                    if (plugins_state_json_.is_object()) {
                        obj = plugins_state_json_.as_object();
                    }
                    Json::array arr;
                    if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
                        arr = obj["custom_lists"].as_array();
                    }
                    bool found = false;
                    for (auto& item : arr) {
                        if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                            Json::object item_obj = item.as_object();
                            item_obj["title"] = url_decode(title);
                            item_obj["url"] = url_decode(url);
                            item_obj["enabled"] = enabled;
                            item = item_obj;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        arr.push_back(Json::object{
                            {"name", name},
                            {"title", url_decode(title)},
                            {"url", url_decode(url)},
                            {"enabled", enabled}
                        });
                    }
                    obj["custom_lists"] = arr;
                    plugins_state_json_ = obj;
                }
                save_plugins_state();
                add_custom_list_plugin(name, url_decode(url));
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing name or url parameter");
            }
            return;
        }
        else if (action == "delete_custom_list") {
            auto name = query_get(ctx.query, "name");
            if (!name.empty()) {
                {
                    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                    Json::object obj;
                    if (plugins_state_json_.is_object()) {
                        obj = plugins_state_json_.as_object();
                    }
                    if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
                        Json::array arr = obj["custom_lists"].as_array();
                        Json::array new_arr;
                        for (const auto& item : arr) {
                            if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                                continue;
                            }
                            new_arr.push_back(item);
                        }
                        obj["custom_lists"] = new_arr;
                        plugins_state_json_ = obj;
                    }
                }
                save_plugins_state();
                remove_custom_list_plugin(name);
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing name parameter");
            }
            return;
        }
        else if (action == "export_sources") {
            auto fmt = query_get(ctx.query, "format");
            if (fmt.empty()) fmt = "m3u";

            Json::object config_obj;
            {
                std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                if (plugins_state_json_.is_object()) {
                    config_obj = plugins_state_json_.as_object();
                }
            }

            Json::object urls_obj;
            if (config_obj.contains("urls") && config_obj["urls"].is_object()) {
                urls_obj = config_obj["urls"].as_object();
            }

            Json::array custom_arr;
            if (config_obj.contains("custom_lists") && config_obj["custom_lists"].is_array()) {
                custom_arr = config_obj["custom_lists"].as_array();
            }

            if (fmt == "json") {
                Json export_json = Json::object{
                    {"version", kAppVersion},
                    {"urls", urls_obj},
                    {"custom_lists", custom_arr}
                };
                auto body_str = export_json.dump(2);
                headers["Content-Type"] = "application/json; charset=utf-8";
                headers["Content-Disposition"] = "attachment; filename=\"fuentes_httpaceproxy.json\"";
                headers["Content-Length"] = std::to_string(body_str.size());
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text(body_str);
                connection.close();
                return;
            } else {
                std::ostringstream out;
                out << "#EXTM3U name=\"Fuentes HTTPAceProxy\" url-tvg=\"https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz\"\n";

                struct Predef { std::string id; std::string name; std::string fallback; };
                std::vector<Predef> predefined = {
                    {"newera", "NewEra", "https://ipfs.io/ipns/k2k4r8lm8tkmuxbc8lkmq1in3v0oya1p6pe9o5bu0hu30br5ko08k2gb/data/listas/lista_iptv.m3u"},
                    {"elcano", "Elcano.top by @Lucas_m_o_o_m ... Vacaciones en el Mar", "https://k51qzi5uqu5dh5qej4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik820fecs.ipns.inbrowser.link/hashes.json"},
                    {"af1c1onados", "Af1c1onados", "https://raw.githubusercontent.com/af1Series1/Tritolgia/refs/heads/main/AcEStREAM%20iDs.w3u"},
                    {"acepl", "Acepl", "https://api.acestream.me/all?api_version=1.0&api_key=test_api_key"}
                };

                for (const auto& p : predefined) {
                    std::string u = p.fallback;
                    if (urls_obj.contains(p.id) && urls_obj.at(p.id).is_string() && !urls_obj.at(p.id).as_string().empty()) {
                        u = urls_obj.at(p.id).as_string();
                    }
                    bool enabled = true;
                    if (config_obj.contains(p.id) && config_obj.at(p.id).is_bool()) {
                        enabled = config_obj.at(p.id).as_bool();
                    }
                    out << "#EXTINF:-1 tvg-id=\"" << p.id << "\" tvg-name=\"" << p.name << "\" group-title=\"Fuentes Predefinidas\" status=\"" << (enabled ? "enabled" : "disabled") << "\", " << p.name << "\n";
                    out << u << "\n";
                }

                for (const auto& item : custom_arr) {
                    if (!item.is_object()) continue;
                    auto c_obj = item.as_object();
                    std::string c_name = c_obj.contains("name") ? c_obj.at("name").as_string() : "";
                    std::string c_title = c_obj.contains("title") ? c_obj.at("title").as_string() : c_name;
                    std::string c_url = c_obj.contains("url") ? c_obj.at("url").as_string() : "";
                    bool enabled = c_obj.contains("enabled") ? c_obj.at("enabled").as_bool() : true;

                    if (c_name.empty() || c_url.empty()) continue;

                    out << "#EXTINF:-1 tvg-id=\"" << c_name << "\" tvg-name=\"" << c_title << "\" group-title=\"Fuentes Personalizadas\" status=\"" << (enabled ? "enabled" : "disabled") << "\", " << c_title << "\n";
                    out << c_url << "\n";
                }

                auto body_str = out.str();
                headers["Content-Type"] = "audio/mpegurl; charset=utf-8";
                headers["Content-Disposition"] = "attachment; filename=\"fuentes_httpaceproxy.m3u\"";
                headers["Content-Length"] = std::to_string(body_str.size());
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text(body_str);
                connection.close();
                return;
            }
        }
        else if (action == "import_sources") {
            std::string content = request.body;
            if (content.empty()) {
                content = url_decode(query_get(ctx.query, "content"));
            }
            std::string mode = query_get(ctx.query, "mode");
            if (mode.empty()) mode = "merge";

            if (content.empty()) {
                send_error(connection, 400, "Empty payload for import");
                return;
            }

            int imported_count = 0;
            std::set<std::string> known_predefined = {"newera", "elcano", "af1c1onados", "acepl"};

            std::lock_guard<std::mutex> lock(plugins_state_mutex_);
            Json::object obj;
            if (plugins_state_json_.is_object()) {
                obj = plugins_state_json_.as_object();
            }

            Json::object urls_obj;
            if (obj.contains("urls") && obj["urls"].is_object()) {
                urls_obj = obj["urls"].as_object();
            }

            Json::array custom_arr;
            if (mode == "merge" && obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
                custom_arr = obj["custom_lists"].as_array();
            }

            std::string trimmed_content = trim(content);
            if (starts_with(trimmed_content, "{")) {
                try {
                    auto j = Json::parse(trimmed_content);
                    if (j.is_object()) {
                        if (j.contains("urls") && j["urls"].is_object()) {
                            auto imp_urls = j["urls"].as_object();
                            for (const auto& [k, v] : imp_urls) {
                                if (v.is_string()) {
                                    urls_obj[k] = v.as_string();
                                    imported_count++;
                                    set_plugin_url(k, v.as_string());
                                }
                            }
                        }
                        if (j.contains("custom_lists") && j["custom_lists"].is_array()) {
                            auto imp_custom = j["custom_lists"].as_array();
                            for (const auto& item : imp_custom) {
                                if (!item.is_object()) continue;
                                auto c_obj = item.as_object();
                                std::string c_name = c_obj.contains("name") ? c_obj.at("name").as_string() : "";
                                std::string c_title = c_obj.contains("title") ? c_obj.at("title").as_string() : c_name;
                                std::string c_url = c_obj.contains("url") ? c_obj.at("url").as_string() : "";
                                bool c_enabled = c_obj.contains("enabled") ? c_obj.at("enabled").as_bool() : true;

                                if (c_name.empty() || c_url.empty()) continue;

                                bool found = false;
                                for (auto& existing : custom_arr) {
                                    if (existing.is_object() && existing.as_object().contains("name") && existing.as_object().at("name").as_string() == c_name) {
                                        Json::object ex_obj = existing.as_object();
                                        ex_obj["title"] = c_title;
                                        ex_obj["url"] = c_url;
                                        ex_obj["enabled"] = c_enabled;
                                        existing = ex_obj;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    custom_arr.push_back(Json::object{
                                        {"name", c_name},
                                        {"title", c_title},
                                        {"url", c_url},
                                        {"enabled", c_enabled}
                                    });
                                }
                                imported_count++;
                                add_custom_list_plugin(c_name, c_url);
                            }
                        }
                    }
                } catch (...) {
                    send_error(connection, 400, "Error de parseo JSON al importar fuentes");
                    return;
                }
            } else {
                std::istringstream stream(trimmed_content);
                std::string line;
                std::string pending_id;
                std::string pending_title;
                bool pending_enabled = true;

                while (std::getline(stream, line)) {
                    line = trim(line);
                    if (line.empty() || line == "#EXTM3U") continue;
                    if (starts_with(line, "#EXTVLCOPT:") || starts_with(line, "#EXTHTTP:") || starts_with(line, "#EXT-X-")) continue;

                    if (starts_with(line, "#EXTINF:")) {
                        auto attrs = parse_extinf_attrs(line);
                        if (attrs.contains("tvg-id") && !attrs["tvg-id"].empty()) {
                            pending_id = attrs["tvg-id"];
                        }
                        if (attrs.contains("tvg-name") && !attrs["tvg-name"].empty()) {
                            pending_title = attrs["tvg-name"];
                        }
                        if (attrs.contains("status")) {
                            pending_enabled = (attrs["status"] != "disabled");
                        }
                        auto parsed_name = parse_extinf_name(line);
                        if (pending_title.empty() && !parsed_name.empty() && parsed_name != "Unknown Channel") {
                            pending_title = parsed_name;
                        }
                        continue;
                    }

                    if (!starts_with(line, "#")) {
                        std::string url = line;
                        if (pending_title.empty()) {
                            pending_title = "Fuente " + std::to_string(imported_count + 1);
                        }
                        if (pending_id.empty()) {
                            pending_id = pending_title;
                            std::string clean_id;
                            for (char ch : pending_id) {
                                char l = std::tolower(static_cast<unsigned char>(ch));
                                if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_') clean_id += l;
                                else if (l == ' ' || l == '-') clean_id += '_';
                            }
                            if (clean_id.empty()) clean_id = "fuente_" + std::to_string(imported_count + 1);
                            pending_id = clean_id;
                        }

                        if (known_predefined.count(pending_id) > 0) {
                            urls_obj[pending_id] = url;
                            set_plugin_url(pending_id, url);
                            imported_count++;
                        } else {
                            bool found = false;
                            for (auto& existing : custom_arr) {
                                if (existing.is_object() && existing.as_object().contains("name") && existing.as_object().at("name").as_string() == pending_id) {
                                    Json::object ex_obj = existing.as_object();
                                    ex_obj["title"] = pending_title;
                                    ex_obj["url"] = url;
                                    ex_obj["enabled"] = pending_enabled;
                                    existing = ex_obj;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                custom_arr.push_back(Json::object{
                                    {"name", pending_id},
                                    {"title", pending_title},
                                    {"url", url},
                                    {"enabled", pending_enabled}
                                });
                            }
                            imported_count++;
                            add_custom_list_plugin(pending_id, url);
                        }

                        pending_id.clear();
                        pending_title.clear();
                        pending_enabled = true;
                    }
                }
            }

            obj["urls"] = urls_obj;
            obj["custom_lists"] = custom_arr;
            plugins_state_json_ = obj;
            save_plugins_state();

            Json res = Json::object{
                {"status", "success"},
                {"message", "Fuentes importadas con éxito"},
                {"imported_count", imported_count}
            };
            auto body_str = res.dump(2);
            headers["Content-Type"] = "application/json; charset=utf-8";
            headers["Content-Length"] = std::to_string(body_str.size());
            connection.send_response_headers(200, status_reason(200), headers);
            connection.send_text(body_str);
            connection.close();
            return;
        }
        else if (action == "check_list") {
            auto target_url = query_get(ctx.query, "url");
            auto plugin_name = query_get(ctx.query, "plugin");
            if (target_url.empty() && !plugin_name.empty()) {
                target_url = get_plugin_url(plugin_name, "");
            }
            if (!target_url.empty()) {
                std::string content;
                int http_status = 200;
                auto decoded_url = url_decode(target_url);

                bool is_local = starts_with(decoded_url, "/") || starts_with(decoded_url, "file://") || decoded_url.find("/listas/locales/") != std::string::npos;
                if (is_local) {
                    std::filesystem::path local_path;
                    if (starts_with(decoded_url, "file://")) {
                        local_path = decoded_url.substr(7);
                    } else if (starts_with(decoded_url, "/")) {
                        local_path = std::filesystem::path(config_.root_dir) / "http" / decoded_url.substr(1);
                    } else {
                        auto pos = decoded_url.find("/listas/locales/");
                        local_path = std::filesystem::path(config_.root_dir) / "http" / decoded_url.substr(pos + 1);
                    }
                    if (std::filesystem::exists(local_path)) {
                        std::ifstream file_stream(local_path, std::ios::binary);
                        if (file_stream.is_open()) {
                            std::stringstream ss;
                            ss << file_stream.rdbuf();
                            content = ss.str();
                        } else {
                            content = read_file_binary(local_path.string());
                        }
                    } else {
                        if (!plugin_name.empty()) {
                            auto lower_target = lower(plugin_name);
                            for (auto& plugin : plugins_.unique_plugins()) {
                                if (lower(plugin->name()) == lower_target) {
                                    if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
                                        int c_count = playlist->channel_count();
                                        std::set<std::string> grps;
                                        for (const auto& item : playlist->playlist_items()) {
                                            if (!item.group.empty()) grps.insert(item.group);
                                        }
                                        Json out = Json::object{
                                            {"status", "success"},
                                            {"http_status", 200},
                                            {"channels_count", static_cast<double>(c_count)},
                                            {"groups_count", static_cast<double>(grps.size())},
                                            {"size_bytes", 0}
                                        };
                                        connection.send_response_headers(200, status_reason(200), headers);
                                        connection.send_text(out.dump(2));
                                        return;
                                    }
                                }
                            }
                        }
                        Json out = Json::object{
                            {"status", "error"},
                            {"error", "Archivo local no encontrado"}
                        };
                        connection.send_response_headers(200, status_reason(200), headers);
                        connection.send_text(out.dump(2));
                        return;
                    }
                } else {
                    auto normalized_url = normalize_list_url(decoded_url);
                    normalized_url = replace_all(normalized_url, " ", "%20");
                    try {
                        auto res = http_client_.get(normalized_url, {{"User-Agent", kBrowserUserAgent}}, 2, true);
                        http_status = res.status;
                        if (res.status >= 200 && res.status < 400) {
                            content = res.body;
                        } else {
                            Json out = Json::object{
                                {"status", "error"},
                                {"http_status", static_cast<double>(res.status)},
                                {"error", "HTTP " + std::to_string(res.status)}
                            };
                            connection.send_response_headers(200, status_reason(200), headers);
                            connection.send_text(out.dump(2));
                            return;
                        }
                    } catch (const std::exception& e) {
                        Json out = Json::object{
                            {"status", "error"},
                            {"error", "timeout"},
                            {"http_status", 504},
                            {"channels_count", 0},
                            {"groups_count", 0}
                        };
                        connection.send_response_headers(200, status_reason(200), headers);
                        connection.send_text(out.dump(2));
                        return;
                    }
                }

                int channel_count = 0;
                std::set<std::string> groups;
                std::string trimmed_content = trim(content);

                if (starts_with(trimmed_content, "{") || starts_with(trimmed_content, "[")) {
                    try {
                        auto j = Json::parse(trimmed_content);
                        std::function<void(const Json&, const std::string&)> count_json = [&](const Json& node, const std::string& current_grp) {
                            if (node.is_object()) {
                                std::string grp = node.contains("name") ? node["name"].as_string() : current_grp;
                                if (node.contains("stations") && node["stations"].is_array()) {
                                    for (const auto& st : node["stations"].as_array()) {
                                        if (st.is_object() && st.contains("name")) {
                                            channel_count++;
                                            if (!grp.empty()) groups.insert(grp);
                                        }
                                    }
                                }
                                if (node.contains("groups") && node["groups"].is_array()) {
                                    for (const auto& g : node["groups"].as_array()) {
                                        if (g.is_object()) {
                                            if (g.contains("name") && !g["name"].as_string().empty()) {
                                                groups.insert(g["name"].as_string());
                                            }
                                            if (g.contains("url") && !g.contains("stations")) {
                                                channel_count++;
                                            }
                                        }
                                        count_json(g, grp);
                                    }
                                }
                            }
                        };
                        count_json(j, "");
                    } catch (...) {}
                }

                if (channel_count == 0) {
                    std::istringstream stream(content);
                    std::string line;
                    while (std::getline(stream, line)) {
                        line = trim(line);
                        if (starts_with(line, "#EXTINF:")) {
                            channel_count++;
                            auto attrs = parse_extinf_attrs(line);
                            if (attrs.contains("group-title") && !attrs["group-title"].empty()) {
                                groups.insert(attrs["group-title"]);
                            }
                        } else if (starts_with(line, "#EXTGRP:")) {
                            auto grp = trim(line.substr(8));
                            if (!grp.empty()) groups.insert(grp);
                        } else if (!line.empty() && !starts_with(line, "#")) {
                            if (starts_with(line, "http://") || starts_with(line, "https://") || starts_with(line, "acestream://") || std::regex_search(line, std::regex(R"([a-fA-F0-9]{40})"))) {
                                channel_count++;
                            }
                        }
                    }
                }

                if (channel_count == 0 && !plugin_name.empty()) {
                    auto lower_target = lower(plugin_name);
                    for (auto& plugin : plugins_.unique_plugins()) {
                        if (lower(plugin->name()) == lower_target) {
                            if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
                                channel_count = playlist->channel_count();
                                for (const auto& item : playlist->playlist_items()) {
                                    if (!item.group.empty()) groups.insert(item.group);
                                }
                            }
                            break;
                        }
                    }
                }

                Json out = Json::object{
                    {"status", "success"},
                    {"http_status", static_cast<double>(http_status)},
                    {"channels_count", static_cast<double>(channel_count)},
                    {"groups_count", static_cast<double>(groups.size())},
                    {"size_bytes", static_cast<double>(content.size())}
                };
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text(out.dump(2));
                return;
            } else {
                send_error(connection, 400, "Missing url parameter");
            }
            return;
        }
        else if (action == "check_epg") {
            auto url = query_get(ctx.query, "url");
            auto res = check_epg_url(url_decode(url));
            connection.send_response_headers(200, status_reason(200), headers);
            connection.send_text(res.dump(2));
            return;
        }
        else if (action == "save_epg_source") {
            auto name = query_get(ctx.query, "name");
            auto url = query_get(ctx.query, "url");
            auto title = query_get(ctx.query, "title");
            auto enabled_str = query_get(ctx.query, "enabled");
            if (!name.empty() && !url.empty()) {
                bool enabled = (enabled_str != "false");
                if (title.empty()) title = name;
                {
                    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                    Json::object obj;
                    if (plugins_state_json_.is_object()) {
                        obj = plugins_state_json_.as_object();
                    }
                    Json::array arr;
                    if (obj.contains("epg_sources") && obj["epg_sources"].is_array()) {
                        arr = obj["epg_sources"].as_array();
                    }
                    bool found = false;
                    for (auto& item : arr) {
                        if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                            Json::object item_obj = item.as_object();
                            item_obj["url"] = url_decode(url);
                            item_obj["title"] = url_decode(title);
                            item_obj["enabled"] = enabled;
                            item = item_obj;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        arr.push_back(Json::object{
                            {"name", name},
                            {"title", url_decode(title)},
                            {"url", url_decode(url)},
                            {"enabled", enabled}
                        });
                    }
                    obj["epg_sources"] = arr;

                    if (name == "epg_default" || arr.size() == 1) {
                        Json::object urls_obj;
                        if (obj.contains("urls") && obj["urls"].is_object()) {
                            urls_obj = obj["urls"].as_object();
                        }
                        urls_obj["epg"] = url_decode(url);
                        obj["urls"] = urls_obj;
                    }

                    plugins_state_json_ = obj;
                }
                save_plugins_state();
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing name or url parameter");
            }
            return;
        }
        else if (action == "delete_epg_source") {
            auto name = query_get(ctx.query, "name");
            if (!name.empty()) {
                {
                    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                    Json::object obj;
                    if (plugins_state_json_.is_object()) {
                        obj = plugins_state_json_.as_object();
                    }
                    if (obj.contains("epg_sources") && obj["epg_sources"].is_array()) {
                        Json::array arr = obj["epg_sources"].as_array();
                        Json::array new_arr;
                        for (const auto& item : arr) {
                            if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                                continue;
                            }
                            new_arr.push_back(item);
                        }
                        obj["epg_sources"] = new_arr;
                        plugins_state_json_ = obj;
                    }
                }
                save_plugins_state();
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing name parameter");
            }
            return;
        }
        else if (action == "toggle_epg_source") {
            auto name = query_get(ctx.query, "name");
            auto status_str = query_get(ctx.query, "status");
            if (!name.empty() && !status_str.empty()) {
                bool status = (status_str == "true");
                {
                    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
                    Json::object obj;
                    if (plugins_state_json_.is_object()) {
                        obj = plugins_state_json_.as_object();
                    }
                    if (obj.contains("epg_sources") && obj["epg_sources"].is_array()) {
                        Json::array arr = obj["epg_sources"].as_array();
                        for (auto& item : arr) {
                            if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                                Json::object item_obj = item.as_object();
                                item_obj["enabled"] = status;
                                item = item_obj;
                                break;
                            }
                        }
                        obj["epg_sources"] = arr;
                        plugins_state_json_ = obj;
                    }
                }
                save_plugins_state();
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing name or status parameter");
            }
            return;
        }
        else {
            auto plugin = query_get(ctx.query, "plugin");
            auto status_str = query_get(ctx.query, "status");
            if (!plugin.empty() && !status_str.empty()) {
                bool status = (status_str == "true");
                set_plugin_enabled(plugin, status);
                connection.send_response_headers(200, status_reason(200), headers);
                connection.send_text("{\"status\":\"success\"}");
            } else {
                send_error(connection, 400, "Missing plugin, status or action parameter");
            }
            return;
        }
    }

    for (int redirects = 0; redirects < 4; ++redirects) {
        ctx.rewritten = false;
        ctx.parts = split(ctx.path, '/', true);
        ctx.reqtype = ctx.parts.size() > 1 ? lower(ctx.parts[1]) : "";
        if (auto plugin = plugins_.by_handler(ctx.reqtype)) {
            bool handled = plugin->handle(ctx);
            if (handled) return;
            if (ctx.rewritten) continue;
        }
        ctx.reqtype = map_reqtype_alias(ctx.reqtype);
        if (is_core_reqtype(ctx.reqtype)) {
            handle_core_stream(ctx);
            return;
        }

        // Fallback for static subdirectories under http/
        if (!ctx.reqtype.empty()) {
            auto target_path = std::filesystem::path(config_.root_dir) / "http" / ctx.reqtype;
            if (std::filesystem::exists(target_path)) {
                std::string rel_file = "index.html";
                if (ctx.parts.size() > 2) {
                    std::vector<std::string> sub_parts(ctx.parts.begin() + 2, ctx.parts.end());
                    rel_file = join(sub_parts, "/");
                }
                if (rel_file.empty()) rel_file = "index.html";
                handle_static(request, connection, ctx.reqtype + "/" + rel_file);
                return;
            }
        }

        send_error(connection, 400, "Bad Request");
        return;
    }
    send_error(connection, 500, "Too many internal rewrites");
}

bool Proxy::is_fake_request(const HttpRequest& request) const {
    auto ua = request.header("user-agent");
    if (ua.empty()) return false;
    if (std::find(config_.fake_user_agents.begin(), config_.fake_user_agents.end(), ua) != config_.fake_user_agents.end()) return true;
    if (ua == "Lavf/55.33.100" && request.header("range").empty()) return true;
    if (ua == "GStreamer souphttpsrc (compatible; LG NetCast.TV-2013) libsoup/2.34.2" && request.header("icy-metadata") != "1") return true;
    return false;
}

bool Proxy::check_firewall(const std::string& client_ip) const {
    bool in_range = false;
    for (const auto& range : config_.firewall_ranges) {
        if (client_ip == range || (ends_with(range, "/16") && starts_with(client_ip, range.substr(0, range.find_last_of('.')))) ||
            (ends_with(range, "/8") && starts_with(client_ip, range.substr(0, range.find('.'))))) {
            in_range = true;
            break;
        }
    }
    return !((config_.firewall_blacklist_mode && in_range) || (!config_.firewall_blacklist_mode && !in_range));
}

void Proxy::handle_static(const HttpRequest&, ClientConnection& connection, const std::string& root_prefix) {
    try {
        if (!path_is_safe_relative(root_prefix)) throw std::runtime_error("unsafe path");
        auto full = std::filesystem::path(config_.root_dir) / "http" / root_prefix;
        auto body = read_file_binary(full.string());
        send_simple_response(connection, 200, mime_type_for_path(root_prefix), body);
    } catch (...) {
        send_error(connection, 404, "Not Found");
    }
}

void Proxy::handle_core_stream(RequestContext& ctx) {
    if (ctx.parts.size() < 3) {
        send_error(ctx.connection, 400, "Bad Request");
        return;
    }
    if (config_.max_connections > 0 && static_cast<int>(broadcasts_.client_count()) >= config_.max_connections) {
        send_error(ctx.connection, 403, "Maximum client connections reached, can't serve request");
        return;
    }
    if (!is_video_extension(ctx.path)) {
        send_error(ctx.connection, 501, "request seems valid but no valid video extension was provided");
        return;
    }

    std::map<std::string, std::string> params = {
        {"file_indexes", "0"}, {"developer_id", "0"}, {"affiliate_id", "0"}, {"zone_id", "0"}, {"stream_id", "0"},
        {"stream_type", config_.stream_type_string()},
        {"sessionID", std::to_string(reinterpret_cast<std::uintptr_t>(&ctx))},
    };
    static const std::vector<std::string> start_params = {"file_indexes", "developer_id", "affiliate_id", "zone_id", "stream_id"};
    for (std::size_t i = 3; i < ctx.parts.size() && i - 3 < start_params.size(); ++i) {
        if (!ctx.parts[i].empty() && std::all_of(ctx.parts[i].begin(), ctx.parts[i].end(), ::isdigit)) {
            params[start_params[i - 3]] = ctx.parts[i];
        }
    }
    auto req_value = url_decode(ctx.parts[2]);
    params[ctx.reqtype] = req_value;

    std::string infohash;
    std::string channel_name = ctx.channel_name.empty() ? url_decode(basename_no_ext(ctx.path)) : ctx.channel_name;
    if (channel_name.empty()) channel_name = "NoNameChannel";

    std::string raw_host = ctx.request.header("host", "127.0.0.1:8888");
    std::string ip_nodo = raw_host;
    auto colon_pos = raw_host.find(':');
    if (colon_pos != std::string::npos) {
        ip_nodo = raw_host.substr(0, colon_pos);
    }
    if (ip_nodo.empty() || ip_nodo == "0.0.0.0") ip_nodo = "127.0.0.1";

    std::string stream_link = "http://" + ip_nodo + ":8888/content_id/" + req_value + "/stream.ts";

    try {
        if (ctx.reqtype == "direct_url" || ctx.reqtype == "efile_url") {
            infohash = sha1_hex(ctx.path);
        } else {
            try {
                auto info = get_content_info(params);
                infohash = info["infohash"].as_string();
                if (channel_name == "NoNameChannel" && info["files"].is_array()) {
                    int wanted = 0;
                    try { wanted = std::stoi(params["file_indexes"]); } catch (...) {}
                    for (const auto& file : info["files"].as_array()) {
                        if (file.is_array() && file.as_array().size() >= 2 &&
                            static_cast<int>(file[1].as_number(-1)) == wanted) {
                            channel_name = file[0].as_string(channel_name);
                            break;
                        }
                    }
                }
            } catch (const std::exception& e) {
                log_line("WARNING", "get_content_info failed, proceeding with direct stream start: " + std::string(e.what()));
            }
        }
        if (infohash.empty()) infohash = sha1_hex(ctx.reqtype + ":" + req_value);

        if (config_.max_concurrent_channels > 0 && broadcasts_.broadcast_count() >= static_cast<std::size_t>(config_.max_concurrent_channels) &&
            !broadcasts_.find(infohash)) {
            send_error(ctx.connection, 503, "Maximum concurrent channels reached, try again later");
            return;
        }

        // TELEMETRÍA: AL INICIAR STREAM
        add_bunker_log("[REPRODUCTOR] Iniciando streaming -> ID: " + req_value + " | Enlace: " + stream_link);

        std::string user_agent = ctx.request.header("user-agent");
        std::string referer = ctx.request.header("referer");
        std::string full_stream_url = "http://" + (raw_host.empty() ? "127.0.0.1:8888" : raw_host) + "/content_id/" + req_value + "/stream.ts";

        std::string epg_title = "";
        std::string epg_icon = ctx.channel_icon;

        for (const auto& plugin : plugins_.unique_plugins()) {
            for (const auto& item : plugin->playlist_items()) {
                if (!item.url.empty() && (item.url.find(req_value) != std::string::npos || lower(item.name) == lower(channel_name))) {
                    if (channel_name == "NoNameChannel" && !item.name.empty()) {
                        channel_name = item.name;
                    }
                    if (epg_icon.empty() && !item.logo.empty()) {
                        epg_icon = item.logo;
                    }
                    if (epg_title.empty()) {
                        if (!item.tvg.empty()) epg_title = item.tvg;
                        else if (!item.group.empty()) epg_title = item.group;
                    }
                    break;
                }
            }
            if (!epg_title.empty()) break;
        }

        auto broadcast = broadcasts_.get_or_create(infohash, params);
        auto client = broadcast->add_client(
            ctx.request.header("x-forwarded-for", ctx.request.client_ip),
            channel_name,
            epg_icon,
            user_agent,
            referer,
            full_stream_url,
            epg_title,
            epg_icon
        );
        broadcast->start_once();

        // 1. ESPERAR EL PRIMER CHUNK REAL DE DATOS (TIMEOUT 30s)
        std::vector<char> first_chunk;
        if (!client->queue->pop_timeout(first_chunk, std::chrono::seconds(30)) || first_chunk.empty()) {
            broadcast->remove_client(client);
            broadcasts_.remove_if_empty(infohash);
            add_bunker_log("[ERROR REPRODUCTOR] Canal offline / Cannot retrieve torrent para ID: " + req_value);
            std::map<std::string, std::string> err_headers = {
                {"Content-Type", "application/json; charset=utf-8"},
                {"Connection", "close"}
            };
            ctx.connection.send_response_headers(502, status_reason(502), err_headers);
            ctx.connection.send_text("{\"error\":\"AceEngine: Cannot retrieve torrent or hash offline\",\"status\":502}");
            return;
        }

        // 2. ENVIAR CABECERAS HTTP 200 OK TRAS CONFIRMAR DATOS REALES
        bool chunked = config_.use_chunked && ctx.request.version == "HTTP/1.1";
        std::map<std::string, std::string> headers = {
            {"Content-Type", mime_type_for_path(ctx.path)},
            {"Accept-Ranges", "none"},
            {"Connection", chunked ? "keep-alive" : "close"}
        };
        if (chunked) {
            headers["Transfer-Encoding"] = "chunked";
            headers["Keep-Alive"] = "timeout=" + std::to_string(config_.video_timeout) + ", max=100";
        }
        ctx.connection.send_response_headers(200, status_reason(200), headers);

        // 3. ENVIAR EL PRIMER CHUNK PRECARGADO
        bool ok = true;
        if (chunked) {
            std::ostringstream prefix;
            prefix << std::hex << first_chunk.size() << "\r\n";
            ok = ctx.connection.send_text(prefix.str())
              && ctx.connection.send_all(first_chunk.data(), first_chunk.size())
              && ctx.connection.send_text("\r\n");
        } else {
            ok = ctx.connection.send_all(first_chunk.data(), first_chunk.size());
        }
        client->last_activity.store(unix_time(), std::memory_order_relaxed);

        // 4. CONTINUAR STREAMING HABITUAL CON LOS SIGUIENTES CHUNKS Y MARGEN DE GRACIA DE 45s
        if (ok) {
            std::vector<char> chunk;
            auto empty_start = std::chrono::steady_clock::now();
            bool empty_timer_running = false;

            while (true) {
                if (client->queue->pop_timeout(chunk, std::chrono::milliseconds(50))) {
                    empty_timer_running = false;
                    if (chunked) {
                        std::ostringstream prefix;
                        prefix << std::hex << chunk.size() << "\r\n";
                        ok = ctx.connection.send_text(prefix.str())
                          && ctx.connection.send_all(chunk.data(), chunk.size())
                          && ctx.connection.send_text("\r\n");
                    } else {
                        ok = ctx.connection.send_all(chunk.data(), chunk.size());
                    }
                    if (!ok) break;
                    client->last_activity.store(unix_time(), std::memory_order_relaxed);
                } else {
                    // La cola por cliente está vacía temporalmente.
                    if (!empty_timer_running) {
                        empty_timer_running = true;
                        empty_start = std::chrono::steady_clock::now();
                    }

                    // Verificar si AceClient sigue en estado activo/descargando
                    auto ace = client->ace.lock();
                    bool ace_active = false;
                    if (ace) {
                        auto st_map = ace->status(1);
                        std::string st = st_map.contains("status") ? lower(st_map["status"]) : "";
                        if (st == "dl" || st == "main:dl" || st == "buf" || st == "prebuf" || st == "wait" || st == "check" || ace->alive()) {
                            ace_active = true;
                        }
                    }

                    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - empty_start).count();

                    if (ace_active && elapsed_sec < 45) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    } else {
                        // Se superaron 45 segundos consecutivos con la cola vacía o AceEngine no está activo
                        break;
                    }
                }
            }
        }
        if (chunked) ctx.connection.send_text("0\r\n\r\n");
        broadcast->remove_client(client);
        broadcasts_.remove_if_empty(infohash);

        // TELEMETRÍA: AL DETENER STREAM
        add_bunker_log("[REPRODUCTOR] Streaming detenido -> ID: " + req_value);
    } catch (const std::exception& e) {
        // TELEMETRÍA: SI OCURRE UN ERROR
        add_bunker_log("[ERROR REPRODUCTOR] Fallo al conectar con el motor " + selected_engine() + ": " + e.what());
        send_error(ctx.connection, 500, e.what());
    }
}

Json Proxy::get_content_info(const std::map<std::string, std::string>& params) {
    std::lock_guard<std::mutex> lock(idle_mutex_);
    try {
        if (!idle_ace_ || !idle_ace_->alive()) {
            idle_ace_ = std::make_unique<AceClient>(config_, "idleAce");
            idle_ace_->authenticate();
        }
        return idle_ace_->content_info(params);
    } catch (const std::exception& e) {
        idle_ace_.reset();
        add_bunker_log("[ERROR REPRODUCTOR] Fallo al conectar con el motor " + selected_engine() + ": " + e.what());
        throw;
    }
}

void Proxy::send_error(ClientConnection& connection, int status, const std::string& message) {
    log_line(status >= 500 ? "ERROR" : "WARNING", message);
    send_simple_response(connection, status, "text/plain", message);
}

Json Proxy::status_json() {
    Json::array clients;
    auto all_clients_list = broadcasts_.all_clients();

    // Obtener estado P2P en memoria (snapshot lock-free / non-blocking) por cada AceClient
    std::map<AceClient*, std::map<std::string, std::string>> ace_status_map;
    for (const auto& client : all_clients_list) {
        if (!client) continue;
        auto ace = client->ace.lock();
        if (ace && !ace_status_map.contains(ace.get())) {
            auto st = ace->get_cached_status();
            if (!st.contains("status") || st["status"].empty() || st["status"] == "error" || st["status"] == "idle") {
                st["status"] = "dl";
            }
            ace_status_map[ace.get()] = st;
        }
    }

    for (const auto& client : all_clients_list) {
        if (!client) continue;
        auto ace = client->ace.lock();
        Json stat = Json::object{};
        int dl_speed_kbps = 0;
        int ul_speed_kbps = 0;
        int peers = 0;
        std::string status_str = "DL";

        if (ace && ace_status_map.contains(ace.get())) {
            const auto& status_data = ace_status_map[ace.get()];
            Json::object stat_obj;
            for (const auto& [k, v] : status_data) stat_obj[k] = v;
            stat = stat_obj;

            if (status_data.contains("speed_down")) {
                try { dl_speed_kbps = std::stoi(status_data.at("speed_down")); } catch (...) {}
            }
            if (status_data.contains("speed_up")) {
                try { ul_speed_kbps = std::stoi(status_data.at("speed_up")); } catch (...) {}
            }
            if (status_data.contains("peers")) {
                try { peers = std::stoi(status_data.at("peers")); } catch (...) {}
            }
            if (status_data.contains("status") && !status_data.at("status").empty() && status_data.at("status") != "error") {
                status_str = upper(status_data.at("status"));
            }
        }

        std::int64_t now_ts = unix_time();
        std::int64_t duration_sec = std::max<std::int64_t>(0, now_ts - client->connection_time);
        std::string start_time_formatted = format_local_time(client->connection_time);
        std::string duration_formatted = format_duration(std::chrono::seconds(duration_sec));

        std::string logo_url = client->channel_icon;
        if (logo_url.empty()) {
            logo_url = "http://static.acestream.net/sites/acestream/img/ACE-logo.png";
        }
        std::string epg_title_val = client->epg_title;
        if (epg_title_val.empty()) {
            epg_title_val = client->channel_name;
        }

        clients.push_back(Json::object{
            {"channel_name", client->channel_name},
            {"epg_title", epg_title_val},
            {"logo", logo_url},
            {"epg_icon", logo_url},
            {"client_ip", client->client_ip},
            {"client_type", client->client_type.empty() ? "Web Player" : client->client_type},
            {"start_time_formatted", start_time_formatted},
            {"duration", duration_formatted},
            {"dl_speed_kbps", static_cast<double>(dl_speed_kbps)},
            {"ul_speed_kbps", static_cast<double>(ul_speed_kbps)},
            {"peers", static_cast<double>(peers)},
            {"status", status_str},
            {"stream_url", client->stream_url},
            // Legacy / Backwards compatibility
            {"sessionID", client->session_id},
            {"channelIcon", logo_url},
            {"channelName", client->channel_name},
            {"clientIP", client->client_ip},
            {"startTime", start_time_formatted},
            {"durationTime", duration_formatted},
            {"stat", stat}
        });
    }
    Json::array loaded;
    for (const auto& plugin : plugins_.unique_plugins()) {
        Json::array handlers;
        for (const auto& handler : plugin->handlers()) handlers.push_back(handler);
        std::string p_url = get_plugin_url(plugin->name(), "");
        loaded.push_back(Json::object{
            {"name", plugin->name()},
            {"channels", static_cast<double>(plugin->channel_count())},
            {"status", "loaded"},
            {"handlers", handlers},
            {"enabled", is_plugin_enabled(plugin->name())},
            {"url", p_url}
        });
    }
    // Métricas del Thread Pool — lectura lock-free desde los contadores atómicos.
    // server_ puede ser null en el instante previo a Proxy::start(), se protege
    // con guardia ternaria para no causar UB en ningún caso.
    Json thread_pool_info = Json::object{
        {"workers_total",  server_ ? static_cast<double>(server_->pool_worker_count())   : 0.0},
        {"workers_active", server_ ? static_cast<double>(server_->pool_active_workers()) : 0.0},
        {"queue_pending",  server_ ? static_cast<double>(server_->pool_queue_depth())    : 0.0}
    };

    return Json::object{
        {"status", "success"},
        {"version", kAppVersion},
        {"cpu_detected", cpu_info_.cpu_detected},
        {"selected_engine", config_.ace_host},
        {"engine_mode", engine_mode_},
        {"sys_info", system_info()},
        {"server_config", Json::object{
            {"aceproxy", Json::object{{"host", config_.http_host}, {"port", config_.http_port}}},
            {"acestream", Json::object{{"host", config_.ace_host}, {"api_port", config_.ace_api_port}, {"http_port", config_.ace_http_port}}},
            {"limits", Json::object{{"max_connections", config_.max_connections}, {"max_concurrent_channels", config_.max_concurrent_channels}}},
            {"plugins", Json::object{{"enabled", config_.enabled_plugins}}}
        }},
        {"server_info", Json::object{
            {"runtime", "C++20"},
            {"version", kAppVersion},
            {"os_name", "native"},
            {"hostname", get_hostname()},
            {"cpu_detected", cpu_info_.cpu_detected},
            {"selected_engine", config_.ace_host},
            {"engine_mode", engine_mode_},
            {"acestream_engine", acestream_engine_status()},
            {"plugins_loaded", loaded}
        }},
        {"connection_info", Json::object{
            {"max_clients",       config_.max_connections},
            {"total_clients",     static_cast<double>(broadcasts_.client_count())},
            {"active_broadcasts", static_cast<double>(broadcasts_.broadcast_count())},
            {"thread_pool",       thread_pool_info}
        }},
        {"clients_data", clients}
    };
}


Json Proxy::acestream_engine_status() {
    {
        std::lock_guard<std::mutex> lock(ace_status_mutex_);
        auto age = std::chrono::steady_clock::now() - ace_status_time_;
        if (!ace_status_cache_.is_null() && age < std::chrono::seconds(10)) return ace_status_cache_;
    }

    Json status = Json::object{
        {"status", "disconnected"},
        {"version", "unknown"},
        {"host", config_.ace_host},
        {"api_port", config_.ace_api_port}
    };

    try {
        auto url = "http://" + config_.ace_host + ":" + std::to_string(config_.ace_api_port) +
                   "/webui/api/service?method=get_version&format=json";
        auto response = http_client_.get(url, {{"User-Agent", "HTTPAceProxyCPP"}}, 2, false);
        if (response.status >= 200 && response.status < 300) {
            auto data = Json::parse(response.body);
            auto version = data["result"]["version"].as_string("unknown");
            status = Json::object{
                {"status", "connected"},
                {"version", version},
                {"host", config_.ace_host},
                {"api_port", config_.ace_api_port}
            };
        } else {
            status = Json::object{
                {"status", "disconnected"},
                {"version", "unknown"},
                {"host", config_.ace_host},
                {"api_port", config_.ace_api_port},
                {"error", "HTTP status " + std::to_string(response.status)}
            };
        }
    } catch (const std::exception& http_error) {
        try {
            AceClient ace(config_, "EngineStatus");
            ace.authenticate();
            status = Json::object{
                {"status", "connected"},
                {"version", "unknown"},
                {"host", config_.ace_host},
                {"api_port", config_.ace_api_port},
                {"transport", "control"}
            };
        } catch (const std::exception& control_error) {
            status = Json::object{
                {"status", "disconnected"},
                {"version", "unknown"},
                {"host", config_.ace_host},
                {"api_port", config_.ace_api_port},
                {"error", std::string(http_error.what()) + "; control: " + control_error.what()}
            };
        }
    }

    {
        std::lock_guard<std::mutex> lock(ace_status_mutex_);
        ace_status_cache_ = status;
        ace_status_time_ = std::chrono::steady_clock::now();
    }
    return status;
}

Json Proxy::plugins_json() {
    Json::array plugin_array;
    for (const auto& plugin : plugins_.unique_plugins()) {
        if (plugin->name() == "stat" || plugin->name() == "statplugin" || plugin->name() == "aio") continue;
        Json::array channels;
        auto picons = plugin->picons();
        for (const auto& [name, url] : plugin->channels()) {
            auto parsed = parse_url(url);
            if (parsed.scheme != "acestream") continue;
            channels.push_back(Json::object{
                {"name", name},
                {"content_id", parsed.host},
                {"logo", picons.contains(name) ? picons[name] : ""},
                {"status", "unknown"},
                {"last_check", nullptr},
                {"infohash", ""}
            });
        }
        if (!channels.empty()) {
            plugin_array.push_back(Json::object{
                {"name", plugin->name()},
                {"total_channels", static_cast<double>(channels.size())},
                {"channels", channels}
            });
        }
    }
    Json::object res;
    res["status"] = "success";
    res["version"] = kAppVersion;
    res["plugins"] = plugin_array;
    res["total_plugins"] = static_cast<double>(plugin_array.size());
    return res;
}

Json Proxy::check_channel_light(const std::string& plugin_name, const std::string& channel, const std::string& content_id) {
    std::string cid = content_id;
    if (cid.empty()) {
        auto plugin = plugins_.by_handler(plugin_name);
        if (!plugin) return Json::object{{"status", "error"}, {"error", "Plugin not found: " + plugin_name}};
        auto channels = plugin->channels();
        if (!channels.contains(channel)) return Json::object{{"status", "error"}, {"error", "Channel not found: " + channel}};
        auto parsed = parse_url(channels[channel]);
        cid = parsed.scheme == "acestream" ? parsed.host : "";
    }
    if (cid.empty()) return Json::object{{"status", "error"}, {"error", "content_id required"}};
    try {
        AceClient ace(config_, "Check_" + cid.substr(0, 8));
        ace.authenticate();
        auto info = ace.content_info({{"content_id", cid}, {"sessionID", "0"}});
        ace.shutdown();
        return Json::object{{"status", "success"}, {"cached", false}, {"available", true}, {"infohash", info["infohash"].as_string()}, {"checked_at", static_cast<double>(unix_time())}};
    } catch (const std::exception& e) {
        return Json::object{{"status", "success"}, {"cached", false}, {"available", false}, {"error", e.what()}, {"checked_at", static_cast<double>(unix_time())}};
    }
}

Json Proxy::check_channel_peers(const std::string& content_id, int max_wait, const std::string& engine_name) {
    if (content_id.empty()) return Json::object{{"status", "error"}, {"error", "content_id required"}};
    std::string target_engine = engine_name.empty() ? config_.ace_host : engine_name;
    try {
        Config cfg = config_;
        cfg.ace_host = target_engine;

        if (!engine_name.empty()) {
            bool ready = false;
            for (int i = 0; i < 15; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                try {
                    AceClient probe(cfg, "Probe_" + target_engine.substr(0, 6));
                    probe.authenticate();
                    probe.shutdown();
                    ready = true;
                    break;
                } catch (...) {}
            }
            if (!ready) {
                return Json::object{
                    {"status", "error"}, {"engine", target_engine},
                    {"error", "Motor " + target_engine + " no responde en puerto 62062"}, {"available", false}
                };
            }
        }

        AceClient ace(cfg, "PeerCheck_" + target_engine.substr(0, 8) + "_" + content_id.substr(0, 4));
        ace.authenticate();
        auto info = ace.content_info({{"content_id", content_id}, {"sessionID", "0"}});
        auto params = std::map<std::string, std::string>{{"content_id", content_id}, {"file_indexes", "0"}, {"stream_type", cfg.stream_type_string()}};
        ace.start_broadcast_async(params);
        int peers = 0;
        int http_peers = 0;
        std::string status_text = "unknown";
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(max_wait)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto stat = ace.status(1);
            status_text = value_or(stat, "status", status_text);
            try { peers = std::stoi(value_or(stat, "peers", "0")); } catch (...) {}
            try { http_peers = std::stoi(value_or(stat, "http_peers", "0")); } catch (...) {}
            if (peers > 0 || http_peers > 0 || status_text == "dl" || status_text == "buf") break;
        }
        ace.stop_broadcast();
        ace.shutdown();
        int total = peers + http_peers;
        return Json::object{
            {"status", "success"}, {"engine", target_engine}, {"cached", false}, {"available", total > 0 || status_text == "dl" || status_text == "buf"},
            {"peers", peers}, {"http_peers", http_peers}, {"total_peers", total}, {"status_text", status_text},
            {"infohash", info["infohash"].as_string()}, {"checked_at", static_cast<double>(unix_time())}
        };
    } catch (const std::exception& e) {
        return Json::object{{"status", "error"}, {"engine", target_engine}, {"error", e.what()}, {"available", false}};
    }
}

bool Proxy::is_plugin_enabled(const std::string& name) const {
    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
    if (!plugins_state_json_.is_object()) return true;
    if (plugins_state_json_.contains("custom_lists") && plugins_state_json_["custom_lists"].is_array()) {
        for (const auto& item : plugins_state_json_["custom_lists"].as_array()) {
            if (item.is_object() && item.contains("name") && item["name"].as_string() == name) {
                return item["enabled"].as_bool(true);
            }
        }
    }
    if (plugins_state_json_.contains(name)) {
        return plugins_state_json_[name].as_bool(true);
    }
    return true;
}

void Proxy::set_plugin_enabled(const std::string& name, bool enabled) {
    {
        std::lock_guard<std::mutex> lock(plugins_state_mutex_);
        Json::object obj;
        if (plugins_state_json_.is_object()) {
            obj = plugins_state_json_.as_object();
        }
        bool is_custom = false;
        if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
            Json::array arr = obj["custom_lists"].as_array();
            for (auto& item : arr) {
                if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                    Json::object item_obj = item.as_object();
                    item_obj["enabled"] = enabled;
                    item = item_obj;
                    is_custom = true;
                    break;
                }
            }
            if (is_custom) {
                obj["custom_lists"] = arr;
            }
        }
        if (!is_custom) {
            obj[name] = enabled;
        }
        plugins_state_json_ = obj;
    }
    save_plugins_state();

    for (auto& plugin : plugins_.unique_plugins()) {
        if (plugin->name() == name) {
            plugin->set_enabled(enabled);
            if (enabled) {
                if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
                    std::thread([playlist] {
                        try { playlist->refresh_if_needed(); } catch (...) {}
                    }).detach();
                }
            }
            break;
        }
    }
}

std::string Proxy::get_plugin_url(const std::string& name, const std::string& fallback) const {
    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
    if (!plugins_state_json_.is_object()) return fallback;
    if (plugins_state_json_.contains("custom_lists") && plugins_state_json_["custom_lists"].is_array()) {
        for (const auto& item : plugins_state_json_["custom_lists"].as_array()) {
            if (item.is_object() && item.contains("name") && item["name"].as_string() == name) {
                if (item.contains("url")) {
                    return item["url"].as_string();
                }
            }
        }
    }
    if (plugins_state_json_.contains("urls") && plugins_state_json_["urls"].is_object()) {
        auto urls = plugins_state_json_["urls"].as_object();
        auto it = urls.find(name);
        if (it != urls.end()) {
            return it->second.as_string(fallback);
        }
    }
    return fallback;
}

void Proxy::set_plugin_url(const std::string& name, const std::string& url) {
    {
        std::lock_guard<std::mutex> lock(plugins_state_mutex_);
        Json::object obj;
        if (plugins_state_json_.is_object()) {
            obj = plugins_state_json_.as_object();
        }
        bool is_custom = false;
        if (obj.contains("custom_lists") && obj["custom_lists"].is_array()) {
            Json::array arr = obj["custom_lists"].as_array();
            for (auto& item : arr) {
                if (item.is_object() && item.as_object().contains("name") && item.as_object().at("name").as_string() == name) {
                    Json::object item_obj = item.as_object();
                    item_obj["url"] = url;
                    item = item_obj;
                    is_custom = true;
                    break;
                }
            }
            if (is_custom) {
                obj["custom_lists"] = arr;
            }
        }
        if (!is_custom) {
            Json::object urls;
            if (obj.contains("urls") && obj["urls"].is_object()) {
                urls = obj["urls"].as_object();
            }
            urls[name] = url;
            obj["urls"] = urls;
        }
        plugins_state_json_ = obj;
    }
    save_plugins_state();

    for (auto& plugin : plugins_.unique_plugins()) {
        if (plugin->name() == name && plugin->is_enabled()) {
            if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
                std::thread([playlist]() {
                    try { playlist->force_refresh(); } catch (...) {}
                }).detach();
            }
            break;
        }
    }
}

Json Proxy::plugins_state_json() const {
    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
    return plugins_state_json_;
}

extern std::shared_ptr<Plugin> create_custom_list_plugin_helper(Config config, HttpClient& http_client, Proxy& proxy, const std::string& name, const std::string& url);

void Proxy::add_custom_list_plugin(const std::string& name, const std::string& url) {
    auto existing = plugins_.by_handler(name);
    if (existing) {
        if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(existing)) {
            set_plugin_url(name, url);
            std::thread([playlist]() {
                try { playlist->force_refresh(); } catch (...) {}
            }).detach();
        }
        return;
    }
    
    // Register new custom plugin
    auto plugin = create_custom_list_plugin_helper(config_, http_client_, *this, name, url);
    plugins_.add(plugin);
    
    if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
        std::thread([playlist]() {
            try { playlist->force_refresh(); } catch (...) {}
        }).detach();
    }
}

void Proxy::remove_custom_list_plugin(const std::string& name) {
    // Disable it first so it frees memory
    for (auto& plugin : plugins_.unique_plugins()) {
        if (plugin->name() == name) {
            plugin->set_enabled(false);
            break;
        }
    }
    plugins_.remove(name);
}

void Proxy::load_plugins_state() {
    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
    auto filepath = std::filesystem::path(config_.root_dir) / "http" / "plugins_state.json";
    std::ifstream file(filepath.string());
    if (!file.is_open()) return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    try {
        plugins_state_json_ = Json::parse(buffer.str());
        if (plugins_state_json_.is_object()) {
            auto obj = plugins_state_json_.as_object();
            obj["version"] = kAppVersion;
            plugins_state_json_ = obj;
        }
    } catch (...) {
        log_line("ERROR", "Failed to parse plugins_state.json");
    }
}

void Proxy::save_plugins_state() {
    std::lock_guard<std::mutex> lock(plugins_state_mutex_);
    auto filepath = std::filesystem::path(config_.root_dir) / "http" / "plugins_state.json";
    std::ofstream file(filepath.string());
    if (file.is_open()) {
        if (plugins_state_json_.is_object()) {
            auto obj = plugins_state_json_.as_object();
            obj["version"] = kAppVersion;
            plugins_state_json_ = obj;
        }
        file << plugins_state_json_.dump(2);
    } else {
        log_line("ERROR", "Failed to write plugins_state.json");
    }
}

Json Proxy::check_epg_url(const std::string& target_url) {
    auto url = target_url;
    if (url.empty()) {
        url = get_plugin_url("epg", "https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz");
    }
    try {
        auto response = http_client_.get(url, {{"User-Agent", "Mozilla/5.0"}}, 10, true);
        if (response.status >= 200 && response.status < 400) {
            std::string content_type = "unknown";
            auto it = response.headers.find("content-type");
            if (it != response.headers.end()) content_type = it->second;

            return Json::object{
                {"status", "success"},
                {"http_status", static_cast<double>(response.status)},
                {"size_bytes", static_cast<double>(response.body.size())},
                {"url", response.url.empty() ? url : response.url},
                {"content_type", content_type}
            };
        } else {
            return Json::object{
                {"status", "error"},
                {"http_status", static_cast<double>(response.status)},
                {"error", "HTTP " + std::to_string(response.status)}
            };
        }
    } catch (const std::exception& e) {
        return Json::object{
            {"status", "error"},
            {"error", e.what()}
        };
    }
}

} // namespace httpace
