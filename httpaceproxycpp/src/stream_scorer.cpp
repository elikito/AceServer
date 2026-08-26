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

// Elimina sufijos de procedencia/origen como "→ ELCANO", "--> NEW ERA", "-> SPORT TV", etc.
std::string strip_origin_suffix(std::string text) {
    static const std::vector<std::string> arrow_patterns = {
        "\xe2\x86\x92", // → (U+2192)
        "\xe2\x9e\x94", // ➔ (U+2794)
        "\xe2\x9e\x9c", // ➜ (U+279C)
        "\xe2\x9e\xa1", // ➡ (U+27A1)
        "\xe2\x87\x92", // ⇒ (U+21D2)
        "-->",
        "->",
        "==>",
        "=>"
    };

    for (const auto& arrow : arrow_patterns) {
        auto pos = text.find(arrow);
        if (pos != std::string::npos) {
            text = text.substr(0, pos);
        }
    }
    return text;
}

// Reconoce hashes alfanuméricos/hexadecimales de 4 caracteres (ej. "936c", "2929", "9f1a", "9e38", "ad6d")
bool is_hex_hash_token(const std::string& token) {
    if (token.size() != 4) return false;
    for (char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
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
    name = strip_origin_suffix(std::move(name));
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

    // Dividir en palabras y filtrar tokens de calidad / modificadores / hashes de 4 caracteres
    const auto& filter = get_filter_tokens();
    std::istringstream stream(name);
    std::string word;
    std::vector<std::string> valid_words;

    while (stream >> word) {
        word = trim(word);
        if (word.empty()) continue;
        if (filter.find(word) != filter.end()) continue;
        if (is_hex_hash_token(word)) continue;
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

bool detect_is_foreign(const std::string& name) {
    auto clean = strip_origin_suffix(name);
    auto low = lower(strip_accents_utf8(clean));
    static const std::vector<std::string> foreign_patterns = {
        "(de)", "(ru)", "(pl)", "(uk)", "(it)", "(fr)", "(pt)", "(tr)", "(ar)",
        "[de]", "[ru]", "[pl]", "[uk]", "[it]", "[fr]", "[pt]", "[tr]", "[ar]",
        " turk", " france", " poland", " germany", " russia", " italia", " portugal", " turkey",
        "deutschland", "russian", "polski", "italiano", "francais"
    };
    for (const auto& pat : foreign_patterns) {
        if (low.find(pat) != std::string::npos) {
            return true;
        }
    }
    return false;
}

double StreamScorer::calculate_score(const ChannelCandidate& candidate) {
    // Si ha sido deshabilitado manualmente por el usuario en Favoritos/Curación
    if (candidate.is_disabled) {
        return -999.0;
    }

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

    // Bonus por calidad detectada y ponderación contra peers (v08.26.02)
    // - 1080p: Base mínima de +100 puntos si tiene al menos 3 peers activos (o +30 si < 3).
    // - 720p: Base de +60 puntos si tiene al menos 3 peers activos (o +15 si < 3).
    // - SD: Base máxima acotada (+30 puntos totales de calidad + peers) para evitar que supere a un 1080p/720p saludable.
    if (candidate.quality == StreamQuality::UHD_4K) {
        score += (candidate.peers >= 3) ? 120.0 : 40.0;
        score += (candidate.peers * 10.0);
    } else if (candidate.quality == StreamQuality::FHD_1080) {
        score += (candidate.peers >= 3) ? 100.0 : 30.0;
        score += (candidate.peers * 10.0);
    } else if (candidate.quality == StreamQuality::HD_720) {
        score += (candidate.peers >= 3) ? 60.0 : 15.0;
        score += (candidate.peers * 10.0);
    } else {
        // Calidad SD
        double sd_peer_contrib = candidate.peers * 5.0;
        score += std::min(30.0, sd_peer_contrib);
    }

    // Puntuación por velocidad de bajada
    score += (static_cast<double>(candidate.speed_down) / 1024.0 * 0.5);

    // Penalización por país/idioma extranjero no español
    if (candidate.is_foreign) {
        score -= 50.0;
    }

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
        c.is_foreign = detect_is_foreign(c.name);
        c.score = calculate_score(c);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const ChannelCandidate& a, const ChannelCandidate& b) {
        return a.score > b.score;
    });
}

} // namespace httpace
