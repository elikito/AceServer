#include "httpaceproxycpp/stream_scorer.hpp"
#include "httpaceproxycpp/util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace httpace {

namespace {

// Reemplaza acentos y caracteres especiales en UTF-8
std::string strip_accents_utf8(std::string text) {
    static const std::vector<std::pair<std::string, std::string>> replacements = {
        {"\xc3\xa1", "a"}, {"\xc3\x81", "a"}, // á, Á
        {"\xc3\xa9", "e"}, {"\xc3\x89", "e"}, // é, É
        {"\xc3\xad", "i"}, {"\xc3\x8d", "i"}, // í, Í
        {"\xc3\xb3", "o"}, {"\xc3\x93", "o"}, // ó, Ó
        {"\xc3\xba", "u"}, {"\xc3\x9a", "u"}, // ú, Ú
        {"\xc3\xbc", "u"}, {"\xc3\x9c", "u"}, // ü, Ü
        {"\xc3\xb1", "n"}, {"\xc3\x91", "n"}, // ñ, Ñ
        {"\xc3\xa7", "c"}, {"\xc3\x87", "c"}, // ç, Ç
    };

    for (const auto& [from, to] : replacements) {
        text = replace_all(text, from, to);
    }
    return text;
}

// Lista de tokens de calidad y modificadores para filtrar
const std::unordered_set<std::string>& get_filter_tokens() {
    static const std::unordered_set<std::string> tokens = {
        "1080p", "1080i", "1080", "720p", "720i", "720", "576p", "576i", "480p",
        "4k", "uhd", "fhd", "hd", "sd",
        "hevc", "h265", "h.265", "h264", "h.264",
        "50fps", "60fps", "25fps", "fps",
        "back", "backup", "opt", "alt", "directo", "live", "envivo",
        "castellano", "spanish", "spain", "espana", "acestream",
        "*", "**", "***", "#", "##"
    };
    return tokens;
}

} // namespace

StreamQuality detect_stream_quality(const std::string& name) {
    auto low = lower(strip_accents_utf8(name));
    if (low.find("4k") != std::string::npos || low.find("uhd") != std::string::npos) {
        return StreamQuality::UHD_4K;
    }
    if (low.find("1080") != std::string::npos || low.find("fhd") != std::string::npos) {
        return StreamQuality::FHD_1080;
    }
    if (low.find("720") != std::string::npos || low.find("hd") != std::string::npos) {
        return StreamQuality::HD_720;
    }
    return StreamQuality::SD;
}

std::string canonical_name(std::string name) {
    name = strip_accents_utf8(name);
    name = lower(name);

    // Reemplazar caracteres especiales y puntuación por espacios
    for (char& c : name) {
        if (c == '*' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '_' || c == '+' || c == ':' ||
            c == ',' || c == '.' || c == '|' || c == '/' || c == '\\' ||
            c == '"' || c == '\'' || c == '-' || c == '`' || c == '~') {
            c = ' ';
        }
    }

    // Dividir en palabras y filtrar tokens de calidad / modificadores
    const auto& filter = get_filter_tokens();
    std::istringstream stream(name);
    std::string word;
    std::vector<std::string> valid_words;

    while (stream >> word) {
        word = trim(word);
        if (word.empty()) continue;
        if (filter.find(word) != filter.end()) continue;
        valid_words.push_back(word);
    }

    if (valid_words.empty()) {
        return trim(name);
    }

    std::ostringstream result;
    for (std::size_t i = 0; i < valid_words.size(); ++i) {
        if (i > 0) result << " ";
        result << valid_words[i];
    }
    return result.str();
}

std::string canonical_slug(std::string name) {
    auto cname = canonical_name(std::move(name));
    std::string slug;
    slug.reserve(cname.size());

    bool last_was_dash = true; // evitar guión inicial
    for (char c : cname) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            slug.push_back(c);
            last_was_dash = false;
        } else if (!last_was_dash) {
            slug.push_back('-');
            last_was_dash = true;
        }
    }

    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug.empty() ? "channel" : slug;
}

double StreamScorer::calculate_score(const ChannelCandidate& candidate) {
    // Penalización total si está marcado como caído/erróneo
    if (candidate.health == ChannelHealth::OFFLINE ||
        candidate.health == ChannelHealth::ERROR ||
        candidate.health == ChannelHealth::BLOCKED) {
        return -100.0;
    }

    double score = 0.0;

    // Bonus de sesión activa en caliente
    if (candidate.is_active_stream) {
        score += 100.0;
    }

    // Bonus por calidad detectada
    switch (candidate.quality) {
        case StreamQuality::UHD_4K:   score += 40.0; break;
        case StreamQuality::FHD_1080: score += 30.0; break;
        case StreamQuality::HD_720:   score += 15.0; break;
        default: break;
    }

    // Puntuación por enjambre (peers y velocidad de bajada)
    score += (candidate.peers * 10.0);
    score += (static_cast<double>(candidate.speed_down) / 1024.0 * 0.5);

    // Bonus por estado confirmado
    if (candidate.health == ChannelHealth::ONLINE) {
        score += 50.0;
    } else if (candidate.health == ChannelHealth::LOW_PEERS) {
        score += 20.0;
    } else if (candidate.health == ChannelHealth::UNKNOWN) {
        score += 10.0;
    }

    return score;
}

void StreamScorer::rank_candidates(std::vector<ChannelCandidate>& candidates) {
    for (auto& c : candidates) {
        c.quality = detect_stream_quality(c.name);
        c.score = calculate_score(c);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const ChannelCandidate& a, const ChannelCandidate& b) {
        return a.score > b.score;
    });
}

} // namespace httpace
