#include "httpaceproxycpp/config.hpp"
#include "httpaceproxycpp/proxy.hpp"
#include "httpaceproxycpp/util.hpp"

#include <atomic>
#include <csignal>
#include <ctime>
#include <exception>

#include <fstream>
#include <filesystem>

namespace {
std::atomic<httpace::Proxy*> g_proxy{nullptr};

void handle_signal(int) {
    if (auto* proxy = g_proxy.load()) proxy->stop();
}

void ensure_local_m3u_structure(const httpace::Config& config) {
    try {
        auto config_dir = config.get_config_dir();
        std::filesystem::create_directories(config_dir);
        std::filesystem::create_directories(config_dir / "listas" / "locales");

        auto listas_dir = std::filesystem::path(config.root_dir) / "http" / "listas";
        auto locales_dir = listas_dir / "locales";
        std::filesystem::create_directories(locales_dir);

        // 1. epg_favorites.json en config_dir
        auto cfg_favs = config_dir / "epg_favorites.json";
        auto root_favs = listas_dir / "epg_favorites.json";
        if (!std::filesystem::exists(cfg_favs)) {
            if (std::filesystem::exists(root_favs) && std::filesystem::file_size(root_favs) > 5) {
                std::filesystem::copy_file(root_favs, cfg_favs, std::filesystem::copy_options::skip_existing);
            } else {
                std::ofstream out(cfg_favs);
                out << "{\n  \"favorites\": [],\n  \"disabled_cids\": []\n}\n";
            }
        }

        // 2. plugins_state.json en config_dir
        auto cfg_plugins = config_dir / "plugins_state.json";
        auto root_plugins = std::filesystem::path(config.root_dir) / "http" / "plugins_state.json";
        if (!std::filesystem::exists(cfg_plugins)) {
            if (std::filesystem::exists(root_plugins) && std::filesystem::file_size(root_plugins) > 5) {
                std::filesystem::copy_file(root_plugins, cfg_plugins, std::filesystem::copy_options::skip_existing);
            }
        }

        // 3. Interna.m3u & hashes_acestream.m3u
        for (const auto& base_locales : {config_dir / "listas" / "locales", locales_dir}) {
            auto interna = base_locales / "Interna.m3u";
            if (!std::filesystem::exists(interna)) {
                std::ofstream out(interna);
                out << "#EXTM3U\n";
            }
            auto hashes = base_locales / "hashes_acestream.m3u";
            if (!std::filesystem::exists(hashes)) {
                std::ofstream out(hashes);
                out << "#EXTM3U\n";
            }
        }
    } catch (const std::exception& e) {
        httpace::log_line("WARNING", "ensure_local_m3u_structure error: " + std::string(e.what()));
    }
}
} // namespace

int main(int argc, char** argv) {
    tzset();
    try {
        auto config = httpace::load_config(argc, argv);
        ensure_local_m3u_structure(config);
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
