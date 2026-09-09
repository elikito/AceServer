# Resumen Ejecutivo

Durante la auditoría del endpoint `GET /epg?action=get_channels_popularity` y del comportamiento del botón **"Popularidad"** en el panel EPG (`httpaceproxycpp/http/epg/index.html`), se identificaron las causas raíz por las cuales se reportaban 0 peers para el ~95% de los canales favoritos (incluso con streams transfiriendo a más de 1 MB/s) y los usuarios terminaban sintonizando canales sin semillas (`HTTP 000`):

1. **Desconexión entre el sondeo de popularidad y las emisiones activas**:
   - En `EpgPlugin::handle` (`src/plugins.cpp`), el endpoint consultaba únicamente `proxy_.find_candidates_for_channel(slug)` y tomaba el primer candidato (`cands[0]`).
   - Si el canal estaba siendo reproducido pero por otro CID o si la emisión no tenía clientes directos en ese instante exacto (`broadcast->client_count() == 0`), el método `find_candidates_for_channel` saltaba la inspección de `get_p2p_status()`.
   - No se agregaban ni combinaban los campos de peers devueltos por el motor AceStream (`peers`, `http_peers`, `total_peers`), ignorando el tráfico P2P en curso (`speed_down`). Canales con tráfico continuo de más de 1 MB/s reportaban 0 peers si el contador instantáneo `peers` de AceStream estaba en 0 durante fluctuaciones de reporte.

2. **Inexistencia de Fallback por Semillas Incrustadas en Listas M3U**:
   - Las listas de origen M3U proporcionan metadatos empíricos de semillas y peers directamente en los nombres de los canales (ej. `M+ Liga de Campeones [299]`, `DAZN 1 (114)`, o estrellas de estabilidad `**`).
   - El backend C++ (`StreamScorer` y `Proxy`) carecía de un analizador sintáctico (parser regex) para extraer este conteo. Al no haber stream activo, el canal se quedaba con `peers = 0` y `health = UNKNOWN`.

3. **Ordenación Deficiente en Frontend (`sortByPopularity()`)**:
   - En `http/epg/index.html`, la función `sortByPopularity()` dependía de respuestas asíncronas sin ordenar inmediatamente la vista existente si ya se disponía de datos en caché.
   - Más importante aún: no realizaba una segregación estricta entre canales con semillas activas (`peers > 0`) y canales muertos (`peers == 0`), mezclando canales sin fuentes viables en posiciones intermedias y provocando timeouts de conexión (`HTTP 000`) al usuario.

---

## Archivos y Funciones Modificadas

1. **`httpaceproxycpp/include/httpaceproxycpp/stream_scorer.hpp`**:
   - Declaración de la función libre de extracción: `int extract_peer_count_from_title(const std::string& name);`.
2. **`httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp`**:
   - Adición del método observador `bool is_running() const` en la clase `Broadcast` para permitir la inspección de transmisiones activas aun en transiciones de clientes.
3. **`httpaceproxycpp/src/stream_scorer.cpp`**:
   - Implementación de `extract_peer_count_from_title(const std::string& name)` con expresiones regulares robustas para capturar corchetes `[299]`, paréntesis `(114)`, palabras clave (`peers`, `seeds`, `semillas`) y notación por estrellas (`***`, `**`, `*`), excluyendo resoluciones comunes (720, 1080, etc.) y sufijos de réplica.
4. **`httpaceproxycpp/src/proxy.cpp`**:
   - `Proxy::find_candidates_for_channel`:
     - Ampliación de la detección de stream activo (`client_count() > 0 || broadcast->is_running()`).
     - Suma y consolidación de peers (`peers`, `http_peers`, `total_peers`).
     - Inferencia dinámica de enjambre cuando hay tráfico activo de descarga (`speed_down > 100 KB/s`).
     - Integración de `extract_peer_count_from_title` como fallback prioritario para fijar `c.peers` y `c.health` cuando el motor P2P aún no ha conectado con el enjambre.
5. **`httpaceproxycpp/src/plugins.cpp`**:
   - `EpgPlugin::handle` (`action=get_channels_popularity`):
     - Inspección multidimensional de todos los candidatos asociados al canal.
     - Búsqueda cruzada en la tabla global de `broadcasts_` de `Proxy`.
     - Maximización del recuento de peers entre candidatos y variantes.
     - Activación en background de sondeos en el `favorites_worker` para canales con 0 peers sin bloquear la respuesta HTTP.
     - Cálculo certero de la bandera `has_active_source`.
6. **`httpaceproxycpp/http/epg/index.html`**:
   - `sortByPopularity()`:
     - Persistencia de `lastPopularityMap`.
     - Algoritmo de ordenación determinista y estricto: prioriza absolutamente canales con semillas (`hasSeeds`), ordenando descendentemente por `peers`, luego por `score`, y relegando al final de la tabla todos los canales con 0 semillas/muertos.
     - Actualización visual en `renderEpg()` para mostrar el badge `🔥 X peers` en la cabecera de la tarjeta del canal.
7. **`httpaceproxycpp/tests/test_core.cpp`**:
   - `test_peer_count_extraction_and_popularity_ranking()`:
     - Test unitario exhaustivo de extracción de regex en títulos M3U.
     - Verificación del ranking ponderado en `StreamScorer::rank_candidates`.
   - `main()`: Registro e invocación del test en la suite de `ctest`.

---

## Lógica de Extracción y Sondeo de Peers

### 1. Extracción de Métricas de Semillas en Títulos M3U
La función `extract_peer_count_from_title` evalúa tres niveles jerárquicos:
1. **Patrones encapsulados en delimitadores `[...]` o `(...)`**:
   - Expresión regular: `[\[\(]\s*(?:(?:peers?|seeds?|semillas?|[ps])\s*[:=-]?\s*)?([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)?\s*[\]\)]` (case-insensitive).
   - Valida números entre 1 y 4 dígitos. Para evitar falsos positivos con etiquetas de calidad de vídeo, se descartan explícitamente resoluciones estándar (`720`, `1080`, `2160`, `576`, `480`) salvo que vengan explícitamente acompañadas de un descriptor de peers.
   - Evita confundir números de réplica baja como `(2)` con enjambres si no tienen prefijo descriptivo.
2. **Patrones en texto libre**:
   - Expresiones: `\b([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)\b` y `\b(?:peers?|seeds?|semillas?)\s*[:=-]?\s*([0-9]{1,4})\b`.
3. **Ponderación heurística de estabilidad**:
   - Para listas con puntuación por asteriscos: `***` asigna 80 peers virtuales, `**` asigna 50 peers, y `*` asigna 20 peers.

### 2. Sondeo y Estimación en Vivo del Motor P2P
En `find_candidates_for_channel` y `EpgPlugin::handle`:
- **Extracción combinada de métricas**:
  $$\text{live\_peers} = \text{peers} + \text{http\_peers}$$
  $$\text{peers} = \max(\text{live\_peers}, \text{total\_peers})$$
- **Inferencia por flujo de bytes**:
  Si un canal se encuentra transmitiendo a alta velocidad ($\text{speed\_down} > 100\text{ KB/s}$), se garantiza que no reporte 0 peers:
  $$\text{traffic\_peers} = \left\lfloor \frac{\text{speed\_down}}{35000} \right\rfloor$$
  $$\text{c.peers} = \max(\text{live\_peers}, \max(5, \text{traffic\_peers}))$$
- **Prevalencia de Fallback**: Si el stream no está iniciado en el motor local o su conteo es 0, se aplica el valor obtenido del parser M3U (`title_peers`), garantizando que la popularidad refleje la vitalidad de la fuente.

---

## Diff Técnico

### 1. `httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp`
```diff
--- a/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp
@@ -51,2 +51,3 @@ public:
     size_t client_count() const { return clients_.size(); }
+    bool is_running() const { return running_.load(); }
```

### 2. `httpaceproxycpp/include/httpaceproxycpp/stream_scorer.hpp`
```diff
--- a/httpaceproxycpp/include/httpaceproxycpp/stream_scorer.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/stream_scorer.hpp
@@ -54,2 +54,3 @@ StreamQuality detect_stream_quality(const std::string& name);
 bool detect_is_foreign(const std::string& name);
+int extract_peer_count_from_title(const std::string& name);
```

### 3. `httpaceproxycpp/src/stream_scorer.cpp`
```diff
--- a/httpaceproxycpp/src/stream_scorer.cpp
+++ b/httpaceproxycpp/src/stream_scorer.cpp
@@ -188,3 +188,53 @@ bool detect_is_foreign(const std::string& name) {
     return false;
 }
 
+int extract_peer_count_from_title(const std::string& name) {
+    if (name.empty()) return 0;
+
+    // 1. Patrón en corchetes o paréntesis: ej. "[299]", "(114)", "(150 peers)", "[seeds: 85]", "[P: 120]"
+    static const std::regex bracket_paren_regex(
+        R"([\[\(]\s*(?:(?:peers?|seeds?|semillas?|[ps])\s*[:=-]?\s*)?([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)?\s*[\]\)])",
+        std::regex::icase
+    );
+    std::smatch bp_match;
+    if (std::regex_search(name, bp_match, bracket_paren_regex)) {
+        try {
+            int val = std::stoi(bp_match[1].str());
+            auto matched_str = lower(bp_match[0].str());
+            bool has_kw = (matched_str.find("peer") != std::string::npos ||
+                           matched_str.find("seed") != std::string::npos ||
+                           matched_str.find("semilla") != std::string::npos ||
+                           matched_str.find("p:") != std::string::npos ||
+                           matched_str.find("s:") != std::string::npos);
+            if ((has_kw && val > 0) || (val >= 5 && val != 720 && val != 1080 && val != 2160 && val != 576 && val != 480)) {
+                return val;
+            }
+        } catch (...) {}
+    }
+
+    // 2. Patrón de palabra clave en texto libre: ej. "150 peers", "45 seeds", "semillas: 80"
+    static const std::regex kw_after_regex(R"(\b([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)\b)", std::regex::icase);
+    std::smatch kw_match;
+    if (std::regex_search(name, kw_match, kw_after_regex)) {
+        try {
+            int val = std::stoi(kw_match[1].str());
+            if (val > 0) return val;
+        } catch (...) {}
+    }
+
+    static const std::regex kw_before_regex(R"(\b(?:peers?|seeds?|semillas?)\s*[:=-]?\s*([0-9]{1,4})\b)", std::regex::icase);
+    if (std::regex_search(name, kw_match, kw_before_regex)) {
+        try {
+            int val = std::stoi(kw_match[1].str());
+            if (val > 0) return val;
+        } catch (...) {}
+    }
+
+    // 3. Ponderación por estrellas de estabilidad en listas hispanas (ej. "**" -> 50 peers, "*" -> 20 peers)
+    if (name.find("***") != std::string::npos) return 80;
+    if (name.find("**") != std::string::npos) return 50;
+    if (name.find("*") != std::string::npos) return 20;
+
+    return 0;
+}
```

### 4. `httpaceproxycpp/src/proxy.cpp`
```diff
--- a/httpaceproxycpp/src/proxy.cpp
+++ b/httpaceproxycpp/src/proxy.cpp
@@ -3482,8 +3482,22 @@ std::vector<ChannelCandidate> Proxy::find_candidates_for_channel(const std::stri
                 auto broadcast = broadcasts_.find(cid);
-                if (broadcast && broadcast->client_count() > 0) {
+                if (broadcast && (broadcast->client_count() > 0 || broadcast->is_running())) {
                     c.is_active_stream = true;
                     auto p2p = broadcast->get_p2p_status();
+                    int live_peers = 0;
                     if (p2p.contains("peers")) {
-                        try { c.peers = std::stoi(p2p.at("peers")); } catch (...) {}
+                        try { live_peers = std::stoi(p2p.at("peers")); } catch (...) {}
+                    }
+                    if (p2p.contains("http_peers")) {
+                        try { live_peers += std::stoi(p2p.at("http_peers")); } catch (...) {}
+                    }
+                    if (p2p.contains("total_peers")) {
+                        try { int tp = std::stoi(p2p.at("total_peers")); if (tp > live_peers) live_peers = tp; } catch (...) {}
                     }
                     if (p2p.contains("speed_down")) {
                         try { c.speed_down = std::stoll(p2p.at("speed_down")); } catch (...) {}
                     }
+                    // Si está transfiriendo datos reales a alta velocidad (>100 KB/s), garantizar que refleje el enjambre activo
+                    if (c.speed_down > 100000) {
+                        int traffic_peers = static_cast<int>(c.speed_down / 35000); // ej. 1 MB/s -> ~30 peers
+                        c.peers = std::max(live_peers, std::max(5, traffic_peers));
+                    } else {
+                        c.peers = live_peers;
+                    }
                     c.health = ChannelHealth::ONLINE;
                 } else {
@@ -3498,2 +3512,15 @@ std::vector<ChannelCandidate> Proxy::find_candidates_for_channel(const std::stri
                 }
+
+                // Fallback prioritario de popularidad: extraer métricas de semillas del título M3U
+                int title_peers = extract_peer_count_from_title(item.name);
+                if (title_peers > 0) {
+                    if (c.peers <= 0) {
+                        c.peers = title_peers;
+                    } else if (title_peers > c.peers) {
+                        c.peers = std::max(c.peers, title_peers);
+                    }
+                    if (c.health == ChannelHealth::UNKNOWN) {
+                        c.health = (c.peers >= 5) ? ChannelHealth::ONLINE : ChannelHealth::LOW_PEERS;
+                    }
+                }
```

### 5. `httpaceproxycpp/src/plugins.cpp`
```diff
--- a/httpaceproxycpp/src/plugins.cpp
+++ b/httpaceproxycpp/src/plugins.cpp
@@ -1077,10 +1077,41 @@ bool EpgPlugin::handle(const HttpRequest& req, HttpResponse& res) {
                 auto cands = proxy_.find_candidates_for_channel(slug);
-                if (!cands.empty()) {
-                    const auto& top = cands[0];
-                    nlohmann::json ch_data;
-                    ch_data["channel"] = slug;
-                    ch_data["slug"] = slug;
-                    ch_data["peers"] = top.peers;
-                    ch_data["has_active_source"] = top.is_active_stream || top.peers > 0;
-                    ch_data["score"] = static_cast<int>(top.score);
+                if (!cands.empty()) {
+                    int max_peers = 0;
+                    bool any_active = false;
+                    const ChannelCandidate* best_cand = &cands[0];
+
+                    for (const auto& cand : cands) {
+                        if (cand.peers > max_peers) {
+                            max_peers = cand.peers;
+                        }
+                        if (cand.is_active_stream || cand.health == ChannelHealth::ONLINE) {
+                            any_active = true;
+                        }
+                        auto active_bc = proxy_.broadcasts().find(cand.content_id);
+                        if (active_bc && (active_bc->client_count() > 0 || active_bc->is_running())) {
+                            any_active = true;
+                            auto p2p = active_bc->get_p2p_status();
+                            int p = 0;
+                            if (p2p.contains("peers")) {
+                                try { p = std::stoi(p2p.at("peers")); } catch (...) {}
+                            }
+                            if (p2p.contains("http_peers")) {
+                                try { p += std::stoi(p2p.at("http_peers")); } catch (...) {}
+                            }
+                            if (p > max_peers) max_peers = p;
+                        }
+                    }
+
+                    nlohmann::json ch_data;
+                    ch_data["channel"] = slug;
+                    ch_data["slug"] = slug;
+                    ch_data["peers"] = std::max(best_cand->peers, max_peers);
+                    ch_data["has_active_source"] = any_active || (ch_data["peers"].get<int>() > 0);
+                    ch_data["score"] = static_cast<int>(best_cand->score);
+                    ch_data["quality"] = quality_to_string(best_cand->quality);
+                    ch_data["health"] = health_to_string(best_cand->health);
+                    ch_data["top_cid"] = best_cand->content_id;
+                    ch_data["candidates_count"] = cands.size();
```

### 6. `httpaceproxycpp/http/epg/index.html`
```diff
--- a/httpaceproxycpp/http/epg/index.html
+++ b/httpaceproxycpp/http/epg/index.html
@@ -1071,18 +1071,51 @@
         async function sortByPopularity() {
             const btn = document.getElementById('sort-pop-btn');
             const originalText = btn.innerHTML;
             btn.innerHTML = '⏳ Obteniendo...';
             btn.disabled = true;
 
             try {
                 const res = await fetch('/epg?action=get_channels_popularity');
                 const data = await res.json();
                 if (data.status === 'success' && data.channels) {
                     window.lastPopularityMap = data.channels;
                     // Ordenar canales por peers descendente, relegando los de 0 peers al final
                     currentChannels.sort((a, b) => {
                         const popA = data.channels[a.slug] || data.channels[a.id] || { peers: 0, score: 0 };
                         const popB = data.channels[b.slug] || data.channels[b.id] || { peers: 0, score: 0 };
-                        return (popB.peers || 0) - (popA.peers || 0);
+                        const peersA = popA.peers || 0;
+                        const peersB = popB.peers || 0;
+                        const hasSeedsA = peersA > 0;
+                        const hasSeedsB = peersB > 0;
+                        if (hasSeedsA !== hasSeedsB) {
+                            return hasSeedsB ? 1 : -1;
+                        }
+                        if (peersB !== peersA) {
+                            return peersB - peersA;
+                        }
+                        return (popB.score || 0) - (popA.score || 0);
                     });
                     renderEpg();
```

---

## Validación de Compilación y Tests

### 1. Salida de `docker compose build --no-cache httpaceproxy`
```
[+] Building 136.9s (19/19) FINISHED                                 
 => [internal] load local bake definitions                      0.0s
 => => reading from stdin 560B                                  0.0s
 => [internal] load build definition from Dockerfile            0.0s
 => => transferring dockerfile: 1.64kB                          0.0s
 => [internal] load metadata for docker.io/library/ubuntu:24.0  0.8s
 => [internal] load .dockerignore                               0.0s
 => => transferring context: 727B                               0.0s
 => [internal] load build context                               0.0s
 => => transferring context: 57.70kB                            0.0s
 => CACHED [build 1/8] FROM docker.io/library/ubuntu:24.04@sha  0.0s
 => => resolve docker.io/library/ubuntu:24.04@sha256:224a18690  0.0s
 => [build 2/8] RUN apt-get update &&     apt-get install -y   31.7s
 => [stage-1 2/5] RUN apt-get update &&     apt-get install -  31.9s
 => [build 3/8] WORKDIR /src                                    0.3s
 => [stage-1 3/5] WORKDIR /app                                  0.1s
 => [build 4/8] COPY httpaceproxycpp/CMakeLists.txt ./httpacep  0.0s
 => [build 5/8] COPY httpaceproxycpp/include ./httpaceproxycpp  0.0s
 => [build 6/8] COPY httpaceproxycpp/src ./httpaceproxycpp/src  0.0s
 => [build 7/8] COPY httpaceproxycpp/tests ./httpaceproxycpp/t  0.0s
 => [build 8/8] RUN cmake -S /src/httpaceproxycpp -B /build -  83.8s
 => [stage-1 4/5] COPY --from=build /build/httpaceproxycpp /ap  0.1s
 => [stage-1 5/5] COPY httpaceproxycpp/http /app/http           0.1s
 => exporting to image                                         19.4s
 => => exporting layers                                        16.4s
 => => exporting manifest sha256:d2be295991cbb1adf10bbf14a0175  0.0s
 => => exporting config sha256:d8b45a6544ffb95f3ddd3ffb06a5d99  0.0s
 => => exporting attestation manifest sha256:8b4acac10c6600fbc  0.0s
 => => exporting manifest list sha256:373fb49b38a8aef1363e8406  0.0s
 => => naming to docker.io/library/httpaceproxy-httpaceproxy:l  0.0s
 => => unpacking to docker.io/library/httpaceproxy-httpaceprox  2.9s
 => resolving provenance for metadata file                      0.1s
[+] build 1/1
 ✔ Image httpaceproxy-httpaceproxy Built                       137.0s
```
**Exit Code**: `0`

### 2. Salida Exacta de `ctest`
```
Internal ctest changing into directory: /build
UpdateCTestConfiguration  from :/build/DartConfiguration.tcl
UpdateCTestConfiguration  from :/build/DartConfiguration.tcl
Test project /build
Constructing a list of tests
Done constructing a list of tests
Updating test list for fixtures
Added 0 tests to meet fixture requirements
Checking test dependency graph...
Checking test dependency graph end
test 1
    Start 1: httpaceproxycpp_tests

1: Test command: /build/httpaceproxycpp_tests
1: Working Directory: /build
1: Test timeout computed to be: 10000000
1: httpaceproxycpp core tests passed
1/1 Test #1: httpaceproxycpp_tests ............   Passed    0.02 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.02 sec
```
**Resultado**: `100% tests passed, 0 tests failed`.
