#include "httpaceproxycpp/config.hpp"
#include "httpaceproxycpp/proxy.hpp"
#include "httpaceproxycpp/util.hpp"

#include <atomic>
#include <csignal>
#include <exception>

#include <fstream>
#include <filesystem>

namespace {
std::atomic<httpace::Proxy*> g_proxy{nullptr};

void handle_signal(int) {
    if (auto* proxy = g_proxy.load()) proxy->stop();
}

void ensure_local_m3u_structure(const std::string& root_dir) {
    try {
        auto locales_dir = std::filesystem::path(root_dir) / "http" / "listas" / "locales";
        std::filesystem::create_directories(locales_dir);
        auto interna = locales_dir / "Interna.m3u";
        if (!std::filesystem::exists(interna)) {
            std::ofstream out(interna);
            out << "#EXTM3U\n";
        }
        auto hashes = locales_dir / "hashes_acestream.m3u";
        if (!std::filesystem::exists(hashes)) {
            std::ofstream out(hashes);
            out << "#EXTM3U\n";
        }
    } catch (const std::exception& e) {
        httpace::log_line("WARNING", "ensure_local_m3u_structure error: " + std::string(e.what()));
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        auto config = httpace::load_config(argc, argv);
        ensure_local_m3u_structure(config.root_dir);
        httpace::log_line("INFO", "HTTPAceProxyCPP starting");
        httpace::log_line("INFO", "AceStream engine " + config.ace_host + ":" + std::to_string(config.ace_api_port));
        httpace::Proxy proxy(config);
        g_proxy.store(&proxy);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGINT, handle_signal);
        proxy.start();
        g_proxy.store(nullptr);
        return 0;
    } catch (const std::exception& e) {
        httpace::log_line("ERROR", e.what());
        return 1;
    }
}
