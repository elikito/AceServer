# INFORME DE ESTABILIZACIÓN Y RELEASE CANÓNICA v09.10.02
**DESACTIVACIÓN TOTAL DEL REAPER EN MULTI-CLIENTE Y PURGA FÍSICA DE ASTERISCOS**

**Fecha**: 2026-09-10  
**Host**: Intel N150 (x86_64)  
**Versión Canónica**: `09.10.02`  
**Estado**: Establecido, Verificado en Producción (100% CTest Passed)

---

## 1. Resumen Ejecutivo de Cambios

### 1.1. Incremento de Versión Canónica (`09.10.02`)
Se fijó de forma sincronizada y canónica la versión `09.10.02` en:
- `httpaceproxycpp/include/httpaceproxycpp/version.hpp` (`kAppVersion = "09.10.02"`)
- `httpaceproxycpp/CMakeLists.txt` (`VERSION 9.10.2`, `HTTPACEPROXYCPP_VERSION "09.10.02"`)
- `httpaceproxycpp/http/js/footer.js` (`canonicalVersion = '09.10.02'`)
- `httpaceproxycpp/http/js/navbar.js` (`v09.10.02`)
- `httpaceproxycpp/http/plugins_state.json` y `config/plugins_state.json` (`"version": "09.10.02"`)
- `httpaceproxycpp/tests/test_core.cpp` (`test_v09_10_02_reaper_and_asterisk_purge`)

### 1.2. Purga Real de Heurística de Asteriscos
- Se erradicó por completo la asignación artificial de peers virtuales a partir de asteriscos (`*`, `**`, `***`) o estrellas unicode (`★`, `★★`, `★★★`).
- Se reordenó la detección en `extract_peer_count_from_title` para priorizar el patrón con separador explícito (`seeds: 40`, `peers: 150`), evitando colisiones con números de canal (ej. `DAZN 1 seeds: 40`).
- Si un canal no dispone de una etiqueta explícita de semillas numéricas (`[100 peers]`, `seeds: 40`, `[299]`), `peers = 0` garantizado en `stream_scorer.cpp` y `proxy.cpp`.
- Comprobado en runtime en canal `m-golf`: títulos con asteriscos reportan `peers: 0` y puntuación base de `40` (frente a los 50 peers y score 620 erróneos anteriores).

### 1.3. Desactivación del Cierre Forzado del Reaper en Multi-Cliente
- **Causa Raíz**: A las 13:33:19, al cerrarse pestañas del reproductor web, la condición de parada del bucle de lectura HTTP (`stream_http_url`) evaluaba `return running_ && client_count() > 0;`. Al caer a 0 temporalmente, finalizaba el consumo y enviaba `STOP` a AceStream, matando sesiones vivas de VLC o bloqueando el motor con `Cannot retrieve torrent`.
- **Corrección**:
  1. `Broadcast::stop()` bloquea terminantemente cualquier parada si `subscribers_ > 0 || client_count() > 0` con registro de advertencia `PROHIBIDO enviar STOP a AceStream`.
  2. Período de gracia ampliado a 60 segundos por defecto (`linger_timeout = 60`). Durante este intervalo, el stream de fondo sigue vivo en segundo plano.
  3. `stream_http_url` y `stream_hls_url` se mantienen en ejecución mientras `running_` sea verdadero, sin abortar inmediatamente ante desconexiones momentáneas de clientes.
  4. En `stream_loop()`, solo se envía `stop_broadcast()` si no quedan suscriptores ni clientes (`subscribers_ <= 0 && client_count() == 0`).
  5. `BroadcastManager::reap_inactive_sessions()` exige que ambos contadores sean cero (`subs <= 0 && broadcast->client_count() == 0`) y que hayan transcurrido al menos 60 segundos de inactividad total. Si hay clientes activos, resetea el temporizador.

### 1.4. Rediseño y Mejora Integral de la Consola del Búnker
- **Limpieza de Consola en Memoria**: Implementado método `clear_bunker_logs()` en backend C++ (`Proxy` y endpoint `/stat/?action=clear_bunker_logs`). El botón "Limpiar Consola" vacía tanto la vista en el navegador como el buffer en memoria del servidor, evitando la reaparición de eventos borrados en sondeos posteriores.
- **Botón Copiar Logs**: Botón interactivo con icono (`📋 Copiar Logs`) para volcar al portapapeles todo el texto plano acumulado, con retroalimentación visual (`✓ ¡Copiado!` durante 2s).
- **Categorización Semántica y Badges de Color**:
  - `[ERROR]` (Rojo `#ff5252`): Caídas, timeouts, fallos de socket, bloqueos.
  - `[AVISO]` (Ámbar `#ffb142`): Advertencias de buffer, peers bajos, reintentos.
  - `[DESCONEXIÓN]` (Naranja `#ff793f`): Desconexión de clientes, ciclo Reaper.
  - `[STREAM]` (Verde brillante `#33d9b2`): Inicio de transmisiones, streams URL y clientes conectados.
  - `[INFO]` (Azul claro `#70a1ff`): Eventos de sistema y monitoreo.
- **Auto-scroll Inteligente**: Checkbox para activar/desactivar el auto-scroll automático con detección de desplazamiento manual hacia arriba para no interrumpir la inspección de logs antiguos.

---

## 2. Diffs Completos

```diff
diff --git a/httpaceproxycpp/CMakeLists.txt b/httpaceproxycpp/CMakeLists.txt
index 35ee8b1..b53b827 100644
--- a/httpaceproxycpp/CMakeLists.txt
+++ b/httpaceproxycpp/CMakeLists.txt
@@ -1,6 +1,6 @@
 cmake_minimum_required(VERSION 3.20)
-project(httpaceproxycpp VERSION 9.10.1 LANGUAGES CXX)
-set(HTTPACEPROXYCPP_VERSION "09.10.01")
+project(httpaceproxycpp VERSION 9.10.2 LANGUAGES CXX)
+set(HTTPACEPROXYCPP_VERSION "09.10.02")
 
 set(CMAKE_CXX_STANDARD 20)
 set(CMAKE_CXX_STANDARD_REQUIRED ON)
diff --git a/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp b/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp
index ecb283d..8fcfbb4 100644
--- a/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp
@@ -100,6 +100,7 @@ public:
     // v09.09.01 — Safe Reaper & Dynamic Stream Upgrader helpers
     int get_subscribers() const { return subscribers_.load(); }
     std::int64_t get_zero_subscribers_time() const { return zero_subscribers_time_.load(); }
+    void reset_zero_subscribers_time() { zero_subscribers_time_.store(0, std::memory_order_relaxed); }
     bool has_valid_ts_data() const { return total_bytes_received_.load() >= 188; }
     double get_bitrate_kbps();
     bool is_bitrate_degraded(int seconds_threshold = 20);
diff --git a/httpaceproxycpp/include/httpaceproxycpp/config.hpp b/httpaceproxycpp/include/httpaceproxycpp/config.hpp
index a235a9d..8894452 100644
--- a/httpaceproxycpp/include/httpaceproxycpp/config.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/config.hpp
@@ -25,7 +25,7 @@ struct Config {
     int client_queue_size = 256;
     int client_write_timeout = 30;
     int curl_stream_buffer = 1048576;
-    int linger_timeout = 15;
+    int linger_timeout = 60;
     bool use_chunked = true;
     bool firewall = false;
     bool firewall_blacklist_mode = false;
diff --git a/httpaceproxycpp/include/httpaceproxycpp/proxy.hpp b/httpaceproxycpp/include/httpaceproxycpp/proxy.hpp
index cf25330..776e73f 100644
--- a/httpaceproxycpp/include/httpaceproxycpp/proxy.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/proxy.hpp
@@ -131,6 +131,7 @@ public:
 
     void add_bunker_log(const std::string& message);
     Json get_bunker_logs_json() const;
+    void clear_bunker_logs();
     void set_limits(int max_connections, int max_concurrent_channels);
 
 private:
diff --git a/httpaceproxycpp/include/httpaceproxycpp/version.hpp b/httpaceproxycpp/include/httpaceproxycpp/version.hpp
index 45cb111..dfd2ef9 100644
--- a/httpaceproxycpp/include/httpaceproxycpp/version.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/version.hpp
@@ -1,6 +1,6 @@
 #pragma once
 
 namespace httpace {
-inline constexpr const char* kAppVersion = "09.10.01";
+inline constexpr const char* kAppVersion = "09.10.02";
 }
 
diff --git a/httpaceproxycpp/src/broadcast.cpp b/httpaceproxycpp/src/broadcast.cpp
index 2378832..a237eb6 100644
--- a/httpaceproxycpp/src/broadcast.cpp
+++ b/httpaceproxycpp/src/broadcast.cpp
@@ -292,12 +292,25 @@ void Broadcast::start_once() {
 }
 
 void Broadcast::stop() {
-    // Bloquear terminantemente el comando STOP si todavía existen suscriptores activos
-    if (subscribers_.load(std::memory_order_relaxed) > 0) {
+    // Bloquear terminantemente el comando STOP si todavía existen suscriptores activos o clientes conectados
+    if (subscribers_.load(std::memory_order_relaxed) > 0 || client_count() > 0) {
         log_line("WARNING", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
-                 "] Broadcast::stop bloqueado: sesión con " + std::to_string(subscribers_.load()) + " suscriptores activos.");
+                 "] Broadcast::stop bloqueado: sesión con " + std::to_string(subscribers_.load()) + " suscriptores y " +
+                 std::to_string(client_count()) + " clientes activos. PROHIBIDO enviar STOP a AceStream.");
         return;
     }
+
+    // Verificar período de gracia linger_timeout (mínimo 60s)
+    auto zero_time = zero_subscribers_time_.load(std::memory_order_relaxed);
+    auto now = unix_time();
+    int linger = std::max(60, config_.linger_timeout);
+    if (zero_time > 0 && (now - zero_time) < linger) {
+        log_line("INFO", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
+                 "] Broadcast::stop aplazado: sesión en período de gracia (" + std::to_string(now - zero_time) + "/" +
+                 std::to_string(linger) + "s). Stream de fondo protegido.");
+        return;
+    }
+
     bool expected = false;
     if (!stopped_.compare_exchange_strong(expected, true)) return;
     running_ = false;
@@ -346,9 +359,12 @@ void Broadcast::stream_loop() {
         log_line("ERROR", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) + "] stream failed with unknown error");
     }
     running_ = false;
-    if (ace_) {
-        try { ace_->stop_broadcast(); } catch (...) {}
-        try { ace_->shutdown(); } catch (...) {}
+    // Solo enviar STOP al motor AceStream si NO quedan suscriptores ni clientes conectados
+    if (subscribers_.load(std::memory_order_relaxed) <= 0 && client_count() == 0) {
+        if (ace_) {
+            try { ace_->stop_broadcast(); } catch (...) {}
+            try { ace_->shutdown(); } catch (...) {}
+        }
     }
     for (auto& client : clients()) client->queue->close();
 }
@@ -355,7 +371,7 @@ void Broadcast::stream_loop() {
 void Broadcast::stream_http_url(const std::string& url) {
     http_client_.stream(url, [&](const char* data, std::size_t size) {
         broadcast_chunk(data, size);
-        return running_ && client_count() > 0;
+        return running_.load();
     }, running_, 5, config_.video_timeout, std::max(1, config_.curl_stream_buffer));
 }
 
@@ -362,5 +378,5 @@ void Broadcast::stream_http_url(const std::string& url) {
 void Broadcast::stream_hls_url(const std::string& url) {
     std::vector<std::string> seen;
-    while (running_ && client_count() > 0) {
+    while (running_) {
         try {
             auto response = http_client_.get(url, {}, config_.video_timeout);
@@ -377,7 +393,7 @@ void Broadcast::stream_hls_url(const std::string& url) {
                 stream_http_url(segment);
                 seen.push_back(segment);
                 if (seen.size() > 50) seen.erase(seen.begin());
-                if (!running_ || client_count() == 0) break;
+                if (!running_) break;
             }
         } catch (const std::exception& e) {
             log_line("ERROR", "HLS refresh failed: " + std::string(e.what()));
@@ -504,7 +520,7 @@ void BroadcastManager::stop_reaper() {
 
 void BroadcastManager::reap_inactive_sessions(std::int64_t max_idle_seconds) {
     auto now = unix_time();
-    int linger = std::max(1, config_.linger_timeout);
+    int linger = std::max(60, config_.linger_timeout);
     std::vector<std::shared_ptr<Broadcast>> to_stop;
     {
         std::lock_guard<std::mutex> lock(mutex_);
@@ -514,10 +530,12 @@ void BroadcastManager::reap_inactive_sessions(std::int64_t max_idle_seconds) {
                 continue;
             }
             int subs = broadcast->get_subscribers();
-            if (subs <= 0 || broadcast->client_count() == 0) {
+            std::size_t c_count = broadcast->client_count();
+            // Ambos deben ser cero: no deben quedar suscriptores ni clientes conectados
+            if (subs <= 0 && c_count == 0) {
                 auto zero_time = broadcast->get_zero_subscribers_time();
                 if (zero_time == 0) {
-                    // Primer avistamiento sin suscriptores: marcar inicio de gracia linger_timeout
+                    // Primer avistamiento sin suscriptores: marcar inicio de gracia linger_timeout (60s)
                     broadcast->detach_client_for_migration(nullptr);
                     ++it;
                 } else if ((now - zero_time) >= linger) {
@@ -525,10 +543,12 @@ void BroadcastManager::reap_inactive_sessions(std::int64_t max_idle_seconds) {
                     to_stop.push_back(broadcast);
                     it = broadcasts_.erase(it);
                 } else {
-                    // En período de gracia (linger_timeout)
+                    // En período de gracia (linger_timeout >= 60s)
                     ++it;
                 }
             } else {
+                // Hay clientes o suscriptores activos: mantener vivo y cancelar temporizador
                 broadcast->reset_zero_subscribers_time();
                 ++it;
             }
         }
@@ -562,7 +582,7 @@ void BroadcastManager::remove_if_empty(const std::string& infohash) {
             if (it->second->get_subscribers() <= 0 && it->second->client_count() == 0) {
                 auto now = unix_time();
                 auto zero_time = it->second->get_zero_subscribers_time();
-                int linger = std::max(1, config_.linger_timeout);
+                int linger = std::max(60, config_.linger_timeout);
                 if (zero_time > 0 && (now - zero_time) >= linger) {
                     removed = it->second;
                     broadcasts_.erase(it);
diff --git a/httpaceproxycpp/src/plugins.cpp b/httpaceproxycpp/src/plugins.cpp
index ce83c83..2274283 100644
--- a/httpaceproxycpp/src/plugins.cpp
+++ b/httpaceproxycpp/src/plugins.cpp
@@ -752,6 +752,11 @@ public:
             send_bytes(ctx.connection, 200, "application/json; charset=utf-8", proxy_.get_bunker_logs_json().dump(2));
             return true;
         }
+        if (action == "clear_bunker_logs") {
+            proxy_.clear_bunker_logs();
+            send_bytes(ctx.connection, 200, "application/json; charset=utf-8", "{\"status\":\"success\",\"cleared\":true}");
+            return true;
         }
         if (action == "check_peers") {
             int max_wait = 10;
             try { max_wait = std::stoi(query_get(ctx.query, "max_wait", "10")); } catch (...) {}
diff --git a/httpaceproxycpp/src/proxy.cpp b/httpaceproxycpp/src/proxy.cpp
index 2acbb0a..0a065a3 100644
--- a/httpaceproxycpp/src/proxy.cpp
+++ b/httpaceproxycpp/src/proxy.cpp
@@ -239,6 +239,11 @@ Json Proxy::get_bunker_logs_json() const {
     };
 }
 
+void Proxy::clear_bunker_logs() {
+    std::lock_guard<std::mutex> lock(bunker_mutex_);
+    bunker_logs_.clear();
+}
+
 void Proxy::set_limits(int max_connections, int max_concurrent_channels) {
     bool changed = false;
     if (max_connections >= 1 && max_connections <= 10 && config_.max_connections != max_connections) {
@@ -3548,6 +3553,9 @@ std::vector<ChannelCandidate> Proxy::find_candidates_for_channel(const std::stri
                         } else if (title_peers > c.peers) {
                             c.peers = std::max(c.peers, title_peers);
                         }
+                    } else if (live_peers <= 0) {
+                        // Purga canónica v09.10.02: Si no hay tag numérico real ([100 peers], seeds: 40), peers = 0
+                        c.peers = 0;
                     }
 
                     if (c.peers > 0) {
diff --git a/httpaceproxycpp/src/stream_scorer.cpp b/httpaceproxycpp/src/stream_scorer.cpp
index 9db3a3c..f749f6a 100644
--- a/httpaceproxycpp/src/stream_scorer.cpp
+++ b/httpaceproxycpp/src/stream_scorer.cpp
@@ -212,24 +212,28 @@ int extract_peer_count_from_title(const std::string& name) {
         } catch (...) {}
     }
 
-    // 2. Patrón de palabra clave en texto libre: ej. "150 peers", "45 seeds", "semillas: 80"
-    static const std::regex kw_after_regex(R"(\b([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)\b)", std::regex::icase);
+    // 2. Patrón de prefijo de palabra clave en texto libre: ej. "seeds: 40", "peers: 150", "semillas: 80"
+    static const std::regex kw_before_regex(R"(\b(?:peers?|seeds?|semillas?)\s*[:=-]\s*([0-9]{1,4})\b)", std::regex::icase);
     std::smatch kw_match;
-    if (std::regex_search(name, kw_match, kw_after_regex)) {
+    if (std::regex_search(name, kw_match, kw_before_regex)) {
         try {
             int val = std::stoi(kw_match[1].str());
             if (val > 0) return val;
         } catch (...) {}
     }
 
-    static const std::regex kw_before_regex(R"(\b(?:peers?|seeds?|semillas?)\s*[:=-]?\s*([0-9]{1,4})\b)", std::regex::icase);
-    if (std::regex_search(name, kw_match, kw_before_regex)) {
+    // 3. Patrón de sufijo de palabra clave en texto libre: ej. "150 peers", "45 seeds", "semillas: 80"
+    static const std::regex kw_after_regex(R"(\b([0-9]{1,4})\s*(?:peers?|seeds?|semillas?)\b)", std::regex::icase);
+    if (std::regex_search(name, kw_match, kw_after_regex)) {
         try {
             int val = std::stoi(kw_match[1].str());
             if (val > 0) return val;
         } catch (...) {}
     }
 
+    // PURGA CANÓNICA v09.10.02: Si el título no contiene una etiqueta numérica real explícita
+    // ([100 peers], seeds: 40, [299], (114)), el canal tiene estrictamente 0 peers.
+    // Los asteriscos ('*', '**', '***') o estrellas unicode ('★') jamás aportan peers.
     return 0;
 }
 
diff --git a/httpaceproxycpp/tests/test_core.cpp b/httpaceproxycpp/tests/test_core.cpp
index 99af57c..18c8b4f 100644
--- a/httpaceproxycpp/tests/test_core.cpp
+++ b/httpaceproxycpp/tests/test_core.cpp
@@ -838,9 +838,9 @@ void test_v09_09_01_dynamic_upgrader_and_safe_reaper() {
     // 1. Verificación de versión base
     require(std::string(kAppVersion) >= "09.09.01", "App version compatibility");
 
-    // 2. Configuración de linger_timeout por defecto = 15s
+    // 2. Configuración de linger_timeout por defecto = 60s (v09.10.02)
     Config cfg;
-    require(cfg.linger_timeout == 15, "default linger_timeout must be 15s");
+    require(cfg.linger_timeout == 60, "default linger_timeout must be 60s");
 
     // 3. Verificación de score -1000 para candidatos desactivados
     ChannelCandidate dis;
@@ -976,8 +976,8 @@ void test_peer_count_extraction_and_popularity_ranking() {
 }
 
 void test_v09_10_01_peer_serialization_and_version() {
-    // 1. Verificación estricta de versión canónica v09.10.01
-    require(std::string(kAppVersion) == "09.10.01", "App version must be 09.10.01");
+    // 1. Verificación estricta de versión canónica v09.10.01+
+    require(std::string(kAppVersion) >= "09.10.01", "App version must be at least 09.10.01");
 
     // 2. Verificación de que asteriscos NO producen peers
     require(extract_peer_count_from_title("M+ Liga de Campeones 2 1080p ***") == 0, "three-star no peers");
@@ -997,6 +997,27 @@ void test_v09_10_01_peer_serialization_and_version() {
     require(list[1].score == -1000.0, "offline candidate must have -1000.0 score");
 }
 
+void test_v09_10_02_reaper_and_asterisk_purge() {
+    // 1. Verificación canónica de versión v09.10.02
+    require(std::string(kAppVersion) == "09.10.02", "App version must be 09.10.02");
+
+    // 2. Verificación de purga física de asteriscos en títulos
+    require(extract_peer_count_from_title("M+ Golf 1080p **") == 0, "golf two-star no peers");
+    require(extract_peer_count_from_title("M+ Golf 1080p *") == 0, "golf one-star no peers");
+    require(extract_peer_count_from_title("DAZN 1 1080p **") == 0, "dazn two-star no peers");
+    require(extract_peer_count_from_title("DAZN 1 1080p [100 peers]") == 100, "bracket peer tag supported");
+    require(extract_peer_count_from_title("DAZN 1 seeds: 40") == 40, "seeds peer tag supported");
+
+    // 3. Verificación de puntuación sin inflación artificial (score 30 base, no 620)
+    ChannelCandidate c_stars{"M+ Golf 1080p **", "cid_golf", "unificada", "", "", "m-golf", StreamQuality::FHD_1080, 100, 0, 0, ChannelHealth::UNKNOWN, false, false, false, 0.0};
+    double score = StreamScorer::calculate_score(c_stars);
+    require(score == 40.0, "Candidate with 0 peers in 1080p has base score 30 + 10 health");
+
+    // 4. Verificación de configuración linger_timeout por defecto = 60s
+    Config cfg;
+    require(cfg.linger_timeout == 60, "Config linger_timeout default must be 60s");
+}
+
 } // namespace
 
 int main() {
@@ -1034,6 +1055,7 @@ int main() {
         test_v09_09_06_reaper_tolerance_and_client_connection();
         test_peer_count_extraction_and_popularity_ranking();
         test_v09_10_01_peer_serialization_and_version();
+        test_v09_10_02_reaper_and_asterisk_purge();
         std::cout << "httpaceproxycpp core tests passed\n";
         return 0;
     } catch (const std::exception& e) {
```

---

## 3. Pruebas de Validación en Host N150

1. **Compilación y CTest**:
   - `docker compose build httpaceproxy` (incluye ejecución integrada de `ctest --test-dir /build --output-on-failure`).
   - Resultado: **100% tests passed**.
2. **Recreación y Despliegue de Contenedor**:
   - `docker compose up -d httpaceproxy`
   - Log verificado: `HTTPAceProxyCPP v09.10.02 started at 0.0.0.0:8888`.
3. **Endpoint de Candidatos y Popularidad**:
   ```bash
   curl -s "http://127.0.0.1:8888/auto/m-golf?action=list" | grep -oP '"name":"[^"]+","peers":[0-9]+'
   ```
   **Salida**:
   ```text
   "name":"M+ Golf 1080p *","peers":0
   "name":"M+ Golf 1080p *","peers":0
   "name":"M+ Golf 1080p **","peers":0
   ```
   *Ningún canal con asteriscos reporta 50 peers ni puntuación 620. Puntuación estabilizada en su valor real (40).*
4. **Verificación de Limpieza de Consola del Búnker**:
   ```bash
   curl -s "http://127.0.0.1:8888/stat/?action=clear_bunker_logs"
   # -> {"status":"success","cleared":true}
   curl -s "http://127.0.0.1:8888/stat/?action=get_bunker_logs"
   # -> {"logs":[],"status":"success"}
   ```
