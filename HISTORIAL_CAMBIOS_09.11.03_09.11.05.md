# Historial Técnico de Cambios: v09.11.03 → v09.11.04 → v09.11.05
**HTTPAceProxy — Evolución de Estabilidad, Concurrencia y Streaming de Alto Rendimiento**

**Fecha de Consolidación:** 11 de Septiembre de 2026  
**Plataforma de Despliegue:** Debian 12 / Docker (Intel N100 / N150)  
**Repositorio:** `elikito/AceServer` (`/opt/HTTPAceProxy`)  

---

## 📑 Tabla Comparativa Resumen de Versiones

| Versión | Enfoque Principal | Componentes Modificados | Impacto Clave |
|---|---|---|---|
| **`v09.11.03`** | **Continuidad MPEG-TS y Failover Activo** | `proxy.cpp`, `broadcast.cpp`, `channel_verifier.cpp` | Inyección de `discontinuity_indicator`, TS Null Packets con CC rotativo, parada forzada inmediata de transmisiones obsoletas y pausa del verificador en stream activo. |
| **`v09.11.04`** | **Escalabilidad y Blindaje de Concurrencia** | `http_server.hpp`, `http_server.cpp`, `proxy.cpp`, `docker-compose.yml` | 64 workers HTTP, sockets con `TCP_NODELAY` y `SO_SNDBUF=256KB`, blindaje anti-colisión multi-cliente (nadie tumba un stream compartido) y buffer AceStream en 35s. |
| **`v09.11.05`** | **Zero-Copy Fan-Out y Optimización de RAM** | `broadcast.hpp`, `broadcast.cpp`, `proxy.cpp`, `test_core.cpp` | Punteros inmutables `ChunkPtr`, eliminación de clonaciones de memoria para múltiples clientes en un mismo canal/CID, y transmisión directa socket-level. |

---

## 1. Versión `v09.11.03`: Continuidad MPEG-TS y Failover Limpio

### 1.1. Inyección de Banderas de Discontinuidad MPEG-TS (`discontinuity_indicator`)
- **Problema Detectado:** Al ocurrir una conmutación en caliente de CID (*midstream failover*) o tras una sequía transitoria de búfer (>250 ms), los reproductores IPTV estrictos (VLC, mpv, TiviMate, ExoPlayer) se congelaban o arrojaban pantalla negra debido al salto abrupto en los sellos de tiempo PTS/DTS del flujo MPEG-TS.
- **Solución Implementada (`Proxy::apply_ts_discontinuity_to_packet`):**
  - Se implementó la inspección del primer paquete TS de vídeo (PID de vídeo detectado dinámicamente) tras un evento de reconexión.
  - Se inyecta el flag `discontinuity_indicator` (bit 7 del campo de adaptación según norma ISO/IEC 13818-1). Si el paquete carecía de campo de adaptación, se transforma de forma no destructiva preservando la carga útil.
  - Esto indica al decodificador del reproductor que reinicie su reloj interno de reloj PCR/PTS sin interrumpir la reproducción.

### 1.2. Keep-Alive con TS Null Packets de Continuidad Rotativa
- Se generan dinámicamente paquetes TS Null (188 bytes con PID `0x1FFF`) cada 250 ms durante sequías de búfer.
- Se incorporó un contador de continuidad (`null_packet_cc`) de 4 bits rotativo (`0x10 | (cc & 0x0F)`), evitando que los clientes de red consideren muerto el socket y cierren la conexión TCP.

### 1.3. Cancelación Activa de Broadcasts Obsoletos
- **Problema Detectado:** Al cambiar de canal o realizar un failover a un nuevo CID, el motor AceStream continuaba descargando el flujo antiguo en segundo plano, consumiendo ancho de banda P2P y conexiones innecesarias.
- **Solución Implementada:** Se ordenó la ejecución de `force_stop_broadcast` inmediato sobre el CID previo en:
  - Failovers automáticos (`FAILOVER-MIDSTREAM`).
  - Fijación manual de canal (`pin_candidate`).
  - Migración forzada de suscriptores (`migrate_subscribers`).

### 1.4. Pausa del Verificador de Canales en Streaming Activo
- Se modificaron `ChannelVerifier` y `FavoritesWorker` para pausar sus pruebas en segundo plano mientras existan transmisiones de clientes activas, evitando saturar la API del motor (puerto 62062) ni competir por ancho de banda.

---

## 2. Versión `v09.11.04`: Escalabilidad a 64 Workers y Blindaje Anti-Colisión

### 2.1. Ampliación del ThreadPool HTTP (64 Workers)
- **Problema Detectado:** El ThreadPool HTTP calculaba los workers como $\min(16, \text{cores} \times 2)$. En el procesador Intel N100 (4 cores / 4 threads), esto limitaba el servidor a únicamente **8 hilos**. Al ser el streaming HTTP síncrono por socket, 5 clientes consumían 5 hilos permanentes, dejando el servidor sin hilos libres para la API (`/stat`), la EPG o el panel web, devolviendo errores HTTP 503.
- **Solución Implementada (`http_server.hpp` / `http_server.cpp`):**
  - Se elevó `MAX_WORKERS` a **64** hilos y `DEFAULT_QUEUE_DEPTH` a **512** peticiones.
  - Se recalculó el escalado dinámico para cargas I/O-bound: `min(64, max(32, cores * 16))`, garantizando que en un N100 el pool arranque con 32-64 hilos listos para streaming concurrente masivo.

### 2.2. Optimización de Sockets de Vídeo (`TCP_NODELAY` + 256 KB Buffer)
- En `http_server.cpp`, se configuró explícitamente cada socket de cliente aceptado con:
  - **`TCP_NODELAY` = 1**: Desactiva el algoritmo de Nagle, enviando los paquetes MPEG-TS de forma instantánea sin acumular retardos artificiales de 40 ms.
  - **`SO_SNDBUF` = 262144 bytes (256 KB)**: Buffer de transmisión ampliado a nivel de kernel para absorber ráfagas de vídeo y prevenir saturaciones ante oscilaciones de red de los clientes.

### 2.3. Blindaje de Failover Anti-Colisión Multi-Cliente
- **Problema Detectado:** Si dos clientes veían el mismo canal/CID y uno de ellos sufría un timeout de red o cerraba su app bruscamente, el failover ejecutaba `force_stop_broadcast(infohash, force=true)`, enviando una orden fulminante `STOP` al motor AceStream y tumbando el vídeo del otro cliente que estaba viendo la transmisión perfectamente.
- **Solución Implementada (`broadcast.cpp` y `proxy.cpp`):**
  - En `BroadcastManager::force_stop_broadcast` y `Broadcast::stop(force=true)`, se verifica obligatoriamente si quedan suscriptores (`subscribers > 0`) o clientes activos (`client_count() > 0`).
  - Si aún quedan clientes conectados al canal, la orden de detención forzada **se rechaza**, permitiendo que el stream continúe ininterrumpido para los demás usuarios.

### 2.4. Sintonización del Motor AceStream en Docker
- En `docker-compose.yml`, se ajustaron los parámetros del motor `aceserve-modern`:
  - `--live-buffer 35` (reducido desde 60s a 35s, recortando 25 segundos de latencia de visualización frente al directo sin comprometer la salud del enjambre P2P).
  - `--max-connections 500`.

---

## 3. Versión `v09.11.05`: Zero-Copy Fan-Out y Optimización Extrema de Memoria

### 3.1. Arquitectura Fan-Out Zero-Copy (`ChunkPtr`)
- **Problema Detectado:** Cuando múltiples clientes (ej. salón, cocina, dormitorio, tablets) sintonizan el mismo evento deportivo o canal principal con el mismo CID, el método anterior `Broadcast::broadcast_chunk` clonaba el bloque entero de bytes (128 KB – 256 KB) en el heap para la cola de cada cliente. Con 5 clientes y un flujo a 15 Mbps, esto generaba cientos de miles de allocaciones de memoria por minuto, saturando el asignador de C++ y el bus de memoria del procesador N100.
- **Solución Implementada (`broadcast.hpp` / `broadcast.cpp`):**
  - Se introdujo el puntero compartido inmutable:
    ```cpp
    using ChunkPtr = std::shared_ptr<const std::vector<char>>;
    ```
  - `ChunkQueue` almacena punteros inteligentes en un `std::deque<ChunkPtr>`.
  - Al recibir un bloque desde el motor AceStream, `broadcast_chunk` realiza una única asignación:
    ```cpp
    auto shared_chunk = std::make_shared<const std::vector<char>>(std::move(chunk_to_push));
    for (auto& client : clients()) {
        client->queue->push(shared_chunk, wait);
    }
    ```
  - Repartir el vídeo a $N$ clientes cuesta únicamente copiar **8 bytes de puntero** con incremento atómico de referencias, eliminando al 100% las copias de memoria en el heap.

### 3.2. Transmisión Directa y Aislamiento de Discontinuidad TS
- En `Proxy::handle_core_stream` (`src/proxy.cpp`), el envío hacia el socket se realiza directamente leyendo `chunk->data()` sobre la memoria inmutable compartida.
- Si un cliente individual experimenta una sequía de buffer y requiere inyección de bandera de discontinuidad, se genera una copia local de trabajo **únicamente para ese cliente específico**, garantizando que el bloque compartido inmutable permanezca intacto para todos los demás clientes concurrentes.

### 3.3. Verificación Matemática de Cero Copias en Tests C++
- Se añadió la prueba unitaria `test_v09_11_05_zero_copy_fanout_and_version()` en `test_core.cpp`:
  - Valida que al encolar y desencolar en diferentes colas de clientes independientes, `out1.get() == out2.get()` y `out1->data() == out2->data()`, certificando que todos los clientes apuntan a la misma dirección física de memoria.
  - Valida la retrocompatibilidad completa de las sobrecargas legacy de `ChunkQueue`.

---

## 4. Estado de Validación y Despliegue en Producción

- **Compilación:** 100% aprobada en imagen Docker multi-stage Ubuntu 24.04 (C++20).
- **Suite de Tests Unitarios (`ctest`):** Todos los módulos aprobados (`test_core`).
- **Contenedores Activos:**
  - `httpaceproxy`: **Up (healthy)** — Reportando versión canónica `09.11.05`.
  - `aceserve-modern`: **Up** — Puerto 62062 (API) y 6878 (HTTP stream).
  - `ipfs-node`: **Up (healthy)** — Puerto 4010 / 5010 / 8180.
- **Sincronización Git:** Confirmada en rama `main` y sincronizada con el remoto `origin/main` (`31ee24c`).
