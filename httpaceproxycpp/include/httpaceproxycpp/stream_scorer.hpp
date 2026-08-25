#pragma once

#include "httpaceproxycpp/channel_verifier.hpp"

#include <string>
#include <vector>

namespace httpace {

enum class StreamQuality {
    UHD_4K = 4,
    FHD_1080 = 3,
    HD_720 = 2,
    SD = 1,
    UNKNOWN = 0
};

/// Convierte una cadena de canal a su nombre canónico normalizado (minúsculas, sin acentos ni etiquetas).
/// Ej: "Teledeporte 720p **", "TELEDEPORTE FHD" -> "teledeporte"
std::string canonical_name(std::string name);

/// Convierte una cadena de canal a un slug identificador apto para URL (ej. "teledeporte", "la-1", "dazn-1").
std::string canonical_slug(std::string name);

/// Detecta la calidad del stream a partir del nombre del canal.
StreamQuality detect_stream_quality(const std::string& name);

/// Detecta si el stream contiene sufijos o etiquetas de país/idioma extranjero no español.
bool detect_is_foreign(const std::string& name);

/// Estructura de candidato a stream para el selector automático.
struct ChannelCandidate {
    std::string   name;
    std::string   content_id;
    std::string   plugin_name;
    std::string   logo;
    std::string   group;
    std::string   tvg_id;
    StreamQuality quality = StreamQuality::UNKNOWN;
    int           quality_bonus = 0;
    int           peers = 0;
    long long     speed_down = 0;
    ChannelHealth health = ChannelHealth::UNKNOWN;
    bool          is_active_stream = false;
    bool          is_disabled = false;
    bool          is_foreign = false;
    double        score = 0.0;
};

/// Motor de evaluación y puntuación de candidatos a stream.
class StreamScorer {
public:
    /// Calcula la puntuación de un candidato en función de peers, velocidad de bajada, calidad y sesión activa.
    static double calculate_score(const ChannelCandidate& candidate);

    /// Evalúa y ordena una lista de candidatos de mayor a menor puntuación.
    static void rank_candidates(std::vector<ChannelCandidate>& candidates);
};

} // namespace httpace
