#include "httpaceproxycpp/http_client.hpp"
#include "httpaceproxycpp/util.hpp"

#include <curl/curl.h>

#include <mutex>
#include <regex>
#include <stdexcept>

namespace httpace {
namespace {

std::once_flag curl_init_flag;

size_t write_string_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(buffer, size * nitems);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        auto key = lower(trim(line.substr(0, colon)));
        auto value = trim(line.substr(colon + 1));
        if (!key.empty()) (*headers)[key] = value;
    }
    return size * nitems;
}

struct StreamState {
    const std::function<bool(const char*, std::size_t)>* cb;
    const std::atomic<bool>* running;
};

size_t stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* state = static_cast<StreamState*>(userdata);
    auto bytes = size * nmemb;
    if (!state->running->load()) return 0;
    return (*state->cb)(ptr, bytes) ? bytes : 0;
}

int progress_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* running = static_cast<const std::atomic<bool>*>(clientp);
    return running->load() ? 0 : 1;
}

} // namespace

HttpClient::HttpClient() {
    std::call_once(curl_init_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpClient::~HttpClient() = default;

HttpClientResponse HttpClient::get_single(const std::string& url,
                                          const std::map<std::string, std::string>& headers,
                                          long timeout_seconds,
                                          bool follow_redirects) const {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    HttpClientResponse response;
    response.url = url;
    struct curl_slist* header_list = nullptr;
    for (const auto& [k, v] : headers) {
        auto h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(timeout_seconds, 15L));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (HTTPAceProxyCPP)");
    if (header_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        std::string err = curl_easy_strerror(rc);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw std::runtime_error(err);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    char* effective = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
    if (effective) response.url = effective;
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response;
}

HttpClientResponse HttpClient::get(const std::string& url,
                                   const std::map<std::string, std::string>& headers,
                                   long timeout_seconds,
                                   bool follow_redirects) const {
    if (url.find("/ipfs/") != std::string::npos || url.find("/ipns/") != std::string::npos || url.find(".ipns.") != std::string::npos || url.find(".ipfs.") != std::string::npos) {
        bool is_ipns = (url.find("/ipns/") != std::string::npos || url.find(".ipns.") != std::string::npos);
        std::string token = is_ipns ? "/ipns/" : "/ipfs/";
        std::string path_part;
        auto pos = url.find(token);
        if (pos != std::string::npos) {
            path_part = url.substr(pos + token.length());
        } else {
            static const std::regex inbrowser_re(R"(https?://([a-zA-Z0-9]+)\.(ipns|ipfs)\.[^/]+(/.*)?)", std::regex::icase);
            std::smatch m;
            if (std::regex_match(url, m, inbrowser_re)) {
                std::string cid = m[1].str();
                std::string p = m[3].matched ? m[3].str() : "";
                if (p.empty() || p == "/") p = "/hashes.json";
                path_part = cid + p;
            }
        }
        if (!path_part.empty()) {
            std::string cid = path_part;
            std::string subpath;
            auto slash_pos = path_part.find('/');
            if (slash_pos != std::string::npos) {
                cid = path_part.substr(0, slash_pos);
                subpath = path_part.substr(slash_pos);
            }
            std::vector<std::string> gateways;
            if (is_ipns) {
                gateways.push_back("https://" + cid + ".ipns.dweb.link" + subpath);
                gateways.push_back("https://dweb.link/ipns/" + path_part);
                gateways.push_back("https://ipfs.io/ipns/" + path_part);
                gateways.push_back("https://cloudflare-ipfs.com/ipns/" + path_part);
                gateways.push_back("https://gateway.pinata.cloud/ipns/" + path_part);
            } else {
                gateways.push_back("https://" + cid + ".ipfs.dweb.link" + subpath);
                gateways.push_back("https://dweb.link/ipfs/" + path_part);
                gateways.push_back("https://ipfs.io/ipfs/" + path_part);
                gateways.push_back("https://cloudflare-ipfs.com/ipfs/" + path_part);
            }
            for (const auto& gw_url : gateways) {
                try {
                    auto resp = get_single(gw_url, headers, 15, true);
                    if (resp.status >= 200 && resp.status < 400 && !resp.body.empty()) {
                        return resp;
                    }
                } catch (...) {}
            }
        }
    }
    return get_single(url, headers, timeout_seconds, follow_redirects);
}

bool HttpClient::stream(const std::string& url,
                        const std::function<bool(const char*, std::size_t)>& on_chunk,
                        const std::atomic<bool>& cancel,
                        long connect_timeout_seconds,
                        long read_timeout_seconds,
                        long buffer_size) const {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    StreamState state{&on_chunk, &cancel};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, read_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancel);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HTTPAceProxyCPP");
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, static_cast<long>(buffer_size));
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK || !cancel.load();
}

} // namespace httpace
