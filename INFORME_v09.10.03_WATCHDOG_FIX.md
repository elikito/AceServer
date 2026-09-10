# INFORME DE ESTABILIZACIÓN Y RELEASE v09.10.03: ELIMINACIÓN DE STOP PREMATURO Y SELECCIÓN ESTRICTA POR PEERS

**Fecha de Aplicación**: 10 de Septiembre de 2026  
**Versión Canónica**: `09.10.03`  
**Host de Despliegue**: Servidor de Producción N150 (`/opt/HTTPAceProxy`)  
**Estado de Verificación**: CTest 100% Passed · Docker Build Verified · Git Sincronizado  

---

## 1. Resumen Ejecutivo

En la versión previa `09.10.02`, tras la purga física de las heurísticas de asteriscos y la estabilización del Reaper, se observaron dos incidencias críticas en el log de producción durante la sintonización de canales de alta demanda:
1. **Envío de STOP prematuro a los 5 segundos**:
   ```log
   [19:30:35] DEBUG [idleAce] >>> STOP
   ```
   El proxy confundía la latencia natural de handshake y prebuffering del motor AceStream (estados `loading`, `starting`, `dl`, `buf`, `prebuf`) con un fallo o caída del stream, provocando el cierre abrupto del socket y dejando al reproductor (VLC / Web) con error 502 o `Cannot retrieve torrent`.
2. **Selección de candidatos con 0 peers frente a candidatos con peers reales**:
   En canales con múltiples emisiones concurrentes (por ejemplo, *M+ Golf* vs *Movistar Golf*), un candidato con 0 peers (score 80) era seleccionado frente a una fuente saludable con 5 peers reales (score 170) debido a discrepancias en el ordenamiento por calidad nominal frente a salud real y a la interferencia de tags `[5 peers]` en la extracción del slug canónico.

La release **`v09.10.03`** erradica ambas anomalías de forma definitiva:
- **Protección de Inicio del Watchdog (25 segundos)**: Prohibición absoluta de enviar comando `STOP` a AceStream durante los primeros 25 segundos de vida de la sesión si el motor respondió con éxito o se encuentra en fase de conexión/prebuffering.
- **Prioridad Absoluta a Peers Reales**: En el algoritmo de ordenamiento (`StreamScorer::rank_candidates`), cualquier candidato con `peers > 0` se sitúa estrictamente por delante de cualquier candidato con `0 peers`, sin importar la etiqueta de calidad nominal.
- **Normalización de Tags de Peers en Títulos**: Nueva función `strip_peer_tags()` que elimina etiquetas numéricas de peers (`[5 peers]`, `seeds: 40`, `[120]`) antes de derivar el slug y nombre canónico, garantizando que todos los alias converjan al mismo canal virtual.

---

## 2. Detección de Causa Raíz y Solución Técnica

### 2.1. Watchdog de Inicio y Supresión de STOP Prematuro
- **Problema**: El proxy ejecutaba la comprobación de stream e inactividad antes de que AceStream completara la fase de pre-buffer. Si el cliente o el lector HTTP tardaban unos segundos en negociar el primer chunk, el hilo entraba en la condición de desconexión y enviaba `STOP` y `shutdown()` al motor AceStream.
- **Solución**:
  - Se registra `start_time_` al invocar `start_once()` y `stream_loop()`.
  - Tanto en `Broadcast::stop()` como en `Broadcast::stream_loop()` y en el ciclo de lectura de `Proxy::handle_core_stream()`, se impone una comprobación estricta:
    ```cpp
    auto now = unix_time();
    auto start_t = start_time_.load(std::memory_order_relaxed);
    if (start_t > 0 && (now - start_t) < 25) {
        auto st = get_p2p_status();
        std::string status_val = st.contains("status") ? lower(st.at("status")) : "";
        if (status_val == "loading" || status_val == "starting" || status_val == "dl" ||
            status_val == "buf" || status_val == "prebuf" || status_val == "wait" || status_val.empty()) {
            // PROHIBIDO enviar STOP. Esperar datos de AceStream.
            return;
        }
    }
    ```
  - En `AceClient::stop_broadcast()`, se añade un guardián `start_issued_`: si no hay una orden `START` activa emitida por esta instancia de cliente, la llamada se descarta de forma segura sin emitir comandos `STOP` espurios por la conexión de control.

### 2.2. Prioridad Estricta a Fuentes con Peers Reales
- **Problema**: Un canal en 1080p con 0 peers sumaba 30 puntos base + 50 de estado ONLINE = 80 puntos. Si un canal con 5 peers reales no tenía la etiqueta exacta de resolución o difería en el nombre, el desempate por score anterior no penalizaba suficientemente el valor crítico de `peers == 0`.
- **Solución**:
  - En `StreamScorer::rank_candidates`, se implementó una regla de partición primaria:
    ```cpp
    // 2. Prioridad absoluta: candidatos con peers > 0 DEBEN ordenarse por encima de cualquier candidato con 0 peers
    bool a_has_peers = (a.peers > 0);
    bool b_has_peers = (b.peers > 0);
    if (a_has_peers != b_has_peers) {
        return a_has_peers;
    }
    ```
  - De este modo, una emisión SD con 1 o más peers verificados siempre prevalecerá sobre un stream 1080p fantasma con 0 peers.

---

## 3. Matriz de Ficheros Modificados

| Fichero | Tipo | Descripción |
| :--- | :--- | :--- |
| `httpaceproxycpp/include/httpaceproxycpp/version.hpp` | MODIFICADO | Actualizado `kAppVersion` a `"09.10.03"` |
| `httpaceproxycpp/CMakeLists.txt` | MODIFICADO | Versión `9.10.3` y `HTTPACEPROXYCPP_VERSION "09.10.03"` |
| `httpaceproxycpp/http/js/footer.js` | MODIFICADO | Actualizado `canonicalVersion = '09.10.03'` |
| `httpaceproxycpp/http/js/navbar.js` | MODIFICADO | Actualizado encabezado y badge a `(v09.10.03)` |
| `httpaceproxycpp/http/plugins_state.json` | MODIFICADO | Actualizado `"version": "09.10.03"` |
| `config/plugins_state.json` | MODIFICADO | Actualizado `"version": "09.10.03"` |
| `httpaceproxycpp/include/httpaceproxycpp/broadcast.hpp` | MODIFICADO | Añadidos miembros atómicos `start_time_` y `get_start_time()` |
| `httpaceproxycpp/src/ace_client.cpp` | MODIFICADO | Guardián `start_issued_` para evitar llamadas `STOP` duplicadas o prematuras |
| `httpaceproxycpp/src/broadcast.cpp` | MODIFICADO | Ventana de inicio protegida de 25 segundos y timeout de conexión curl de 30s |
| `httpaceproxycpp/src/proxy.cpp` | MODIFICADO | Protección de prebuffering de 25s en `handle_core_stream` |
| `httpaceproxycpp/src/stream_scorer.cpp` | MODIFICADO | Prioridad estricta `peers > 0` en `rank_candidates` y función `strip_peer_tags` |
| `httpaceproxycpp/tests/test_core.cpp` | MODIFICADO | Nueva suite `test_v09_10_03_watchdog_and_real_peer_selection()` |

---

## 4. Diffs de Código Principales

### 4.1. Ventana de Inicio Protegida (`httpaceproxycpp/src/broadcast.cpp`)
```diff
@@ -285,6 +285,7 @@ std::map<std::string, std::string> Broadcast::get_p2p_status() const {
 void Broadcast::start_once() {
     bool expected = false;
     if (started_.compare_exchange_strong(expected, true)) {
+        start_time_.store(unix_time(), std::memory_order_relaxed);
         running_ = true;
         stream_thread_ = std::thread(&Broadcast::stream_loop, this);
@@ -300,9 +301,23 @@ void Broadcast::stop() {
         return;
     }
 
+    // 1. Prohibido enviar STOP a AceStream durante los primeros 25 segundos de inicio si el motor respondió con éxito o está en loading/starting/dl/buf/prebuf
+    auto start_t = start_time_.load(std::memory_order_relaxed);
+    auto now = unix_time();
+    if (start_t > 0 && (now - start_t) < 25) {
+        auto st = get_p2p_status();
+        std::string status_val = st.contains("status") ? lower(st.at("status")) : "";
+        if (status_val == "loading" || status_val == "starting" || status_val == "dl" ||
+            status_val == "buf" || status_val == "prebuf" || status_val == "wait" || status_val.empty()) {
+            log_line("INFO", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
+                     "] Broadcast::stop bloqueado: en ventana de inicio protegida (" +
+                     std::to_string(now - start_t) + "/25s, status: " + status_val + "). PROHIBIDO enviar STOP.");
+            return;
+        }
+    }
```

### 4.2. Prioridad de Selección por Peers Reales (`httpaceproxycpp/src/stream_scorer.cpp`)
```diff
@@ -305,6 +319,24 @@ void StreamScorer::rank_candidates(std::vector<ChannelCandidate>& candidates) {
     }
 
     std::stable_sort(candidates.begin(), candidates.end(), [](const ChannelCandidate& a, const ChannelCandidate& b) {
+        // 1. Candidatos deshabilitados o en error/offline/bloqueados (-1000.0) siempre al final
+        bool a_valid = !a.is_disabled && a.score > -1000.0;
+        bool b_valid = !b.is_disabled && b.score > -1000.0;
+        if (a_valid != b_valid) {
+            return a_valid;
+        }
+        if (!a_valid) {
+            return a.score > b.score;
+        }
+
+        // 2. Prioridad absoluta: candidatos con peers > 0 DEBEN ordenarse por encima de cualquier candidato con 0 peers
+        bool a_has_peers = (a.peers > 0);
+        bool b_has_peers = (b.peers > 0);
+        if (a_has_peers != b_has_peers) {
+            return a_has_peers;
+        }
+
+        // 3. Desempate por score
         return a.score > b.score;
     });
 }
```

---

## 5. Validación y Resultados de Pruebas

### 5.1. Suites de Pruebas Unitarias (`ctest`)
- `test_hashes`: PASSED
- `test_url_helpers`: PASSED
- `test_json`: PASSED
- `test_playlist`: PASSED
- `test_m3u_parser_variants`: PASSED
- `test_stream_scorer`: PASSED
- `test_warp_and_resolution_variants`: PASSED
- `test_channel_verifier_scoring`: PASSED
- `test_p2p_channel_health_and_stability`: PASSED
- `test_recheck_and_dedup`: PASSED
- `test_stream_candidate_sorting`: PASSED
- `test_v09_07_01_smart_reaper`: PASSED
- `test_v09_07_02_regex_filters`: PASSED
- `test_v09_08_01_mobile_and_quality_filter`: PASSED
- `test_v09_08_02_zero_subscribers_and_hls`: PASSED
- `test_v09_08_03_curl_chunk_optimization`: PASSED
- `test_v09_08_04_session_isolation_and_linger`: PASSED
- `test_v09_08_05_dynamic_epg_and_m3u_generation`: PASSED
- `test_v09_08_06_channel_candidate_metadata_and_recheck`: PASSED
- `test_peer_count_extraction_and_popularity_ranking`: PASSED
- `test_v09_10_01_peer_serialization_and_version`: PASSED
- `test_v09_10_02_reaper_and_asterisk_purge`: PASSED
- `test_v09_10_03_watchdog_and_real_peer_selection`: PASSED
- **Resultado Total**: `100% tests passed, 0 tests failed out of 1`

---

## 6. Conclusión de Despliegue

La versión `09.10.03` deja el motor HTTP Ace Proxy completamente blindado frente a cierres anticipados de sesiones P2P en transitorios de buffering, garantiza la selección de las emisiones con mayor enjambre real de seeds y preserva la compatibilidad transparente con todos los clientes y listas.
