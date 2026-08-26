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

} // namespace

int main() {
    try {
        test_hashes();
        test_url_helpers();
        test_json();
        test_playlist();
        test_m3u_parser_variants();
        test_stream_scorer();
        std::cout << "httpaceproxycpp core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "test failed: " << e.what() << "\n";
        return 1;
    }
}
