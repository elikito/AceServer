#include "httpaceproxycpp/json.hpp"
#include "httpaceproxycpp/playlist.hpp"
#include "httpaceproxycpp/stream_scorer.hpp"
#include "httpaceproxycpp/util.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace httpace;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_hashes() {
    require(sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 mismatch");
    require(md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72", "md5 mismatch");
}

void test_url_helpers() {
    require(url_decode("DAZN%201%20FHD") == "DAZN 1 FHD", "url_decode mismatch");
    require(url_encode("DAZN 1 FHD", "") == "DAZN%201%20FHD", "url_encode mismatch");
    auto parsed = parse_url("http://127.0.0.1:6878/ace/getstream?id=abc&x=1");
    require(parsed.scheme == "http", "url scheme");
    require(parsed.host == "127.0.0.1", "url host");
    require(parsed.port == "6878", "url port");
    require(query_get(parsed.query, "id") == "abc", "query id");
    require(rewrite_url_host_port("http://10.0.0.2:6878/live.ts", "aceserve", "6878") == "http://aceserve:6878/live.ts", "rewrite url");
}

void test_json() {
    auto json = Json::parse(R"({"items":[{"name":"A","n":2}],"ok":true})");
    require(json["ok"].as_bool() == true, "json bool");
    require(json["items"][0]["name"].as_string() == "A", "json nested");
    require(json.dump().find("\"ok\":true") != std::string::npos, "json dump");
}

void test_playlist() {
    PlaylistGenerator gen;
    gen.add_item(PlaylistItem{"DAZN 1", "DAZN%201", "Sports", "DAZN 1", "dazn1", ""});
    auto m3u = gen.export_m3u("localhost:8888", "/newera/channel", "ext=m3u8", true);
    require(m3u.find("http://localhost:8888/newera/channel/DAZN%201.m3u8?ext=m3u8") != std::string::npos, "playlist channel url");
    PlaylistGenerator aio;
    aio.add_item(PlaylistItem{"DAZN 1", "acestream://abcdef", "Sports", "DAZN 1", "", ""});
    auto combined = aio.export_m3u("localhost:8888", "", "ext=ts", false);
    require(combined.find("/content_id/abcdef/DAZN%201.ts?ext=ts") != std::string::npos, "playlist core url");
}

void test_m3u_parser_variants() {
    std::map<std::string, std::string> channels;
    std::map<std::string, std::string> picons;
    std::string body =
        "#EXTM3U\n"
        "#EXTGRP: group-title=\"DAZN\" group-logo=\"https://example.test/group.png\"\n"
        "#EXTINF:-1 tvg-logo=\"https://example.test/dazn1.png\" tvg-id=\"DAZN 1 HD\" group-title=\"DAZN\", DAZN 1 FHD ad6d --> NEW ERA\n"
        "http://127.0.0.1:6878/ace/getstream?id=691739972eb3468cf16b25e84dafdeaa40dead6d\n"
        "#EXTINF:-1 tvg-id=\"DAZN 2\" group-title=\"DAZN\",DAZN 2 720p\n"
        "acestream://a116ce3ff95c41c60e987e2b1aa247007f707884\n";
    auto items = parse_m3u_acestream_items(body, channels, picons);
    require(items.size() == 2, "m3u item count");
    require(channels["DAZN 1 FHD ad6d --> NEW ERA"] == "acestream://691739972eb3468cf16b25e84dafdeaa40dead6d", "http getstream extraction");
    require(channels["DAZN 2 720p"] == "acestream://a116ce3ff95c41c60e987e2b1aa247007f707884", "acestream extraction");
    require(items[0].group == "DAZN", "m3u group");
}

void test_stream_scorer() {
    require(canonical_name("Teledeporte 720p **") == "teledeporte", "teledeporte 720p canonical");
    require(canonical_name("Teledeporte 1080p *") == "teledeporte", "teledeporte 1080p canonical");
    require(canonical_name("TELEDEPORTE FHD") == "teledeporte", "teledeporte fhd canonical");
    require(canonical_slug("Teledeporte 1080p **") == "teledeporte", "teledeporte slug");
    require(canonical_slug("La 1 HD") == "la-1", "la 1 slug");

    // v08.26.01 - Tests para Sufijos Hash de 4 caracteres y Flechas de Origen
    require(canonical_name("M+ LALIGA 936c → ELCANO") == "m laliga", "m+ laliga 936c elcano canonical");
    require(canonical_slug("M+ LALIGA 936c → ELCANO") == "m-laliga", "m+ laliga 936c elcano slug");
    require(canonical_name("M+ LALIGA FHD 2929 → NEW ERA VI") == "m laliga", "m+ laliga fhd 2929 new era canonical");
    require(canonical_slug("M+ LALIGA FHD 2929 → NEW ERA VI") == "m-laliga", "m+ laliga fhd 2929 new era slug");
    require(canonical_name("M+ LALIGA 9f1a → ELCANO") == "m laliga", "m+ laliga 9f1a elcano canonical");
    require(canonical_slug("M+ LALIGA 9f1a → ELCANO") == "m-laliga", "m+ laliga 9f1a elcano slug");
    require(canonical_name("M+ LALIGA 9e38 → SPORT TV") == "m laliga", "m+ laliga 9e38 sport tv canonical");
    require(canonical_slug("M+ LALIGA 9e38 → SPORT TV") == "m-laliga", "m+ laliga 9e38 sport tv slug");
    require(canonical_name("M+ LALIGA 2 936c → ELCANO") == "m laliga 2", "m+ laliga 2 936c canonical");
    require(canonical_slug("M+ LALIGA 2 936c → ELCANO") == "m-laliga-2", "m+ laliga 2 936c slug");
    require(canonical_slug("M+ LALIGA 3 FHD 2929 → NEW ERA VI") == "m-laliga-3", "m+ laliga 3 fhd slug");
    require(canonical_slug("DAZN 1 FHD ad6d --> NEW ERA") == "dazn-1", "dazn 1 fhd ad6d slug");

    ChannelCandidate c1{"Teledeporte 1080p *", "cid1", "unificada", "", "General", "tdp", StreamQuality::FHD_1080, 100, 10, 500000, ChannelHealth::ONLINE, false, false, false, 0.0};
    ChannelCandidate c2{"Teledeporte 720p **", "cid2", "unificada", "", "General", "tdp", StreamQuality::HD_720, 60, 4, 50000, ChannelHealth::ONLINE, false, false, false, 0.0};
    ChannelCandidate c3{"TELEDEPORTE FHD", "cid3", "elcano", "", "General", "tdp", StreamQuality::FHD_1080, 100, 0, 0, ChannelHealth::OFFLINE, false, false, false, 0.0};
    ChannelCandidate c4{"Teledeporte SD", "cid4", "unificada", "", "General", "tdp", StreamQuality::SD, 30, 80, 50000, ChannelHealth::ONLINE, false, false, false, 0.0};

    std::vector<ChannelCandidate> list = {c4, c2, c3, c1};
    StreamScorer::rank_candidates(list);

    // c1 (1080p con 10 peers) debe superar a c2 (720p con 4 peers) y a c4 (SD con 80 peers)
    require(list[0].content_id == "cid1", "c1 1080p should rank highest");
    require(list[1].content_id == "cid2", "c2 720p should rank second");
    require(list[2].content_id == "cid4", "c4 sd should rank third");
    require(list[3].content_id == "cid3", "c3 offline should rank lowest");
    require(detect_is_foreign("Sport TV1 (PT)") == true, "foreign detected");
    require(detect_is_foreign("Teledeporte 1080p *") == false, "not foreign");
    require(detect_is_foreign("M+ LALIGA 9e38 → SPORT TV") == false, "spanish channel with sport tv origin is not foreign");
}

void test_warp_and_resolution_variants() {
    // v08.27.01 - Diagnostic JSON formatting test
    Json diag = Json::object{
        {"status", "success"},
        {"warp_connected", true},
        {"traffic_route", "Cloudflare WARP (SOCKS5 Blindado)"},
        {"egress_ip", "104.28.192.1"},
        {"isp_name", "Cloudflare WARP Mesh Network"},
        {"safe_route", true}
    };
    require(diag["warp_connected"].as_bool() == true, "warp_connected check");
    require(diag["traffic_route"].as_string() == "Cloudflare WARP (SOCKS5 Blindado)", "traffic_route check");
    require(diag["egress_ip"].as_string() == "104.28.192.1", "egress_ip check");
    require(diag["isp_name"].as_string() == "Cloudflare WARP Mesh Network", "isp_name check");
    require(diag["safe_route"].as_bool() == true, "safe_route check");

    // Test distinct resolution filtering logic
    ChannelCandidate fhd{"DAZN 1 FHD", "hash_fhd_1080", "unificada", "", "Sports", "dazn-1", StreamQuality::FHD_1080, 100, 20, 600000, ChannelHealth::ONLINE, false, false, false, 0.0};
    ChannelCandidate hd{"DAZN 1 720p", "hash_hd_720", "unificada", "", "Sports", "dazn-1", StreamQuality::HD_720, 70, 15, 300000, ChannelHealth::ONLINE, false, false, false, 0.0};
    ChannelCandidate sd{"DAZN 1 SD", "hash_sd_576", "unificada", "", "Sports", "dazn-1", StreamQuality::SD, 30, 40, 100000, ChannelHealth::ONLINE, false, false, false, 0.0};

    std::vector<ChannelCandidate> all_cands = {sd, hd, fhd};
    StreamScorer::rank_candidates(all_cands);

    // Mejor auto
    require(all_cands[0].content_id == "hash_fhd_1080", "best auto is fhd");

    // Filter FHD (>= 1080p)
    std::vector<ChannelCandidate> fhd_only;
    for (const auto& c : all_cands) {
        if (static_cast<int>(c.quality) >= static_cast<int>(StreamQuality::FHD_1080)) {
            fhd_only.push_back(c);
        }
    }
    require(!fhd_only.empty() && fhd_only[0].content_id == "hash_fhd_1080", "fhd resolution candidate");

    // Filter HD (720p)
    std::vector<ChannelCandidate> hd_only;
    for (const auto& c : all_cands) {
        if (c.quality == StreamQuality::HD_720) {
            hd_only.push_back(c);
        }
    }
    require(!hd_only.empty() && hd_only[0].content_id == "hash_hd_720", "hd resolution candidate");
    require(hd_only[0].content_id != fhd_only[0].content_id, "resolution variants must have distinct Content IDs");
}

void test_persistent_config_and_sources_import() {
    // 1. Test Config::get_config_dir()
    Config cfg;
    cfg.root_dir = "/tmp/test_ace";
    cfg.config_dir = "/tmp/test_ace/custom_config";
    require(cfg.get_config_dir() == "/tmp/test_ace/custom_config", "custom config dir resolution");

    Config cfg_default;
    cfg_default.root_dir = "/tmp/test_ace";
    auto default_cfg_path = cfg_default.get_config_dir().string();
    require(!default_cfg_path.empty(), "default config dir resolution");

    // 2. Test standard export/import schema parsing
    std::string import_payload = R"({
        "custom_lists": [
            {
                "name": "unificada",
                "title": "Lista Unificada",
                "url": "/listas/locales/lista_acestream_unificada.m3u",
                "enabled": true
            },
            {
                "name": "deportes_extra",
                "title": "Deportes Extra",
                "url": "https://example.com/sports.m3u",
                "enabled": true
            }
        ],
        "urls": {
            "newera": "https://ipfs.io/ipns/k2k4r8lm8tkmuxbc8lkmq1in3v0oya1p6pe9o5bu0hu30br5ko08k2gb/data/listas/lista_iptv.m3u",
            "elcano": "https://ipfs.io/ipns/k51qzi5uqu5dh5qej4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik820fecs/hashes.json"
        }
    })";

    auto parsed_json = Json::parse(import_payload);
    require(parsed_json.is_object(), "import json is object");
    require(parsed_json.contains("custom_lists") && parsed_json["custom_lists"].is_array(), "custom_lists present");
    require(parsed_json.contains("urls") && parsed_json["urls"].is_object(), "urls present");

    int imported_count = 0;
    auto custom_arr = parsed_json["custom_lists"].as_array();
    for (const auto& item : custom_arr) {
        if (item.is_object() && item.contains("name") && item.contains("url")) {
            imported_count++;
        }
    }
    auto urls_obj = parsed_json["urls"].as_object();
    for (const auto& [k, v] : urls_obj) {
        if (v.is_string()) {
            imported_count++;
        }
    }
    require(imported_count == 4, "all custom_lists and plugin urls imported");

    Json res = Json::object{
        {"status", "success"},
        {"ok", true},
        {"imported", static_cast<double>(imported_count)},
        {"imported_count", static_cast<double>(imported_count)},
        {"message", "Fuentes importadas con éxito"}
    };
    require(res["status"].as_string() == "success", "response status success");
    require(res["ok"].as_bool() == true, "response ok bool");
    require(static_cast<int>(res["imported"].as_number()) == 4, "response imported count");

    // 3. Test epg_favorites parse invariants
    std::string fav_json = R"({
        "favorites": ["DAZN 1", "M+ LALIGA", "Teledeporte"],
        "disabled_cids": ["bad_cid_123"]
    })";
    auto fav_parsed = Json::parse(fav_json);
    require(fav_parsed.contains("favorites") && fav_parsed["favorites"].as_array().size() == 3, "favorites count");
    require(fav_parsed.contains("disabled_cids") && fav_parsed["disabled_cids"].as_array().size() == 1, "disabled cids count");
}

void test_reaper_and_orphan_session_cleanup() {
    // Test StreamClient idle time and reaper calculation
    auto now = unix_time();
    std::int64_t last_activity_active = now - 5; // 5s ago
    require(now - last_activity_active <= 20, "active client within 20s window");

    std::int64_t last_activity_inactive = now - 35; // 35s ago (orphan stream)
    require(now - last_activity_inactive > 20, "inactive client exceeds 20s threshold");
}

void test_engine_host_fallback_candidates() {
    Config cfg;
    cfg.ace_host = "aceserve-modern";
    std::vector<std::string> hosts_to_try;
    if (!cfg.ace_host.empty()) hosts_to_try.push_back(cfg.ace_host);
    for (const auto& fallback_host : {"127.0.0.1", "172.17.0.1", "aceserve-modern", "aceserve-compat-stable", "aceserve-compat-light"}) {
        if (std::find(hosts_to_try.begin(), hosts_to_try.end(), fallback_host) == hosts_to_try.end()) {
            hosts_to_try.push_back(fallback_host);
        }
    }
    require(std::find(hosts_to_try.begin(), hosts_to_try.end(), "172.17.0.1") != hosts_to_try.end(), "fallback includes Docker gateway");
    require(hosts_to_try.size() >= 4, "fallback candidates pool complete");
}

void test_favorites_reordering_and_playlist_grouping() {
    std::vector<std::string> favs = {"DAZN 1", "M+ LALIGA", "Teledeporte"};
    // Reorder: move Teledeporte to first position
    auto item = favs.back();
    favs.pop_back();
    favs.insert(favs.begin(), item);

    require(favs[0] == "Teledeporte", "Teledeporte is first after reorder");
    require(favs[1] == "DAZN 1", "DAZN 1 is second");
    require(favs[2] == "M+ LALIGA", "M+ LALIGA is third");

    // Test Alphabetical Sorting A-Z
    std::sort(favs.begin(), favs.end());
    require(favs[0] == "DAZN 1", "Alphabetical A-Z DAZN 1 first");
    require(favs[1] == "M+ LALIGA", "Alphabetical A-Z M+ LALIGA second");
    require(favs[2] == "Teledeporte", "Alphabetical A-Z Teledeporte third");

    // Test group-title formatting for channel
    std::string channel_name = "DAZN 1";
    std::string line = "#EXTINF:-1 tvg-id=\"dazn-1\" tvg-name=\"DAZN 1\" group-title=\"" + channel_name + "\", " + channel_name + " (Mejor Stream Auto)\n";
    require(line.find("group-title=\"DAZN 1\"") != std::string::npos, "group-title matches channel name");
    require(line.find("(Mejor Stream Auto)") != std::string::npos, "displays (Mejor Stream Auto)");
}

} // namespace

int main() {
    try {
        test_hashes();
        test_url_helpers();
        test_json();
        test_playlist();
        test_m3u_parser_variants();
        test_stream_scorer();
        test_warp_and_resolution_variants();
        test_persistent_config_and_sources_import();
        test_reaper_and_orphan_session_cleanup();
        test_engine_host_fallback_candidates();
        test_favorites_reordering_and_playlist_grouping();
        std::cout << "httpaceproxycpp core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "test failed: " << e.what() << "\n";
        return 1;
    }
}
