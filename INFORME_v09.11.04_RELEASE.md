# INFORME TÉCNICO DE RELEASE v09.11.04
**Escalabilidad a 64 Workers HTTP, Sockets TCP_NODELAY (256 KB) y Blindaje Anti-Colisión en Failover Concurrente**

**Fecha:** 11 de Septiembre de 2026  
**Versión Canónica:** `09.11.04`  
**Host de Despliegue:** N100 / N150 (`/opt/HTTPAceProxy`)  
**Estado:** Verificado y Compilado en Producción (100% Tests Unitarios Aprobados en Docker)

---

## 1. Resumen Ejecutivo de la Release

En base a la auditoría técnica documentada en `INFORME_VALORACION_ARQUITECTURA.md`, se identificaron dos fallas críticas en la gestión de concurrencia de HTTPAceProxy:
1. **Inanición del `ThreadPool` HTTP**: El servidor limitaba sus workers a $\min(16, \text{hardware\_concurrency} \times 2)$. En CPUs Intel N100 (4 cores / 4 threads), esto asignaba únicamente **8 hilos**. Al realizar un streaming síncrono mediante `Proxy::handle_core_stream`, la conexión de 5 clientes consumía 5 hilos de forma permanente, agotando el pool ante peticiones concurrentes del panel web, API REST o EPG y provocando errores HTTP 503.
2. **Colisión Destructiva en Failover (`force_stop_broadcast`)**: Ante un timeout de 6 segundos o cierre de cola en canales virtuales `/auto/<slug>`, el cliente que experimentaba el fallo ejecutaba `broadcasts_.force_stop_broadcast(infohash)`. Al recibir `force=true`, el método de parada forzada ignoraba a los demás suscriptores, cerraba las colas de todos los clientes conectados y enviaba un comando `STOP` fulminante a AceStream, interrumpiendo las reproducciones legítimas de otros clientes concurrentes en el mismo canal.

La versión **v09.11.04** erradica de raíz estos dos problemas, implementando una arquitectura de sockets optimizada para baja latencia y alta concurrencia.

---

## 2. Modificaciones de Código y Diffs Exactos

### 2.1. Ampliación del ThreadPool a 64 Workers (`include/httpaceproxycpp/http_server.hpp` y `src/http_server.cpp`)
Se elevó el tope de hilos de trabajo de 16 a **64** y la profundidad de cola a **512**. Asimismo, se ajustó el cálculo dinámico de workers para cargas de trabajo I/O-bound (`hardware_concurrency * 16`, capped a 64), asegurando que un procesador N100 disponga de los 64 hilos completos.

```diff
--- a/httpaceproxycpp/include/httpaceproxycpp/http_server.hpp
+++ b/httpaceproxycpp/include/httpaceproxycpp/http_server.hpp
@@ -49,8 +49,9 @@ class ThreadPool {
     std::size_t active_workers() const noexcept { return active_.load(std::memory_order_relaxed); }
 
 private:
-    static constexpr std::size_t MAX_WORKERS     = 16;
-    static constexpr std::size_t DEFAULT_QUEUE_DEPTH = 256;
+    // v09.11.04 — Ampliación a 64 workers para streaming concurrente multi-cliente
+    static constexpr std::size_t MAX_WORKERS     = 64;
+    static constexpr std::size_t DEFAULT_QUEUE_DEPTH = 512;
 
     void worker_loop();
```

```diff
--- a/httpaceproxycpp/src/http_server.cpp
+++ b/httpaceproxycpp/src/http_server.cpp
@@ -269,8 +269,9 @@ void send_simple_response(ClientConnection& connection, int status, const std::s
 ThreadPool::ThreadPool(std::size_t max_workers, std::size_t max_queue)
     : max_queue_(max_queue == 0 ? DEFAULT_QUEUE_DEPTH : max_queue) {
     if (max_workers == 0) {
-        max_workers = std::min(MAX_WORKERS, static_cast<std::size_t>(std::thread::hardware_concurrency() * 2));
-        if (max_workers == 0) max_workers = 2;
+        // v09.11.04: Permitir escala completa hasta MAX_WORKERS (64) para streaming I/O concurrente
+        max_workers = std::min(MAX_WORKERS, std::max<std::size_t>(32, static_cast<std::size_t>(std::thread::hardware_concurrency() * 16)));
+        if (max_workers == 0) max_workers = MAX_WORKERS;
     }
     for (std::size_t i = 0; i < max_workers; ++i) {
         workers_.emplace_back([this] { worker_loop(); });
```

---

### 2.2. Optimización de Sockets: `TCP_NODELAY` y Buffer `SO_SNDBUF` (256 KB) (`src/http_server.cpp`)
Se incluyó `<netinet/tcp.h>` y se configuró cada socket entrante inmediatamente tras la llamada `accept()` para desactivar el algoritmo de Nagle y asignar 256 KB de buffer en kernel.

```diff
--- a/httpaceproxycpp/src/http_server.cpp
+++ b/httpaceproxycpp/src/http_server.cpp
@@ -10,6 +10,7 @@
 #include <netdb.h>
 #include <netinet/in.h>
+#include <netinet/tcp.h>
 #include <sstream>
 
@@ -156,6 +156,14 @@ void HttpServer::accept_loop() {
         }
         char ip[INET_ADDRSTRLEN] = {0};
         ::inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
+
+        // v09.11.04: TCP_NODELAY y buffer de envío de 256 KB para streaming fluido MPEG-TS sin jitter
+        int nodelay = 1;
+        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
+
+        int sndbuf = 262144; // 256 KB
+        ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
+
         if (client_send_timeout_ > 0) {
```

---

### 2.3. Blindaje de Failover contra Colisiones Multi-Cliente (`src/broadcast.cpp` y `src/proxy.cpp`)

#### A. Protección en `BroadcastManager::force_stop_broadcast`
Si la sesión registra suscriptores o clientes legítimos activos, se aborta la destrucción del broadcast:

```diff
--- a/httpaceproxycpp/src/broadcast.cpp
+++ b/httpaceproxycpp/src/broadcast.cpp
@@ -653,6 +653,16 @@ void BroadcastManager::force_stop_broadcast(const std::string& infohash) {
         std::lock_guard<std::mutex> lock(mutex_);
         auto it = broadcasts_.find(infohash);
         if (it != broadcasts_.end()) {
+            // v09.11.04: Blindaje de concurrencia multi-cliente.
+            // Si todavía existen suscriptores activos o clientes conectados a este broadcast,
+            // NO destruir la sesión ni enviar STOP destructivo a AceStream.
+            if (it->second->get_subscribers() > 0 || it->second->client_count() > 0) {
+                log_line("WARNING", "[" + infohash.substr(0, std::min<std::size_t>(8, infohash.size())) +
+                         "] BroadcastManager::force_stop_broadcast omitido: la sesión todavía tiene " +
+                         std::to_string(it->second->get_subscribers()) + " suscriptores y " +
+                         std::to_string(it->second->client_count()) + " clientes activos.");
+                return;
+            }
             removed = it->second;
             broadcasts_.erase(it);
         }
```

#### B. Protección en `Broadcast::stop(bool force)`
Incluso si se invoca con `force=true`, se bloquea el envío de `STOP` si persisten suscriptores o clientes conectados:

```diff
--- a/httpaceproxycpp/src/broadcast.cpp
+++ b/httpaceproxycpp/src/broadcast.cpp
@@ -328,6 +328,14 @@ void Broadcast::stop(bool force) {
             return;
         }
     } else {
+        // v09.11.04: Si aún quedan suscriptores o clientes legítimos, bloquear STOP destructivo
+        if (subscribers_.load(std::memory_order_relaxed) > 0 || client_count() > 0) {
+            log_line("WARNING", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
+                     "] Broadcast::stop forzado BLOQUEADO: sesión con " + std::to_string(subscribers_.load()) +
+                     " suscriptores y " + std::to_string(client_count()) +
+                     " clientes activos. PROHIBIDO enviar STOP destructivo.");
+            return;
+        }
         log_line("INFO", "[" + infohash_.substr(0, std::min<std::size_t>(8, infohash_.size())) +
                  "] Broadcast::stop FORZADO: enviando STOP inmediato a AceStream y liberando sesión.");
         subscribers_.store(0, std::memory_order_relaxed);
```

#### C. Preservación de Sesión en `Proxy::handle_core_stream` (`FAILOVER-AUTO` y `FAILOVER-MIDSTREAM`)
Se verifica si el canal tiene otros clientes antes de intentar cualquier detención del broadcast:

```diff
--- a/httpaceproxycpp/src/proxy.cpp
+++ b/httpaceproxycpp/src/proxy.cpp
@@ -2456,7 +2456,14 @@ void Proxy::handle_core_stream(RequestContext& ctx) {
                              " en '" + ctx.auto_slug + "'. Conmutando al siguiente candidato...");
                     broadcast->remove_client(client);
-                    broadcasts_.force_stop_broadcast(infohash);
+                    // v09.11.04: Solo detener el broadcast si no quedan otros suscriptores o clientes legítimos
+                    if (broadcast->get_subscribers() <= 0 && broadcast->client_count() == 0) {
+                        broadcasts_.force_stop_broadcast(infohash);
+                    } else {
+                        log_line("INFO", "[FAILOVER-AUTO] Broadcast " + infohash + " preservado para otros clientes (" +
+                                 std::to_string(broadcast->get_subscribers()) + " suscriptores / " +
+                                 std::to_string(broadcast->client_count()) + " clientes activos)");
+                    }
```

---

### 2.4. Sincronización Canónica de Versión a `09.11.04`
* `httpaceproxycpp/include/httpaceproxycpp/version.hpp`: `kAppVersion = "09.11.04"`.
* `httpaceproxycpp/CMakeLists.txt`: `VERSION 9.11.4`, `HTTPACEPROXYCPP_VERSION "09.11.04"`.
* `httpaceproxycpp/http/js/navbar.js`: `(v09.11.04)`.
* `httpaceproxycpp/http/js/footer.js`: `canonicalVersion = '09.11.04'`.
* `httpaceproxycpp/tests/test_core.cpp`: Pruebas de escalado de ThreadPool a 64 workers y aserción estricta de versión `09.11.04`.

---

## 3. Verificación de Compilación y Pruebas Unitarias

La imagen Docker ha sido construida y probada mediante:
```bash
docker compose build httpaceproxy
```
* **Compilación:** Limpia con GCC 13/14 y C++20 con `-DCMAKE_BUILD_TYPE=Release`.
* **Pruebas automáticas (`ctest`):** 100% superadas exitosamente durante la fase de empaquetado del contenedor.
* **Arranque:** Verificado con el nuevo binario emitiendo en log inicial:
  ```text
  [INFO] HTTPAceProxyCPP v09.11.04 starting
  [INFO] HTTPAceProxyCPP v09.11.04 started at 0.0.0.0:8888
  ```
