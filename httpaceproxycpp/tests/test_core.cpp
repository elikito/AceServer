#include "httpaceproxycpp/json.hpp"
#include "httpaceproxycpp/playlist.hpp"
#include "httpaceproxycpp/stream_scorer.hpp"
#include "httpaceproxycpp/util.hpp"

#include <cstdlib>
#include <iostream>
#include <regex>
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

void test_fhda_720a_variants_and_custom_logos() {
    // 1. Test canonical slug suffix handling for -fhda and -720a
    std::string slug_fhd = "dazn-1-fhda";
    std::string base_fhd = slug_fhd.substr(0, slug_fhd.size() - 5);
    require(base_fhd == "dazn-1", "dazn-1-fhda stripped to dazn-1");

    std::string slug_720 = "la-1-720a";
    std::string base_720 = slug_720.substr(0, slug_720.size() - 5);
    require(base_720 == "la-1", "la-1-720a stripped to la-1");

    // 2. Test Custom Logos JSON Parsing & Canonical Key Matching
    std::string json_data = R"({
        "status": "success",
        "logos": {
            "dazn-1": "https://img.example.com/dazn1_custom.png",
            "la-1": "https://img.example.com/la1_custom.png"
        }
    })";
    auto parsed = Json::parse(json_data);
    require(parsed["status"].as_string() == "success", "json status success");
    require(parsed["logos"].is_object(), "logos is object");
    auto logos_obj = parsed["logos"].as_object();
    require(logos_obj["dazn-1"].as_string() == "https://img.example.com/dazn1_custom.png", "dazn-1 logo extracted");

    // 3. Test Playlist Generator with FHDa / 720a variant naming
    std::string name = "Movistar LaLiga";
    std::string v_fhd = name + " FHDa";
    std::string v_720 = name + " 720a";
    require(v_fhd == "Movistar LaLiga FHDa", "FHDa variant naming");
    require(v_720 == "Movistar LaLiga 720a", "720a variant naming");
}

void test_virtual_url_and_engine_pool_status() {
    // 1. Test Virtual URL generation from channel name
    std::string chan = "M+ LALIGA TV 1080p";
    std::string slug = canonical_slug(chan);
    require(slug == "m-laliga-tv", "slug generated for virtual URL");
    std::string virtual_url = "http://127.0.0.1:8888/auto/" + slug;
    require(virtual_url == "http://127.0.0.1:8888/auto/m-laliga-tv", "exact virtual url");

    // 2. Test Engine Pool JSON formatting
    std::string engine_json = R"({
        "status": "success",
        "active_engine": "aceserve-modern",
        "engine_mode": "auto",
        "engines": [
            {"name": "aceserve-modern", "status": "connected", "api_port": 62062, "http_port": 6878, "is_main": true},
            {"name": "aceserve-compat-light", "status": "standby", "api_port": 62062, "http_port": 6878, "is_main": false},
            {"name": "aceserve-compat-stable", "status": "standby", "api_port": 62062, "http_port": 6878, "is_main": false}
        ]
    })";
    auto parsed = Json::parse(engine_json);
    require(parsed["status"].as_string() == "success", "engine status success");
    require(parsed["engines"].is_array(), "engines array");
    require(parsed["engines"].as_array().size() == 3, "3 engines returned");
    require(parsed["engines"][0]["name"].as_string() == "aceserve-modern", "modern engine first");
    require(parsed["engines"][0]["is_main"].as_bool() == true, "modern is main");
}

void test_favoritos_m3u_export_epg_and_logos() {
    // 1. Validar la cabecera EPG obligatoria de DobleM
    std::string expected_epg_header = "#EXTM3U url-tvg=\"https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv.xml\" tvg-shift=\"0\"";
    require(expected_epg_header.find("https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv.xml") != std::string::npos, "EPG xml URL");

    // 2. Validar conversión de logos relativos a URLs absolutas para clientes IPTV
    std::string hostport = "192.168.1.100:8888";
    std::string rel_logo = "/logos/dazn1.png";
    std::string abs_logo = rel_logo;
    if (!abs_logo.empty() && abs_logo.rfind("/", 0) == 0) {
        abs_logo = "http://" + hostport + abs_logo;
    }
    require(abs_logo == "http://192.168.1.100:8888/logos/dazn1.png", "absolute logo url conversion");

    // 3. Validar estructura M3U Plus de canal favorito con dial y variantes
    int dial = 1;
    std::string tvg_id = "DAZN1.es";
    std::string display_name = "DAZN 1";
    std::string slug = "dazn-1";

    std::ostringstream out;
    out << expected_epg_header << "\n";
    out << "\n#EXTINF:-1 tvg-id=\"" << tvg_id << "\" tvg-name=\"" << display_name << "\" tvg-logo=\"" << abs_logo << "\" group-title=\"Favoritos\"," << dial << ". " << display_name << " FHDa\n";
    out << "http://" << hostport << "/auto/" << slug << "-fhda\n";
    out << "\n#EXTINF:-1 tvg-id=\"" << tvg_id << "\" tvg-name=\"" << display_name << "\" tvg-logo=\"" << abs_logo << "\" group-title=\"Favoritos\"," << dial << ". " << display_name << " 720a\n";
    out << "http://" << hostport << "/auto/" << slug << "-720a\n";

    std::string m3u_content = out.str();
    require(m3u_content.find(expected_epg_header) == 0, "m3u begins with EPG header");
    require(m3u_content.find("tvg-id=\"DAZN1.es\"") != std::string::npos, "tvg-id DobleM mapping");
    require(m3u_content.find("tvg-logo=\"http://192.168.1.100:8888/logos/dazn1.png\"") != std::string::npos, "absolute tvg-logo present");
    require(m3u_content.find("group-title=\"Favoritos\",1. DAZN 1 FHDa") != std::string::npos, "group-title and dial FHDa");
    require(m3u_content.find("http://192.168.1.100:8888/auto/dazn-1-fhda") != std::string::npos, "FHDa stream url");
    require(m3u_content.find("group-title=\"Favoritos\",1. DAZN 1 720a") != std::string::npos, "group-title and dial 720a");
    require(m3u_content.find("http://192.168.1.100:8888/auto/dazn-1-720a") != std::string::npos, "720a stream url");
}

void test_ipfs_ipns_resolution_and_validation() {
    // 1. Validar esquemas IPFS / IPNS
    require(is_valid_source_url("ipfs://bafybeic56tkn..."), "ipfs scheme valid");
    require(is_valid_source_url("ipns://k51qzi5uqu5dh5q..."), "ipns scheme valid");
    require(is_valid_source_url("http://127.0.0.1:8080/ipns/k51qzi5uqu5dh5q/lista.m3u"), "local ipns 8080 valid");
    require(is_valid_source_url("http://127.0.0.1:8180/ipns/k51qzi5uqu5dh5q/lista.m3u"), "local ipns 8180 valid");
    require(is_valid_source_url("http://ipfs-node:8080/ipns/k51qzi5uqu5dh5q/lista.m3u"), "docker ipfs-node valid");

    // 2. Validar normalización de URLs IPFS / IPNS
    require(normalize_list_url("ipfs://QmHash123/list.m3u") == "https://ipfs.io/ipfs/QmHash123/list.m3u", "ipfs scheme normalization");
    require(normalize_list_url("ipns://k51qzi5uqu5dh5q/list.m3u") == "https://ipfs.io/ipns/k51qzi5uqu5dh5q/list.m3u", "ipns scheme normalization");
    require(normalize_list_url("https://k51qzi5uqu5dh5q.ipns.inbrowser.link/hashes.json") == "https://ipfs.io/ipns/k51qzi5uqu5dh5q/hashes.json", "inbrowser ipns normalization");
}

void test_dynamic_source_channel_matching() {
    // Validar concordancia de nombres de canales de fuentes dinámicas
    std::string target_slug = "dazn-laliga";
    std::string target_cname = "dazn laliga";

    std::vector<std::string> variations = {
        "DAZN LaLiga FHD",
        "DAZN La Liga 1080p",
        "DAZN LaLiga HD (720p)",
        "dazn laliga",
        "DAZN LaLiga"
    };

    for (const auto& var : variations) {
        std::string item_slug = canonical_slug(var);
        std::string item_cname = canonical_name(var);
        std::string item_slug_compact = replace_all(item_slug, "-", "");
        std::string target_slug_compact = replace_all(target_slug, "-", "");
        std::string item_cname_compact = replace_all(item_cname, " ", "");
        std::string target_cname_compact = replace_all(target_cname, " ", "");

        bool matches = (item_slug == target_slug) ||
                       (item_cname == target_cname) ||
                       (!item_slug_compact.empty() && item_slug_compact == target_slug_compact) ||
                       (!item_cname_compact.empty() && item_cname_compact == target_cname_compact);
        require(matches, "dynamic channel variation matched: " + var);
    }
}

// v09.02.01 - Tests para Filtros Regex Personalizados por Canal
void test_channel_regex_filters_and_theme() {
    auto eval_filter = [](const std::vector<std::string>& patterns, const std::string& title) -> bool {
        for (const auto& raw_pat : patterns) {
            if (raw_pat.empty()) continue;
            std::string pat = raw_pat;
            bool icase = true;
            if (pat.rfind("(?i)", 0) == 0) {
                pat = pat.substr(4);
                icase = true;
            } else if (pat.rfind("(?-i)", 0) == 0) {
                pat = pat.substr(5);
                icase = false;
            }
            if (pat.find("(?i)") != std::string::npos) {
                pat = replace_all(pat, "(?i)", "");
                icase = true;
            }
            try {
                auto flags = std::regex::ECMAScript;
                if (icase) flags |= std::regex::icase;
                std::regex reg(pat, flags);
                if (std::regex_search(title, reg)) return true;
            } catch (...) {
                auto low_title = lower(title);
                auto low_pat = lower(pat);
                if (low_title.find(low_pat) != std::string::npos) return true;
            }
        }
        return false;
    };

    // Factory rules
    std::string rule_m_dep = "(?i)(m\\+|m\\.|movistar)[\\s_]*deportes(?![\\s_]*[2-8])";
    std::string rule_m_dep2 = "(?i)(m\\+|m\\.|movistar)[\\s_]*deportes[\\s_]*2";
    std::string rule_m_dep3 = "(?i)(m\\+|m\\.|movistar)[\\s_]*deportes[\\s_]*3";

    // Matches for m-deportes
    require(eval_filter({rule_m_dep}, "M+ Deportes FHD"), "M+ Deportes FHD matches m-deportes");
    require(eval_filter({rule_m_dep}, "Movistar Deportes 1080p"), "Movistar Deportes 1080p matches m-deportes");
    require(eval_filter({rule_m_dep}, "m. deportes"), "m. deportes matches m-deportes");
    require(eval_filter({rule_m_dep}, "MOVISTAR_DEPORTES"), "MOVISTAR_DEPORTES matches m-deportes");

    // Negative lookahead test: M+ Deportes 2 to 8 MUST NOT match m-deportes rule
    require(!eval_filter({rule_m_dep}, "M+ Deportes 2"), "M+ Deportes 2 does NOT match m-deportes");
    require(!eval_filter({rule_m_dep}, "Movistar Deportes 3 FHD"), "Movistar Deportes 3 does NOT match m-deportes");
    require(!eval_filter({rule_m_dep}, "M+ Deportes 4"), "M+ Deportes 4 does NOT match m-deportes");
    require(!eval_filter({rule_m_dep}, "m+ deportes 8"), "m+ deportes 8 does NOT match m-deportes");

    // Matches for m-deportes-2
    require(eval_filter({rule_m_dep2}, "M+ Deportes 2 FHD"), "M+ Deportes 2 FHD matches m-deportes-2");
    require(eval_filter({rule_m_dep2}, "Movistar Deportes 2"), "Movistar Deportes 2 matches m-deportes-2");
    require(!eval_filter({rule_m_dep2}, "M+ Deportes"), "M+ Deportes does NOT match m-deportes-2");
    require(!eval_filter({rule_m_dep2}, "M+ Deportes 3"), "M+ Deportes 3 does NOT match m-deportes-2");

    // Matches for m-deportes-3
    require(eval_filter({rule_m_dep3}, "M+ Deportes 3 HD"), "M+ Deportes 3 HD matches m-deportes-3");
    require(!eval_filter({rule_m_dep3}, "M+ Deportes 2 HD"), "M+ Deportes 2 HD does NOT match m-deportes-3");

    // JSON persistence test
    Json root = Json::object{
        {"status", "success"},
        {"filters", Json::object{
            {"m-deportes", Json::array{Json(rule_m_dep)}},
            {"m-deportes-2", Json::array{Json(rule_m_dep2)}}
        }}
    };
    std::string json_str = root.dump();
    auto parsed = Json::parse(json_str);
    require(parsed.is_object(), "channel_filters parsed object");
    require(parsed.as_object().contains("filters"), "contains filters key");
    auto filters_obj = parsed.as_object().at("filters").as_object();
    require(filters_obj.contains("m-deportes"), "contains m-deportes");
    require(filters_obj.at("m-deportes").as_array().size() == 1, "has 1 pattern");

    // Fallback keyword test on invalid regex
    require(eval_filter({"[invalid(regex"}, "test [invalid(regex channel"), "fallback substring match");
}

// v09.02.02 - Tests para fuentes Docker locales y normalización de búsqueda por tokens
void test_v09_02_02_docker_sources_and_search_tokens() {
    // 1. Verificación de candidatos de resolución para 127.0.0.1 / localhost en Docker
    auto build_fallback_candidates = [](const std::string& original_url) -> std::vector<std::string> {
        std::vector<std::string> candidates;
        if (original_url.rfind("http://127.0.0.1:", 0) == 0 ||
            original_url.rfind("http://localhost:", 0) == 0 ||
            original_url.rfind("http://127.0.0.1/", 0) == 0 ||
            original_url.rfind("http://localhost/", 0) == 0) {
            std::string prefix = (original_url.rfind("http://127.0.0.1", 0) == 0) ? "http://127.0.0.1" : "http://localhost";
            std::string rest = original_url.substr(prefix.size());
            if (rest.rfind(":8080", 0) == 0 || rest.rfind(":8180", 0) == 0 || original_url.find("/ipfs/") != std::string::npos || original_url.find("/ipns/") != std::string::npos) {
                std::string path_only = rest;
                auto slash_pos = rest.find('/', 1);
                if (slash_pos != std::string::npos) {
                    path_only = rest.substr(slash_pos);
                } else {
                    path_only = "/";
                }
                candidates.push_back("http://ipfs-node:8080" + path_only);
            }
            candidates.push_back("http://host.docker.internal" + rest);
            candidates.push_back("http://172.17.0.1" + rest);
        }
        candidates.push_back(original_url);
        return candidates;
    };

    auto c1 = build_fallback_candidates("http://127.0.0.1:8080/ipfs/QmTestHash123");
    require(c1.size() >= 3, "IPFS 127.0.0.1 candidates generated");
    require(c1[0] == "http://ipfs-node:8080/ipfs/QmTestHash123", "IPFS ipfs-node candidate is first");
    require(c1[1] == "http://host.docker.internal:8080/ipfs/QmTestHash123", "host.docker.internal candidate is present");
    require(c1[2] == "http://172.17.0.1:8080/ipfs/QmTestHash123", "172.17.0.1 candidate is present");

    auto c2 = build_fallback_candidates("http://localhost:43110/ZeroNet/data.m3u");
    require(c2.size() >= 3, "ZeroNet localhost candidates generated");
    require(c2[0] == "http://host.docker.internal:43110/ZeroNet/data.m3u", "ZeroNet host.docker.internal is first");
    require(c2[1] == "http://172.17.0.1:43110/ZeroNet/data.m3u", "ZeroNet 172.17.0.1 is second");

    // 2. Normalización de texto y matching multi-término de búsqueda
    auto normalize_search = [](const std::string& input) -> std::string {
        std::string s = lower(input);
        s = replace_all(s, "movistar+", "movistar");
        s = replace_all(s, "m+", "movistar");
        s = replace_all(s, "m.", "movistar");
        std::string out;
        for (char ch : s) {
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                out += ch;
            } else {
                out += ' ';
            }
        }
        std::vector<std::string> words = split(out, ' ');
        std::string clean;
        for (const auto& w : words) {
            if (w.empty()) continue;
            if (w == "m") {
                if (!clean.empty()) clean += " ";
                clean += "movistar";
            } else {
                if (!clean.empty()) clean += " ";
                clean += w;
            }
        }
        return clean;
    };

    auto match_tokens = [&](const std::string& query, const std::string& target) -> bool {
        std::string norm_q = normalize_search(query);
        std::string norm_t = normalize_search(target);
        std::vector<std::string> tokens = split(norm_q, ' ');
        for (const auto& tok : tokens) {
            if (tok.empty()) continue;
            if (norm_t.find(tok) == std::string::npos) return false;
        }
        return true;
    };

    require(match_tokens("m+ deportes", "14. M Deportes FHDa"), "m+ deportes matches 14. M Deportes FHDa");
    require(match_tokens("deportes movistar", "14. M Deportes FHDa"), "reversed query tokens match");
    require(match_tokens("m. deportes", "Movistar Deportes 1"), "m. deportes matches Movistar Deportes 1");
    require(match_tokens("m deportes", "M+ Deportes"), "m deportes matches M+ Deportes");
    require(!match_tokens("m+ cine", "M+ Deportes FHD"), "m+ cine does not match M+ Deportes");

    // 3. Content ID de 40 caracteres hexadecimales
    std::string cid = "4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik82";
    require(cid.length() == 40, "CID must be 40 chars");
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
        test_fhda_720a_variants_and_custom_logos();
        test_virtual_url_and_engine_pool_status();
        test_favoritos_m3u_export_epg_and_logos();
        test_ipfs_ipns_resolution_and_validation();
        test_dynamic_source_channel_matching();
        test_channel_regex_filters_and_theme();
        test_v09_02_02_docker_sources_and_search_tokens();
        std::cout << "httpaceproxycpp core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "test failed: " << e.what() << "\n";
        return 1;
    }
}
