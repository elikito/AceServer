#include "httpaceproxycpp/plugins.hpp"
#include "httpaceproxycpp/proxy.hpp"
#include "httpaceproxycpp/util.hpp"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>

namespace httpace {
namespace {

constexpr const char* kEpgUrl = "https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz";

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

std::vector<std::string> env_csv_or(const char* name, const std::vector<std::string>& fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    std::vector<std::string> out;
    for (auto item : split(value, ',', false)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out.empty() ? fallback : out;
}

void send_bytes(ClientConnection& connection, int status, const std::string& content_type, const std::string& body,
                std::map<std::string, std::string> headers = {}) {
    headers["Content-Type"] = content_type;
    headers["Content-Length"] = std::to_string(body.size());
    headers["Connection"] = "close";
    connection.send_response_headers(status, status_reason(status), headers);
    connection.send_text(body);
}

std::string host_header(const RequestContext& ctx) {
    return ctx.request.header("host", "localhost:8888");
}

std::string normalize_github_blob_url(const std::string& url) {
    auto parsed = parse_url(url);
    if (parsed.host == "github.com" && parsed.path.find("/blob/") != std::string::npos) {
        return "https://raw.githubusercontent.com" + replace_all(parsed.path, "/blob/", "/");
    }
    return url;
}

bool is_shortener_url(const std::string& url) {
    auto host = lower(parse_url(url).host);
    if (starts_with(host, "www.")) host = host.substr(4);
    return host == "cutt.ly" || host == "urlfy.org" || host == "n9.cl" || host == "smurl.es";
}

std::string channel_name_from_request(const RequestContext& ctx) {
    auto base = ctx.parts.empty() ? "" : ctx.parts.back();
    return url_decode(basename_no_ext(base));
}

std::string ext_from_request(const RequestContext& ctx) {
    auto ext = extension_of(ctx.path);
    return ext.empty() ? "m3u8" : ext;
}

PlaylistItem item_from_m3u_extinf(const std::string& extinf_line, const std::string& url, const std::string& fallback_group = "Unknown") {
    auto attrs = parse_extinf_attrs(extinf_line);
    PlaylistItem item;
    item.name = parse_extinf_name(extinf_line);
    item.tvg = attrs.contains("tvg-name") ? attrs["tvg-name"] : item.name;
    item.tvgid = attrs.contains("tvg-id") ? attrs["tvg-id"] : "";
    item.group = attrs.contains("group-title") ? attrs["group-title"] : fallback_group;
    item.logo = attrs.contains("tvg-logo") ? attrs["tvg-logo"] : "";
    item.url = url;
    return item;
}

std::string normalize_catalog_name(std::string value, bool compact) {
    value = lower(value);
    value = std::regex_replace(value, std::regex(R"(^\d+(?:\.\d+)?\s*)"), "");
    value = std::regex_replace(value, std::regex(R"(\.w3u$)"), "");
    value = replace_all(value, "#", " ");
    std::map<std::string, std::string> aliases = {{"m", "movistar"}, {"tennis", "tenis"}, {"us", "usa"}};
    std::set<std::string> skip = compact ? std::set<std::string>{"sport", "sports", "tv", "channel", "hd", "newloop"} : std::set<std::string>{};
    std::vector<std::string> tokens;
    for (auto token : split(std::regex_replace(value, std::regex(R"([^a-z0-9]+)"), " "), ' ', false)) {
        if (aliases.contains(token)) token = aliases[token];
        if (!token.empty() && !skip.contains(token)) tokens.push_back(token);
    }
    return join(tokens, " ");
}

double rough_similarity(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return 0.0;
    auto a_tokens = split(a, ' ', false);
    auto b_tokens = split(b, ' ', false);
    std::set<std::string> aa(a_tokens.begin(), a_tokens.end());
    std::set<std::string> bb(b_tokens.begin(), b_tokens.end());
    std::size_t inter = 0;
    for (const auto& token : aa) if (bb.contains(token)) ++inter;
    auto score = static_cast<double>(inter * 2) / static_cast<double>(aa.size() + bb.size());
    std::size_t prefix = 0;
    auto limit = std::min(a_tokens.size(), b_tokens.size());
    while (prefix < limit && a_tokens[prefix] == b_tokens[prefix]) ++prefix;
    score += static_cast<double>(prefix) * 0.05;
    return std::min(1.0, score);
}

} // namespace

void RequestContext::rewrite_to(const std::string& new_path) {
    path = new_path;
    auto q = path.find('?');
    if (q != std::string::npos) {
        query = path.substr(q + 1);
        path = path.substr(0, q);
    }
    parts = split(path, '/', true);
    reqtype = parts.size() > 1 ? lower(parts[1]) : "";
    rewritten = true;
}

void PluginRegistry::add(std::shared_ptr<Plugin> plugin) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& handler : plugin->handlers()) handlers_[lower(handler)] = plugin;
}

void PluginRegistry::remove(const std::string& handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(lower(handler));
}

std::shared_ptr<Plugin> PluginRegistry::by_handler(const std::string& handler) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(lower(handler));
    return it == handlers_.end() ? nullptr : it->second;
}

std::map<std::string, std::shared_ptr<Plugin>> PluginRegistry::handlers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handlers_;
}

std::vector<std::shared_ptr<Plugin>> PluginRegistry::unique_plugins() const {
    std::vector<std::shared_ptr<Plugin>> out;
    std::set<Plugin*> seen;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [_, plugin] : handlers_) {
        if (seen.insert(plugin.get()).second) out.push_back(plugin);
    }
    return out;
}

PlaylistPlugin::PlaylistPlugin(Config config, HttpClient& http_client, Proxy& proxy, std::string plugin_name,
                               std::string header, int update_minutes)
    : config_(std::move(config)),
      http_client_(http_client),
      proxy_(proxy),
      plugin_name_(std::move(plugin_name)),
      header_(std::move(header)),
      update_minutes_(update_minutes),
      playlist_(header_) {
    playlist_time_ = std::chrono::steady_clock::time_point{};
    if (update_minutes_ > 0) {
        updater_ = std::thread([this] {
            while (!stop_updater_) {
                std::unique_lock<std::mutex> lock(updater_mutex_);
                if (updater_cv_.wait_for(lock, std::chrono::minutes(update_minutes_), [this] { return stop_updater_; })) break;
                lock.unlock();
                refresh_if_needed();
            }
        });
    }
}

PlaylistPlugin::~PlaylistPlugin() {
    {
        std::lock_guard<std::mutex> lock(updater_mutex_);
        stop_updater_ = true;
    }
    updater_cv_.notify_all();
    if (updater_.joinable()) updater_.join();
}

void PlaylistPlugin::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled) {
        playlist_ = PlaylistGenerator(header_);
        channels_.clear();
        picons_.clear();
        etag_.clear();
        playlist_time_ = std::chrono::steady_clock::time_point{};
    }
}

bool PlaylistPlugin::is_enabled() const {
    return proxy_.is_plugin_enabled(plugin_name_);
}

bool PlaylistPlugin::handle(RequestContext& ctx) {
    refresh_if_needed();
    if (ctx.path.find("/" + plugin_name_ + "/channel/") == 0) {
        if (!(ends_with(ctx.path, ".ts") || ends_with(ctx.path, ".m3u8"))) {
            send_bytes(ctx.connection, 404, "text/plain", "Invalid path: must end with .ts or .m3u8");
            return true;
        }
        return rewrite_channel(ctx, channel_name_from_request(ctx), ext_from_request(ctx));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!etag_.empty() && etag_ == ctx.request.header("if-none-match")) {
            ctx.connection.send_response_headers(304, status_reason(304), {{"Connection", "close"}});
            return true;
        }
        auto body = playlist_.export_m3u(host_header(ctx), "/" + plugin_name_ + "/channel", ctx.query, true);
        std::map<std::string, std::string> headers = {{"Access-Control-Allow-Origin", "*"}};
        if (!etag_.empty() && ctx.request.version == "HTTP/1.1") headers["ETag"] = etag_;
        send_bytes(ctx.connection, 200, "audio/mpegurl; charset=utf-8", body, headers);
    }
    return true;
}

std::vector<PlaylistItem> PlaylistPlugin::playlist_items() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playlist_.items();
}

std::map<std::string, std::string> PlaylistPlugin::channels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_;
}

std::map<std::string, std::string> PlaylistPlugin::picons() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return picons_;
}

std::size_t PlaylistPlugin::channel_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_.size();
}

bool PlaylistPlugin::refresh_if_needed(bool force) {
    if (!is_enabled()) {
        set_enabled(false);
        return false;
    }
    if (!force) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto age = std::chrono::steady_clock::now() - playlist_time_;
        if (!playlist_.empty() && age < std::chrono::minutes(30)) return true;
    }
    try { return refresh(); } catch (const std::exception& e) {
        log_line("ERROR", "[" + plugin_name_ + "] refresh failed: " + e.what());
        return false;
    }
}

bool PlaylistPlugin::force_refresh() {
    return refresh_if_needed(true);
}

void PlaylistPlugin::set_playlist(PlaylistGenerator playlist,
                                  std::map<std::string, std::string> channels,
                                  std::map<std::string, std::string> picons) {
    std::lock_guard<std::mutex> lock(mutex_);
    playlist_ = std::move(playlist);
    channels_ = std::move(channels);
    picons_ = std::move(picons);
    etag_ = playlist_.etag();
    playlist_time_ = std::chrono::steady_clock::now();
}

bool PlaylistPlugin::rewrite_channel(RequestContext& ctx, const std::string& channel_name, const std::string& ext) {
    std::string url;
    std::string icon;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto decoded_name = url_decode(channel_name);
        auto it = channels_.find(channel_name);
        if (it == channels_.end()) {
            it = channels_.find(decoded_name);
        }
        if (it == channels_.end()) {
            auto target_lower = lower(trim(decoded_name));
            for (auto map_it = channels_.begin(); map_it != channels_.end(); ++map_it) {
                if (lower(trim(map_it->first)) == target_lower) {
                    it = map_it;
                    break;
                }
            }
        }
        if (it == channels_.end()) {
            double best_score = 0.0;
            auto best_it = channels_.end();
            for (auto map_it = channels_.begin(); map_it != channels_.end(); ++map_it) {
                double score = rough_similarity(decoded_name, map_it->first);
                if (score > best_score && score > 0.6) {
                    best_score = score;
                    best_it = map_it;
                }
            }
            if (best_it != channels_.end()) {
                it = best_it;
            }
        }

        if (it == channels_.end()) {
            send_bytes(ctx.connection, 404, "text/plain", "[" + plugin_name_ + "] unknown channel: " + channel_name);
            return true;
        }
        url = it->second;
        icon = picons_.contains(it->first) ? picons_[it->first] : "";
    }
    auto parsed = parse_url(url);
    std::string new_path;
    if (parsed.scheme == "acestream") new_path = "/content_id/" + parsed.host + "/" + channel_name + "." + ext;
    else if (parsed.scheme == "infohash") new_path = "/infohash/" + parsed.host + "/" + channel_name + "." + ext;
    else if (parsed.scheme == "http" || parsed.scheme == "https") new_path = "/url/" + url_encode(url, "") + "/" + channel_name + "." + ext;
    else {
        send_bytes(ctx.connection, 404, "text/plain", "Unsupported channel URL scheme");
        return true;
    }
    ctx.channel_name = channel_name;
    ctx.channel_icon = icon;
    ctx.rewrite_to(new_path);
    return false;
}

class NewEraPlugin : public PlaylistPlugin {
public:
    NewEraPlugin(Config cfg, HttpClient& client, Proxy& proxy)
        : PlaylistPlugin(std::move(cfg), client, proxy, "newera", PlaylistGenerator::epg_header(kEpgUrl, 0), 60) {}
protected:
    bool refresh() override {
        auto default_url = env_or("NEWERA_PLAYLIST_URL", "https://ipfs.io/ipns/k2k4r8lm8tkmuxbc8lkmq1in3v0oya1p6pe9o5bu0hu30br5ko08k2gb/data/listas/lista_iptv.m3u");
        auto url = proxy_.get_plugin_url("newera", default_url);
        auto response = http_client_.get(url, {{"User-Agent", kBrowserUserAgent}}, 60);
        PlaylistGenerator playlist(header_);
        std::map<std::string, std::string> channels;
        std::map<std::string, std::string> picons;
        for (auto& item : parse_m3u_acestream_items(response.body, channels, picons)) {
            playlist.add_item(item);
        }
        if (channels.empty()) log_line("WARNING", "[newera] parsed zero channels from status " + std::to_string(response.status) + ", body bytes " + std::to_string(response.body.size()));
        set_playlist(std::move(playlist), std::move(channels), std::move(picons));
        log_line("INFO", "[newera] playlist generated with " + std::to_string(channel_count()) + " channels");
        return true;
    }
};

class ElcanoPlugin : public PlaylistPlugin {
public:
    ElcanoPlugin(Config cfg, HttpClient& client, Proxy& proxy)
        : PlaylistPlugin(std::move(cfg), client, proxy, "elcano", PlaylistGenerator::epg_header(kEpgUrl, 0), 60) {}
protected:
    bool refresh() override {
        auto default_urls = env_csv_or("ELCANO_PLAYLIST_URL", {
            "https://k51qzi5uqu5dh5qej4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik820fecs.ipns.inbrowser.link/hashes.json",
            "https://ipfs.io/ipns/k51qzi5uqu5dh5qej4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik820fecs/hashes_acestream.m3u"
        });
        std::string default_url_str = join(default_urls, ",");
        std::string configured_url = proxy_.get_plugin_url("elcano", default_url_str);
        std::vector<std::string> urls = split(configured_url, ',', false);
        PlaylistGenerator playlist(header_);
        std::map<std::string, std::string> channels;
        std::map<std::string, std::string> picons;
        std::set<std::string> seen_hashes;

        for (auto playlist_url : urls) {
            playlist_url = trim(playlist_url);
            if (playlist_url.empty()) continue;
            auto norm_url = normalize_list_url(playlist_url);
            if (norm_url.find("/ipns/") != std::string::npos && (ends_with(norm_url, "/") || (norm_url.find(".m3u") == std::string::npos && norm_url.find(".json") == std::string::npos && norm_url.find(".txt") == std::string::npos))) {
                if (ends_with(norm_url, "/")) norm_url += "hashes.json";
                else norm_url += "/hashes.json";
            }
            try {
                auto response = http_client_.get(norm_url, {{"User-Agent", kBrowserUserAgent}}, 60);
                std::string trimmed_content = trim(response.body);

                if (starts_with(trimmed_content, "{") || starts_with(trimmed_content, "[")) {
                    try {
                        auto j = Json::parse(trimmed_content);
                        std::function<void(const Json&, const std::string&)> parse_elcano_json = [&](const Json& node, const std::string& cur_grp) {
                            if (node.is_object()) {
                                std::string grp = node.contains("group") ? node["group"].as_string() : (node.contains("category") ? node["category"].as_string() : cur_grp);
                                if (grp.empty()) grp = "Otros";

                                if ((node.contains("hash") || node.contains("url") || node.contains("id")) && (node.contains("title") || node.contains("name"))) {
                                    auto st_name = node.contains("title") ? node["title"].as_string() : node["name"].as_string();
                                    auto raw_hash = node.contains("hash") ? node["hash"].as_string() : (node.contains("url") ? node["url"].as_string() : node["id"].as_string());
                                    auto logo = node.contains("logo") ? node["logo"].as_string() : (node.contains("image") ? node["image"].as_string() : "");
                                    auto tvg_id = node.contains("tvg_id") ? node["tvg_id"].as_string() : (node.contains("tvg-id") ? node["tvg-id"].as_string() : st_name);

                                    if (!st_name.empty() && !raw_hash.empty()) {
                                        std::string st_url = (starts_with(raw_hash, "acestream://") || starts_with(raw_hash, "http://") || starts_with(raw_hash, "https://")) ? raw_hash : ("acestream://" + raw_hash);
                                        std::string hash_only = starts_with(st_url, "acestream://") ? st_url.substr(12) : st_url;
                                        if (seen_hashes.insert(hash_only).second) {
                                            PlaylistItem item{st_name, url_encode(st_name, ""), grp, st_name, tvg_id, logo};
                                            channels[st_name] = st_url;
                                            if (!logo.empty()) picons[st_name] = logo;
                                            playlist.add_item(item);
                                        }
                                    }
                                }

                                if (node.contains("hashes") && node["hashes"].is_array()) {
                                    for (const auto& h : node["hashes"].as_array()) parse_elcano_json(h, grp);
                                }
                                if (node.contains("stations") && node["stations"].is_array()) {
                                    for (const auto& s : node["stations"].as_array()) parse_elcano_json(s, grp);
                                }
                                if (node.contains("groups") && node["groups"].is_array()) {
                                    for (const auto& g : node["groups"].as_array()) parse_elcano_json(g, grp);
                                }
                            } else if (node.is_array()) {
                                for (const auto& el : node.as_array()) parse_elcano_json(el, cur_grp);
                            }
                        };
                        parse_elcano_json(j, "Elcano");
                    } catch (...) {}
                }

                if (channels.empty() || starts_with(trimmed_content, "#EXTM3U") || starts_with(trimmed_content, "#EXTINF")) {
                    for (auto& item : parse_m3u_acestream_items(response.body, channels, picons)) {
                        std::string st_url = channels.contains(item.name) ? channels[item.name] : "";
                        std::string hash_only = starts_with(st_url, "acestream://") ? st_url.substr(12) : st_url;
                        if (hash_only.empty() || seen_hashes.insert(hash_only).second) {
                            playlist.add_item(item);
                        }
                    }
                }
            } catch (const std::exception& e) {
                log_line("ERROR", "[elcano] source failed " + playlist_url + ": " + e.what());
            }
        }
        set_playlist(std::move(playlist), std::move(channels), std::move(picons));
        log_line("INFO", "[elcano] playlist generated with " + std::to_string(channel_count()) + " channels");
        return true;
    }
};

class AcePLPlugin : public PlaylistPlugin {
public:
    AcePLPlugin(Config cfg, HttpClient& client, Proxy& proxy)
        : PlaylistPlugin(std::move(cfg), client, proxy, "acepl", PlaylistGenerator::epg_header("", 0), 30) {}
protected:
    bool refresh() override {
        auto default_url = "https://api.acestream.me/all?api_version=1.0&api_key=test_api_key";
        auto url = normalize_list_url(proxy_.get_plugin_url(name(), default_url));
        url = replace_all(url, " ", "%20");
        auto response = http_client_.get(url, {{"User-Agent", kBrowserUserAgent}}, 60);
        auto data = Json::parse(response.body);
        PlaylistGenerator playlist(header_, "#EXTINF:-1 group-title=\"{group}\" tvg-name=\"{name}\",{name}\n#EXTGRP:{group}\n{url}\n");
        std::map<std::string, std::string> channels;
        std::map<std::string, std::string> picons;
        for (const auto& channel : data.as_array()) {
            auto infohash = trim(channel["infohash"].as_string());
            auto name = trim(channel["name"].as_string());
            if (infohash.empty() || name.empty()) continue;
            PlaylistItem item;
            item.name = name;
            item.tvg = name;
            item.group = "Other";
            if (channel["categories"].is_array() && !channel["categories"].as_array().empty()) {
                std::vector<std::string> groups;
                for (const auto& cat : channel["categories"].as_array()) groups.push_back(cat.as_string());
                item.group = join(groups, ", ");
            }
            item.url = url_encode(name, "");
            item.availability = channel["availability"].as_number(0.0);
            channels[name] = "acestream://" + infohash;
            picons[name] = "";
            playlist.add_item(item);
        }
        set_playlist(std::move(playlist), std::move(channels), std::move(picons));
        log_line("INFO", "[acepl] playlist generated with " + std::to_string(channel_count()) + " channels");
        return true;
    }
};

class Af1c1onadosPlugin : public PlaylistPlugin {
public:
    Af1c1onadosPlugin(Config cfg, HttpClient& client, Proxy& proxy)
        : PlaylistPlugin(std::move(cfg), client, proxy, "af1c1onados", PlaylistGenerator::epg_header(kEpgUrl, 0, true), 60) {}
protected:
    bool refresh() override {
        auto default_url = "https://raw.githubusercontent.com/af1Series1/Tritolgia/refs/heads/main/AcEStREAM%20iDs.w3u";
        auto root_url = proxy_.get_plugin_url("af1c1onados", default_url);
        auto data = fetch_playlist_json(root_url);
        PlaylistGenerator playlist(header_);
        std::map<std::string, std::string> channels;
        std::map<std::string, std::string> picons;
        collect_groups(data, "Others", playlist, channels, picons, {});
        set_playlist(std::move(playlist), std::move(channels), std::move(picons));
        log_line("INFO", "[af1c1onados] playlist generated with " + std::to_string(channel_count()) + " channels");
        return true;
    }

private:
    Json fetch_playlist_json(std::string url) {
        url = normalize_github_blob_url(url);
        if (is_shortener_url(url)) {
            try {
                auto response = http_client_.get(url, {{"User-Agent", kBrowserUserAgent}}, 10, true);
                url = normalize_github_blob_url(response.url);
            } catch (...) {}
        }
        auto response = http_client_.get(url, {{"User-Agent", kBrowserUserAgent}}, 20, true);
        return Json::parse(response.body);
    }

    std::optional<std::string> guess_catalog_url(const std::string& group_name) {
        try {
            auto strict = normalize_catalog_name(group_name, false);
            auto compact = normalize_catalog_name(group_name, true);
            double best_score = 0.0;
            double second_score = 0.0;
            std::string best_path;
            for (const auto& entry : catalog_entries()) {
                double score = std::max(rough_similarity(strict, entry.strict), rough_similarity(compact, entry.compact));
                if (score > best_score) {
                    second_score = best_score;
                    best_score = score;
                    best_path = entry.path;
                } else if (score > second_score) {
                    second_score = score;
                }
            }
            if (best_score >= 0.55 && (best_score - second_score) >= 0.01) {
                log_line("INFO", "[af1c1onados] resolved subgroup " + group_name + " via catalog tree: " + best_path);
                return "https://raw.githubusercontent.com/af1Series1/Tritolgia/main/" + url_encode(best_path, "/");
            }
        } catch (const std::exception& e) {
            log_line("ERROR", "[af1c1onados] catalog fallback failed: " + std::string(e.what()));
        }
        return std::nullopt;
    }

    struct CatalogEntry {
        std::string path;
        std::string strict;
        std::string compact;
    };

    const std::vector<CatalogEntry>& catalog_entries() {
        if (!catalog_entries_.empty()) return catalog_entries_;
        try {
            auto response = http_client_.get("https://api.github.com/repos/af1Series1/Tritolgia/git/trees/main?recursive=1",
                                             {{"User-Agent", kBrowserUserAgent}}, 20, true);
            if (response.status == 200) {
                auto parsed = Json::parse(response.body);
                if (parsed.is_object() && parsed.contains("tree") && parsed["tree"].is_array()) {
                    for (const auto& item : parsed["tree"].as_array()) {
                        if (item["type"].as_string() != "blob") continue;
                        auto path = item["path"].as_string();
                        if (path.empty() || path == "AcEStREAM iDs.w3u") continue;
                        catalog_entries_.push_back(CatalogEntry{
                            path,
                            normalize_catalog_name(path, false),
                            normalize_catalog_name(path, true)
                        });
                    }
                }
            } else {
                log_line("WARNING", "[af1c1onados] GitHub API tree fetch returned status " + std::to_string(response.status));
            }
        } catch (const std::exception& e) {
            log_line("ERROR", "[af1c1onados] catalog_entries tree fetch failed: " + std::string(e.what()));
        }
        return catalog_entries_;
    }

    void collect_groups(const Json& data, const std::string& fallback_group, PlaylistGenerator& playlist,
                        std::map<std::string, std::string>& channels,
                        std::map<std::string, std::string>& picons,
                        std::set<std::string> visited) {
        auto group_name = data["name"].as_string(fallback_group);
        for (const auto& station : data["stations"].as_array()) {
            auto name = station["name"].as_string();
            auto url = station["url"].as_string();
            if (name.empty() || url.empty()) continue;
            auto unique = name;
            int n = 2;
            while (channels.contains(unique)) unique = name + " (" + std::to_string(n++) + ")";
            PlaylistItem item{unique, url_encode(unique, ""), group_name, unique, "", station["image"].as_string()};
            channels[unique] = url;
            picons[unique] = item.logo;
            playlist.add_item(item);
        }
        for (const auto& group : data["groups"].as_array()) {
            auto child_name = group["name"].as_string(group_name);
            if (!group["stations"].as_array().empty()) {
                Json child(Json::object{{"name", child_name}, {"stations", group["stations"]}});
                collect_groups(child, child_name, playlist, channels, picons, visited);
            } else {
                auto url = group["url"].as_string();
                if (url.empty()) continue;
                auto normalized = normalize_github_blob_url(url);
                if (visited.contains(normalized)) continue;

                if (is_shortener_url(normalized)) {
                    if (auto guessed = guess_catalog_url(child_name)) {
                        auto guessed_normalized = normalize_github_blob_url(*guessed);
                        if (!visited.contains(guessed_normalized)) {
                            visited.insert(guessed_normalized);
                            try {
                                auto child = fetch_playlist_json(*guessed);
                                collect_groups(child, child_name, playlist, channels, picons, visited);
                                continue;
                            } catch (const std::exception& fallback_error) {
                                log_line("ERROR", "[af1c1onados] catalog subgroup failed " + child_name + ": " + fallback_error.what());
                            }
                        }
                    }
                }

                visited.insert(normalized);
                try {
                    auto child = fetch_playlist_json(url);
                    collect_groups(child, child_name, playlist, channels, picons, visited);
                } catch (const std::exception& e) {
                    if (auto guessed = guess_catalog_url(child_name)) {
                        try {
                            auto child = fetch_playlist_json(*guessed);
                            collect_groups(child, child_name, playlist, channels, picons, visited);
                            continue;
                        } catch (const std::exception& fallback_error) {
                            log_line("ERROR", "[af1c1onados] fallback subgroup failed " + child_name + ": " + fallback_error.what());
                        }
                    }
                    log_line("ERROR", "[af1c1onados] subgroup failed " + child_name + ": " + e.what());
                }
            }
        }
    }

    std::vector<CatalogEntry> catalog_entries_;
};

class AioPlugin : public Plugin {
public:
    AioPlugin(Config cfg, Proxy& proxy) : config_(std::move(cfg)), proxy_(proxy) {}
    std::string name() const override { return "aio"; }
    std::vector<std::string> handlers() const override { return {"aio"}; }
    bool handle(RequestContext& ctx) override {
        PlaylistGenerator generator(PlaylistGenerator::epg_header(kEpgUrl, 0, true));
        std::string host = host_header(ctx);
        if (host.empty()) host = config_.http_host + ":" + std::to_string(config_.http_port);
        if (host.empty() || starts_with(host, "0.0.0.0")) host = "127.0.0.1:8888";

        // 1. Inyectar en la PRIMERA POSICIÓN los canales marcados en Favoritos
        auto favs = proxy_.get_epg_favorites();
        if (!favs.empty()) {
            for (const auto& fav_name : favs) {
                std::string slug = canonical_slug(fav_name);
                if (slug.empty()) continue;
                std::string display_name = canonical_name(fav_name);
                bool cap = true;
                for (char& c : display_name) {
                    if (std::isspace(static_cast<unsigned char>(c))) cap = true;
                    else if (cap) { c = std::toupper(static_cast<unsigned char>(c)); cap = false; }
                }
                if (display_name.empty()) display_name = slug;

                PlaylistItem fav_item;
                fav_item.name = display_name;
                fav_item.group = "⭐ Favoritos";
                fav_item.tvgid = slug;
                fav_item.url = "http://" + host + "/auto/" + slug + "/stream.ts";
                generator.add_item(fav_item);
            }
        }

        // 2. Canales de los plugins habilitados
        std::set<Plugin*> processed;
        auto handlers = proxy_.plugins().handlers();
        for (const auto& [handler, plugin] : handlers) {
            if (handler == "aio" || handler == "stat" || handler == "statplugin" || handler == "torrenttv_api") continue;
            if (!config_.aio_includes(handler)) continue;
            if (!plugin->is_enabled()) continue;
            if (!processed.insert(plugin.get()).second) continue;
            for (auto item : plugin->playlist_items()) {
                auto channels = plugin->channels();
                if (channels.contains(item.name)) item.url = channels[item.name];
                if (item.group.empty()) item.group = handler;
                generator.add_item(item);
            }
        }
        auto body = generator.export_m3u(host, "", ctx.query, false);
        send_bytes(ctx.connection, 200, "audio/mpegurl; charset=utf-8", body);
        return true;
    }
private:
    Config config_;
    Proxy& proxy_;
};

class StatPlugin : public Plugin {
public:
    StatPlugin(Config cfg, Proxy& proxy) : config_(std::move(cfg)), proxy_(proxy) {}
    std::string name() const override { return "stat"; }
    std::vector<std::string> handlers() const override { return {"stat"}; }
    bool handle(RequestContext& ctx) override {
        auto action = query_get(ctx.query, "action");
        if (action == "get_status") {
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", proxy_.status_json().dump(2));
            return true;
        }
        if (action == "set_engine") {
            auto name = query_get(ctx.query, "name");
            if (name.empty()) name = query_get(ctx.query, "engine");
            if (name.empty()) name = query_get(ctx.query, "mode");
            proxy_.set_engine(name);
            Json res = Json::object{
                {"status", "success"},
                {"cpu_detected", proxy_.cpu_detected()},
                {"selected_engine", proxy_.selected_engine()},
                {"engine_mode", proxy_.engine_mode()}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        }
        if (action == "set_limits" || action == "set_config") {
            auto max_conn_str = query_get(ctx.query, "max_connections");
            if (max_conn_str.empty()) max_conn_str = query_get(ctx.query, "max_conns");
            auto max_chan_str = query_get(ctx.query, "max_channels");
            if (max_chan_str.empty()) max_chan_str = query_get(ctx.query, "max_concurrent_channels");

            int max_conn = -1;
            int max_chan = -1;
            try { if (!max_conn_str.empty()) max_conn = std::stoi(max_conn_str); } catch (...) {}
            try { if (!max_chan_str.empty()) max_chan = std::stoi(max_chan_str); } catch (...) {}

            proxy_.set_limits(max_conn, max_chan);

            Json res = Json::object{
                {"status", "success"},
                {"max_connections", proxy_.config().max_connections},
                {"max_concurrent_channels", proxy_.config().max_concurrent_channels}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        }
        if (action == "get_bunker_logs") {
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", proxy_.get_bunker_logs_json().dump(2));
            return true;
        }
        if (action == "check_peers") {
            int max_wait = 10;
            try { max_wait = std::stoi(query_get(ctx.query, "max_wait", "10")); } catch (...) {}
            max_wait = std::min(30, std::max(3, max_wait));
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "motor");
            auto data = proxy_.check_channel_peers(query_get(ctx.query, "content_id"), max_wait, engine);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "check_channel") {
            auto data = proxy_.check_channel_light(query_get(ctx.query, "plugin"), query_get(ctx.query, "channel"), query_get(ctx.query, "content_id"));
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "recheck_sources") {
            auto slug = query_get(ctx.query, "slug");
            if (slug.empty()) slug = query_get(ctx.query, "channel");
            auto data = proxy_.recheck_sources(slug);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "get_engines_status" || action == "engines_status") {
            auto data = proxy_.get_engines_status();
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "restart_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            auto data = proxy_.restart_engine(engine);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "toggle_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            bool enabled = query_get(ctx.query, "enabled", "true") == "true";
            auto data = proxy_.toggle_engine(engine, enabled);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        if (action == "set_main_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            auto data = proxy_.set_main_engine(engine);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;
        }
        std::string relative = "index.html";
        if (ctx.path != "/stat") {
            relative = ctx.path.substr(std::string("/stat/").size());
            if (relative.empty()) {
                relative = "index.html";
            }
        }
        if (!path_is_safe_relative(relative)) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
            return true;
        }
        try {
            auto full = std::filesystem::path(config_.root_dir) / "http" / relative;
            auto body = read_file_binary(full.string());
            send_bytes(ctx.connection, 200, mime_type_for_path(relative), body);
        } catch (...) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
        }
        return true;
    }
private:
    Config config_;
    Proxy& proxy_;
};

class StatpluginPlugin : public Plugin {
public:
    StatpluginPlugin(Config cfg, Proxy& proxy) : config_(std::move(cfg)), proxy_(proxy) {}
    std::string name() const override { return "statplugin"; }
    std::vector<std::string> handlers() const override { return {"statplugin"}; }
    bool handle(RequestContext& ctx) override {
        auto action = query_get(ctx.query, "action");

        // -----------------------------------------------------------------------
        // Acciones legacy (compatibilidad total con versiones anteriores)
        // -----------------------------------------------------------------------
        if (action == "get_plugins") {
            try {
                send_bytes(ctx.connection, 200, "application/json; charset=utf-8",
                           proxy_.plugins_json().dump(2));
            } catch (const std::exception& e) {
                Json::object res;
                res["status"] = "success";
                res["version"] = kAppVersion;
                res["plugins"] = Json::array{};
                res["total_plugins"] = 0.0;
                res["error"] = e.what();
                send_bytes(ctx.connection, 200, "application/json; charset=utf-8", Json(res).dump(2));
            }
            return true;

        } else if (action == "check_channel") {
            // Legacy: resolución vía AceClient TCP (mantener para compatibilidad)
            auto data = proxy_.check_channel_light(
                query_get(ctx.query, "plugin"),
                query_get(ctx.query, "channel"),
                query_get(ctx.query, "content_id"));
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "check_peers") {
            // Legacy: verificación de peers vía AceClient TCP
            int max_wait = 15;
            try { max_wait = std::stoi(query_get(ctx.query, "max_wait", "15")); } catch (...) {}
            max_wait = std::min(30, std::max(5, max_wait));
            auto data = proxy_.check_channel_peers(query_get(ctx.query, "content_id"), max_wait);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        // -----------------------------------------------------------------------
        // v08.24.02 — Nuevas acciones con Worker Pool asíncrono (4 fases)
        // -----------------------------------------------------------------------
        } else if (action == "verify") {
            // Verificación síncrona de un Content ID.
            // GET /statplugin?action=verify&content_id=<hash>[&timeout_ms=<ms>]
            auto cid = query_get(ctx.query, "content_id");
            int timeout_ms = 10000;
            try { timeout_ms = std::stoi(query_get(ctx.query, "timeout_ms", "10000")); } catch (...) {}
            timeout_ms = std::min(30000, std::max(3000, timeout_ms));
            auto data = proxy_.verify_channel(cid, timeout_ms);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "verify_batch") {
            // Verificación asíncrona de múltiples Content IDs.
            // GET /statplugin?action=verify_batch&ids=<h1>,<h2>,...
            auto ids_raw = query_get(ctx.query, "ids");
            std::vector<std::string> ids;
            for (auto id : split(ids_raw, ',', false)) {
                id = trim(id);
                if (!id.empty()) ids.push_back(id);
            }
            auto data = proxy_.verify_channels_batch(ids);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "get_health") {
            // Retorna el mapa completo de estados en memoria (para EPG / polling UI).
            // GET /statplugin?action=get_health
            auto data = proxy_.get_channel_health_map();
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "get_health_one") {
            // Retorna el estado de un único CID sin lanzar nueva verificación.
            // GET /statplugin?action=get_health_one&content_id=<hash>
            auto cid = query_get(ctx.query, "content_id");
            auto data = proxy_.get_channel_health_one(cid);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "network_diag" || action == "warp_status") {
            // Diagnóstico de nivel de protección de red (WARP, Tailscale, IP de salida, Ruta Segura).
            // GET /statplugin?action=network_diag o /statplugin?action=warp_status
            auto data = proxy_.get_network_diagnostics();
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));

        } else if (action == "warp_connect" || action == "warp_disconnect" || action == "warp_toggle") {
            // Control interactivo de Cloudflare WARP (connect / disconnect / toggle)
            bool warp_available = std::filesystem::exists("/usr/bin/warp-cli") || (::system("which warp-cli >/dev/null 2>&1") == 0);
            if (!warp_available) {
                Json::object err_obj;
                err_obj["status"] = "error";
                err_obj["message"] = "warp-cli is not accessible inside container";
                auto diag = proxy_.get_network_diagnostics();
                if (diag.is_object()) {
                    for (const auto& [k, v] : diag.as_object()) {
                        err_obj[k] = v;
                    }
                }
                send_bytes(ctx.connection, 200, "application/json; charset=utf-8", Json(err_obj).dump(2));
                return true;
            }

            if (action == "warp_connect") {
                ::system("warp-cli mode proxy && warp-cli proxy port 4001 && warp-cli connect >/dev/null 2>&1");
            } else if (action == "warp_disconnect") {
                auto ret = ::system("warp-cli disconnect >/dev/null 2>&1");
                (void)ret;
            } else if (action == "warp_toggle") {
                auto diag = proxy_.get_network_diagnostics();
                bool is_conn = diag.contains("warp_connected") ? diag["warp_connected"].as_bool(false) : (diag.contains("warp") && diag["warp"].is_object() && (diag["warp"]["status"].as_string() == "active" || diag["warp"]["status"].as_string() == "proxy"));
                if (is_conn) {
                    auto ret = ::system("warp-cli disconnect >/dev/null 2>&1");
                    (void)ret;
                } else {
                    ::system("warp-cli mode proxy && warp-cli proxy port 4001 && warp-cli connect >/dev/null 2>&1");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            auto data = proxy_.get_network_diagnostics();
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        // -----------------------------------------------------------------------
        // v08.30.02 — Re-comprobación forzada y Panel Gestor de Motores AceStream
        // -----------------------------------------------------------------------
        } else if (action == "recheck_sources") {
            auto slug = query_get(ctx.query, "slug");
            if (slug.empty()) slug = query_get(ctx.query, "channel");
            auto data = proxy_.recheck_sources(slug);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        } else if (action == "get_engines_status" || action == "engines_status") {
            auto data = proxy_.get_engines_status();
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        } else if (action == "restart_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            auto data = proxy_.restart_engine(engine);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        } else if (action == "toggle_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            bool enabled = query_get(ctx.query, "enabled", "true") == "true";
            auto data = proxy_.toggle_engine(engine, enabled);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        } else if (action == "set_main_engine") {
            auto engine = query_get(ctx.query, "engine");
            if (engine.empty()) engine = query_get(ctx.query, "name");
            auto data = proxy_.set_main_engine(engine);
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", data.dump(2));
            return true;

        } else {
            // Servir el frontend HTML del statplugin.
            try {
                auto full = std::filesystem::path(config_.root_dir) / "http" / "statplugin" / "index.html";
                send_bytes(ctx.connection, 200, "text/html; charset=utf-8", read_file_binary(full.string()));
            } catch (...) {
                send_bytes(ctx.connection, 500, "text/plain", "Internal Server Error");
            }
        }
        return true;
    }
private:
    Config config_;
    Proxy& proxy_;
};

class PlayerPlugin : public Plugin {
public:
    PlayerPlugin(Config cfg) : config_(std::move(cfg)) {}
    std::string name() const override { return "player"; }
    std::vector<std::string> handlers() const override { return {"player"}; }
    
    bool handle(RequestContext& ctx) override {
        std::string relative = "index.html";
        if (ctx.path != "/player") {
            if (ctx.path.find("/player/") == 0) {
                relative = ctx.path.substr(std::string("/player/").size());
            } else {
                relative = ctx.path;
            }
        } else {
            relative = "player/index.html";
        }
        
        if (relative.empty()) {
            relative = "index.html";
        }
        
        std::filesystem::path full;
        if (relative == "player/index.html") {
            full = std::filesystem::path(config_.root_dir) / "http" / "player" / "index.html";
        } else {
            full = std::filesystem::path(config_.root_dir) / "http" / "player" / relative;
        }

        try {
            auto body = read_file_binary(full.string());
            send_bytes(ctx.connection, 200, mime_type_for_path(full.filename().string()), body);
        } catch (...) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
        }
        return true;
    }
private:
    Config config_;
};

class ListasPlugin : public Plugin {
public:
    ListasPlugin(Config cfg) : config_(std::move(cfg)) {}
    std::string name() const override { return "listas"; }
    std::vector<std::string> handlers() const override { return {"listas"}; }
    
    bool handle(RequestContext& ctx) override {
        std::string relative = "index.html";
        if (ctx.path != "/listas") {
            if (starts_with(ctx.path, "/listas/")) {
                relative = ctx.path.substr(std::string("/listas/").size());
            }
        }
        if (relative.empty()) relative = "index.html";
        if (!path_is_safe_relative(relative)) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
            return true;
        }
        try {
            auto cfg_path = config_.get_config_dir() / "listas" / relative;
            auto full = std::filesystem::exists(cfg_path) ? cfg_path : (std::filesystem::path(config_.root_dir) / "http" / "listas" / relative);
            auto body = read_file_binary(full.string());
            send_bytes(ctx.connection, 200, mime_type_for_path(relative), body);
        } catch (...) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
        }
        return true;
    }
private:
    Config config_;
};

class FuentesPlugin : public Plugin {
public:
    FuentesPlugin(Config cfg) : config_(std::move(cfg)) {}
    std::string name() const override { return "fuentes"; }
    std::vector<std::string> handlers() const override { return {"fuentes"}; }
    
    bool handle(RequestContext& ctx) override {
        std::string relative = "index.html";
        if (ctx.path != "/fuentes") {
            if (starts_with(ctx.path, "/fuentes/")) {
                relative = ctx.path.substr(std::string("/fuentes/").size());
            }
        }
        if (relative.empty()) relative = "index.html";
        if (!path_is_safe_relative(relative)) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
            return true;
        }
        try {
            auto full = std::filesystem::path(config_.root_dir) / "http" / "fuentes" / relative;
            auto body = read_file_binary(full.string());
            send_bytes(ctx.connection, 200, mime_type_for_path(relative), body);
        } catch (...) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
        }
        return true;
    }
private:
    Config config_;
};

class EpgPlugin : public Plugin {
public:
    EpgPlugin(Config cfg, Proxy& proxy) : config_(std::move(cfg)), proxy_(proxy) {}
    std::string name() const override { return "epg"; }
    std::vector<std::string> handlers() const override { return {"epg"}; }
    
    bool handle(RequestContext& ctx) override {
        auto action = query_get(ctx.query, "action");
        if (action == "get_favorites") {
            auto favs = proxy_.get_epg_favorites();
            auto disabled = proxy_.get_disabled_candidates();
            Json::array arr;
            for (const auto& f : favs) arr.push_back(f);
            Json::array dis_arr;
            for (const auto& d : disabled) dis_arr.push_back(d);
            Json res = Json::object{
                {"status", "success"},
                {"count", static_cast<double>(favs.size())},
                {"favorites", Json(arr)},
                {"disabled_cids", Json(dis_arr)}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "set_favorites" || action == "set_favorites_order" || action == "reorder_favorites") {
            std::vector<std::string> favs;
            if (!ctx.request.body.empty()) {
                try {
                    auto j = Json::parse(ctx.request.body);
                    if (j.is_object()) {
                        if (j.contains("favorites") && j["favorites"].is_array()) {
                            for (const auto& el : j["favorites"].as_array()) {
                                if (el.is_string() && !el.as_string().empty()) {
                                    favs.push_back(el.as_string());
                                }
                            }
                        } else if (j.contains("favorites_order") && j["favorites_order"].is_array()) {
                            for (const auto& el : j["favorites_order"].as_array()) {
                                if (el.is_string() && !el.as_string().empty()) {
                                    favs.push_back(el.as_string());
                                }
                            }
                        }
                    } else if (j.is_array()) {
                        for (const auto& el : j.as_array()) {
                            if (el.is_string() && !el.as_string().empty()) {
                                favs.push_back(el.as_string());
                            }
                        }
                    }
                } catch (...) {}
            }
            if (favs.empty()) {
                auto favs_query = query_get(ctx.query, "favorites");
                if (favs_query.empty()) favs_query = query_get(ctx.query, "favorites_order");
                for (auto f : split(favs_query, ',', false)) {
                    f = trim(f);
                    if (!f.empty()) favs.push_back(f);
                }
            }
            proxy_.set_epg_favorites(favs);
            auto disabled = proxy_.get_disabled_candidates();
            Json::array arr;
            for (const auto& f : favs) arr.push_back(f);
            Json::array dis_arr;
            for (const auto& d : disabled) dis_arr.push_back(d);
            Json res = Json::object{
                {"status", "success"},
                {"count", static_cast<double>(favs.size())},
                {"favorites", Json(arr)},
                {"disabled_cids", Json(dis_arr)}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "toggle_candidate") {
            std::string cid = query_get(ctx.query, "content_id");
            std::string dis_str = query_get(ctx.query, "disabled");
            if (cid.empty() && !ctx.request.body.empty()) {
                try {
                    auto j = Json::parse(ctx.request.body);
                    if (j.is_object()) {
                        if (j.contains("content_id")) cid = j["content_id"].as_string();
                        if (j.contains("disabled")) dis_str = j["disabled"].as_bool() ? "true" : "false";
                    }
                } catch (...) {}
            }
            bool is_dis = (dis_str == "true" || dis_str == "1");
            bool now_disabled = proxy_.toggle_disabled_candidate(cid, is_dis);
            Json res = Json::object{
                {"status", "success"},
                {"content_id", cid},
                {"disabled", now_disabled}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "get_custom_logos") {
            auto logos = proxy_.get_custom_logos();
            Json::object obj;
            for (const auto& [k, v] : logos) {
                obj[k] = v;
            }
            Json res = Json::object{
                {"status", "success"},
                {"logos", Json(obj)}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "set_custom_logo") {
            std::string chan = query_get(ctx.query, "channel");
            if (chan.empty()) chan = query_get(ctx.query, "slug");
            std::string logo = query_get(ctx.query, "logo");
            if ((chan.empty() || logo.empty()) && !ctx.request.body.empty()) {
                try {
                    auto j = Json::parse(ctx.request.body);
                    if (j.is_object()) {
                        if (j.contains("channel")) chan = j["channel"].as_string();
                        if (j.contains("slug")) chan = j["slug"].as_string();
                        if (j.contains("logo")) logo = j["logo"].as_string();
                        if (j.contains("logos") && j["logos"].is_object()) {
                            for (const auto& [k, v] : j["logos"].as_object()) {
                                if (v.is_string()) proxy_.set_custom_logo(k, v.as_string());
                            }
                        }
                    }
                } catch (...) {}
            }
            if (!chan.empty()) {
                proxy_.set_custom_logo(chan, logo);
            }
            auto logos = proxy_.get_custom_logos();
            Json::object obj;
            for (const auto& [k, v] : logos) {
                obj[k] = v;
            }
            Json res = Json::object{
                {"status", "success"},
                {"channel", chan},
                {"logo", logo},
                {"logos", Json(obj)}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "remove_custom_logo") {
            std::string chan = query_get(ctx.query, "channel");
            if (chan.empty()) chan = query_get(ctx.query, "slug");
            if (chan.empty() && !ctx.request.body.empty()) {
                try {
                    auto j = Json::parse(ctx.request.body);
                    if (j.is_object() && j.contains("channel")) chan = j["channel"].as_string();
                } catch (...) {}
            }
            if (!chan.empty()) {
                proxy_.remove_custom_logo(chan);
            }
            Json res = Json::object{
                {"status", "success"},
                {"removed", chan}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "get_channel_logos") {
            std::string chan = query_get(ctx.query, "channel");
            if (chan.empty()) chan = query_get(ctx.query, "slug");
            auto logos_list = proxy_.get_all_logos_for_channel(chan);
            auto active_custom = proxy_.get_custom_logo_for_channel(chan);
            Json::array arr;
            for (const auto& l : logos_list) {
                arr.push_back(l);
            }
            Json res = Json::object{
                {"status", "success"},
                {"channel", chan},
                {"current_custom_logo", active_custom},
                {"logos", Json(arr)}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "get_channel_filters") {
            std::string chan = query_get(ctx.query, "channel");
            if (chan.empty()) chan = query_get(ctx.query, "slug");
            auto filters = proxy_.get_channel_filters();
            Json::object obj;
            for (const auto& [k, v] : filters) {
                Json::array arr;
                for (const auto& p : v) arr.push_back(p);
                obj[k] = arr;
            }
            Json::object res_obj{
                {"status", "success"},
                {"count", static_cast<double>(filters.size())},
                {"filters", Json(obj)}
            };
            if (!chan.empty()) {
                auto pats = proxy_.get_channel_filters_for_slug(chan);
                Json::array p_arr;
                for (const auto& p : pats) p_arr.push_back(p);
                res_obj["slug"] = canonical_slug(chan);
                res_obj["patterns"] = p_arr;
                res_obj["rule"] = pats.empty() ? "" : pats[0];
            }
            Json res = res_obj;
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        } else if (action == "set_channel_filter" || action == "save_channel_filter") {
            std::string chan = query_get(ctx.query, "channel");
            if (chan.empty()) chan = query_get(ctx.query, "slug");
            std::vector<std::string> patterns;

            if (!ctx.request.body.empty()) {
                try {
                    auto j = Json::parse(ctx.request.body);
                    if (j.is_object()) {
                        auto obj = j.as_object();
                        if (chan.empty()) {
                            if (obj.contains("slug") && obj.at("slug").is_string()) chan = obj.at("slug").as_string();
                            else if (obj.contains("channel") && obj.at("channel").is_string()) chan = obj.at("channel").as_string();
                        }
                        if (obj.contains("rules") && obj.at("rules").is_array()) {
                            for (const auto& el : obj.at("rules").as_array()) {
                                if (el.is_string() && !el.as_string().empty()) patterns.push_back(el.as_string());
                            }
                        } else if (obj.contains("patterns") && obj.at("patterns").is_array()) {
                            for (const auto& el : obj.at("patterns").as_array()) {
                                if (el.is_string() && !el.as_string().empty()) patterns.push_back(el.as_string());
                            }
                        } else if (obj.contains("rule") && obj.at("rule").is_string()) {
                            patterns.push_back(obj.at("rule").as_string());
                        } else if (obj.contains("regex") && obj.at("regex").is_string()) {
                            patterns.push_back(obj.at("regex").as_string());
                        }
                    }
                } catch (...) {}
            }

            if (patterns.empty()) {
                auto q_rule = query_get(ctx.query, "rule");
                if (q_rule.empty()) q_rule = query_get(ctx.query, "regex");
                if (q_rule.empty()) q_rule = query_get(ctx.query, "pattern");
                if (!q_rule.empty()) patterns.push_back(url_decode(q_rule));
            }

            bool remove_req = query_get(ctx.query, "action") == "remove_channel_filter" || query_get(ctx.query, "delete") == "true";
            if (remove_req && !chan.empty()) {
                proxy_.remove_channel_filter(chan);
            } else if (!chan.empty()) {
                proxy_.set_channel_filter(chan, patterns);
            }

            Json::array p_arr;
            for (const auto& p : patterns) p_arr.push_back(p);
            Json res = Json::object{
                {"status", "success"},
                {"slug", canonical_slug(chan)},
                {"patterns", Json(p_arr)},
                {"message", remove_req ? "Regla eliminada" : "Regla guardada correctamente"}
            };
            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", res.dump(2));
            return true;
        }

        std::string relative = "index.html";
        if (ctx.path != "/epg") {
            if (starts_with(ctx.path, "/epg/")) {
                relative = ctx.path.substr(std::string("/epg/").size());
            }
        }
        if (relative.empty()) relative = "index.html";
        if (!path_is_safe_relative(relative)) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
            return true;
        }
        try {
            auto full = std::filesystem::path(config_.root_dir) / "http" / "epg" / relative;
            auto body = read_file_binary(full.string());
            send_bytes(ctx.connection, 200, mime_type_for_path(relative), body);
        } catch (...) {
            send_bytes(ctx.connection, 404, "text/plain", "Not Found");
        }
        return true;
    }
private:
    Config config_;
    Proxy& proxy_;
};

class CustomListPlugin : public PlaylistPlugin {
public:
    CustomListPlugin(Config cfg, HttpClient& client, Proxy& proxy, std::string name, std::string url)
        : PlaylistPlugin(std::move(cfg), client, proxy, name, PlaylistGenerator::epg_header("", 0), 60),
          custom_url_(std::move(url)) {}

    void set_custom_url(std::string url) {
        custom_url_ = std::move(url);
    }
protected:
    bool refresh() override {
        if (!is_enabled()) {
            return false;
        }
        auto configured_url = proxy_.get_plugin_url(name(), custom_url_);
        std::vector<std::string> raw_urls = split(configured_url, ',', false);
        if (raw_urls.empty()) raw_urls.push_back(configured_url);

        PlaylistGenerator playlist(header_);
        std::map<std::string, std::string> channels;
        std::map<std::string, std::string> picons;

        for (auto raw_url : raw_urls) {
            raw_url = trim(raw_url);
            if (raw_url.empty()) continue;
            auto url = normalize_list_url(raw_url);
            if (url.find("/ipns/") != std::string::npos && (ends_with(url, "/") || (url.find(".m3u") == std::string::npos && url.find(".json") == std::string::npos && url.find(".txt") == std::string::npos))) {
                if (ends_with(url, "/")) url += "hashes.json";
                else url += "/hashes.json";
            }

            try {
                std::string content;
                if (starts_with(url, "/") || starts_with(url, "file://")) {
                    std::filesystem::path local_path;
                    if (starts_with(url, "file://")) {
                        local_path = url.substr(7);
                    } else {
                        local_path = std::filesystem::path(config_.root_dir) / "http" / url.substr(1);
                    }
                    content = read_file_binary(local_path.string());
                } else {
                    url = replace_all(url, " ", "%20");
                    auto response = http_client_.get(url, {{"User-Agent", kBrowserUserAgent}}, 60);
                    if (response.body.empty() && url.find("/hashes.json") != std::string::npos) {
                        // Fallback to hashes_acestream.m3u
                        auto m3u_url = replace_all(url, "/hashes.json", "/hashes_acestream.m3u");
                        response = http_client_.get(m3u_url, {{"User-Agent", kBrowserUserAgent}}, 60);
                    }
                    content = response.body;
                }

                std::string trimmed_content = trim(content);
                if (starts_with(trimmed_content, "{") || starts_with(trimmed_content, "[")) {
                    try {
                        auto j = Json::parse(trimmed_content);
                        std::function<void(const Json&, const std::string&)> process_json = [&](const Json& node, const std::string& current_grp) {
                            if (node.is_object()) {
                                std::string grp = node.contains("group") ? node["group"].as_string() : (node.contains("category") ? node["category"].as_string() : (node.contains("name") ? node["name"].as_string() : current_grp));
                                if (grp.empty()) grp = "Otros";

                                if ((node.contains("hash") || node.contains("url") || node.contains("id")) && (node.contains("title") || node.contains("name"))) {
                                    auto st_name = node.contains("title") ? node["title"].as_string() : node["name"].as_string();
                                    auto raw_hash = node.contains("hash") ? node["hash"].as_string() : (node.contains("url") ? node["url"].as_string() : node["id"].as_string());
                                    auto logo = node.contains("logo") ? node["logo"].as_string() : (node.contains("image") ? node["image"].as_string() : "");
                                    auto tvg_id = node.contains("tvg_id") ? node["tvg_id"].as_string() : (node.contains("tvg-id") ? node["tvg-id"].as_string() : st_name);

                                    if (!st_name.empty() && !raw_hash.empty()) {
                                        std::string st_url = (starts_with(raw_hash, "acestream://") || starts_with(raw_hash, "http://") || starts_with(raw_hash, "https://")) ? raw_hash : ("acestream://" + raw_hash);
                                        PlaylistItem item{st_name, url_encode(st_name, ""), grp, st_name, tvg_id, logo};
                                        channels[st_name] = st_url;
                                        if (!logo.empty()) picons[st_name] = logo;
                                        playlist.add_item(item);
                                    }
                                }

                                if (node.contains("hashes") && node["hashes"].is_array()) {
                                    for (const auto& h : node["hashes"].as_array()) {
                                        process_json(h, grp);
                                    }
                                }
                                if (node.contains("stations") && node["stations"].is_array()) {
                                    for (const auto& station : node["stations"].as_array()) {
                                        process_json(station, grp);
                                    }
                                }
                                if (node.contains("groups") && node["groups"].is_array()) {
                                    for (const auto& g : node["groups"].as_array()) {
                                        process_json(g, grp);
                                    }
                                }
                                if (node.contains("channels") && node["channels"].is_array()) {
                                    for (const auto& ch : node["channels"].as_array()) {
                                        process_json(ch, grp);
                                    }
                                }
                            } else if (node.is_array()) {
                                for (const auto& el : node.as_array()) {
                                    process_json(el, current_grp);
                                }
                            }
                        };
                        process_json(j, "");
                    } catch (...) {}
                }

                if (channels.empty() && !starts_with(trimmed_content, "#EXTM3U") && !starts_with(trimmed_content, "#EXTINF")) {
                    auto lines = split(content, '\n', true);
                    std::string cur_grp = "Otros";
                    std::string pending_name;
                    for (auto& l : lines) {
                        auto line = trim(l);
                        if (line.empty() || starts_with(line, "#") || starts_with(line, "//") || starts_with(line, "===") || starts_with(line, "Total:") || starts_with(line, "Generado:") || starts_with(line, "Identificadores")) {
                            if (starts_with(line, "===") && ends_with(line, "===") && line.length() > 6) {
                                cur_grp = trim(line.substr(3, line.length() - 6));
                            }
                            continue;
                        }
                        if (starts_with(line, "acestream://") || starts_with(line, "infohash://") || std::regex_match(line, std::regex(R"([a-fA-F0-9]{40})"))) {
                            std::string st_name = pending_name.empty() ? ("Canal " + std::to_string(channels.size() + 1)) : pending_name;
                            std::string st_url = (starts_with(line, "acestream://") || starts_with(line, "infohash://")) ? line : ("acestream://" + line);
                            PlaylistItem item{st_name, url_encode(st_name, ""), cur_grp, st_name, "", ""};
                            channels[st_name] = st_url;
                            playlist.add_item(item);
                            pending_name.clear();
                        } else {
                            pending_name = line;
                        }
                    }
                }

                if (channels.empty()) {
                    for (auto& item : parse_m3u_acestream_items(content, channels, picons)) {
                        playlist.add_item(item);
                    }
                }
            } catch (const std::exception& e) {
                log_line("ERROR", "[" + name() + "] failed to download playlist " + url + ": " + e.what());
            }
        }

        set_playlist(std::move(playlist), std::move(channels), std::move(picons));
        log_line("INFO", "[" + name() + "] dynamic playlist generated with " + std::to_string(channel_count()) + " channels");
        return true;
    }
private:
    std::string custom_url_;
};

std::shared_ptr<Plugin> create_custom_list_plugin_helper(Config config, HttpClient& http_client, Proxy& proxy, const std::string& name, const std::string& url) {
    return std::make_shared<CustomListPlugin>(std::move(config), http_client, proxy, name, url);
}

std::vector<std::shared_ptr<Plugin>> create_plugins(Config config, HttpClient& http_client, Proxy& proxy) {
    std::vector<std::shared_ptr<Plugin>> plugins;
    auto add = [&](const std::string& name, const std::function<std::shared_ptr<Plugin>()>& factory) {
        if (config.plugin_enabled(name)) {
            auto plugin = factory();
            plugins.push_back(std::move(plugin));
            log_line("INFO", "enabled plugin: " + name);
        }
    };
    add("newera", [&] { return std::make_shared<NewEraPlugin>(config, http_client, proxy); });
    add("elcano", [&] { return std::make_shared<ElcanoPlugin>(config, http_client, proxy); });
    add("acepl", [&] { return std::make_shared<AcePLPlugin>(config, http_client, proxy); });
    add("af1c1onados", [&] { return std::make_shared<Af1c1onadosPlugin>(config, http_client, proxy); });
    add("aio", [&] { return std::make_shared<AioPlugin>(config, proxy); });
    add("stat", [&] { return std::make_shared<StatPlugin>(config, proxy); });
    add("statplugin", [&] { return std::make_shared<StatpluginPlugin>(config, proxy); });

    plugins.push_back(std::make_shared<PlayerPlugin>(config));
    plugins.push_back(std::make_shared<ListasPlugin>(config));
    plugins.push_back(std::make_shared<FuentesPlugin>(config));
    plugins.push_back(std::make_shared<EpgPlugin>(config, proxy));

    // Dynamic Custom Lists
    auto state = proxy.plugins_state_json();
    if (state.is_object() && state.contains("custom_lists") && state["custom_lists"].is_array()) {
        for (const auto& item : state["custom_lists"].as_array()) {
            if (item.is_object() && item.contains("name") && item.contains("url")) {
                auto name = item["name"].as_string();
                auto url = item["url"].as_string();
                if (is_valid_source_url(url)) {
                    plugins.push_back(std::make_shared<CustomListPlugin>(config, http_client, proxy, name, url));
                }
            }
        }
    }

    for (auto& plugin : plugins) {
        if (auto playlist = std::dynamic_pointer_cast<PlaylistPlugin>(plugin)) {
            playlist->refresh_if_needed();
        }
    }
    return plugins;
}

} // namespace httpace
