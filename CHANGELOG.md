# Registro de Cambios (Changelog) - HTTPAceProxy

Todos los cambios notables en este proyecto se documentan en este archivo.

El formato sigue las directrices de [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

## [08.24.05] - 2026-08-24

### 🎯 Unificación Total de Fuentes y Extracción Robusta de Content IDs

#### Backend C++ — Exportación de Plugins e Integración de Fuentes
- **Refactorización Completa de `Proxy::plugins_json()`**:
  - Sustituida la iteración sobre `plugin->channels()` por `plugin->playlist_items()`, garantizando que `/statplugin` muestre exactamente las mismas listas y cantidades de canales que `/player` y `/fuentes`.
  - **Normalizador Inteligente de Content IDs (`extract_acestream_content_id()`)**: Extrae Content IDs válidos soportando esquemas `acestream://`, rutas HTTP `/content_id/HASH/stream.ts` y hashes hex puros de 40 caracteres.
  - **Sustitución de Canales de `Af1c1onados`**: Resuelto el problema de descarte de canales en `Af1c1onados`, permitiendo la lectura de los 32 canales reales de la fuente.
  - **Corrección de Conteo en `Elcano`**: Preservados los 44 canales reales exportados por la fuente sin pérdidas causadas por sobrescrituras en mapas estáticos de C++.
  - **Soporte de Fuentes Locales Personalizadas (`CustomListPlugin`)**: Integración directa de listas subidas o añadidas manualmente, respetando la propiedad `enabled: true`.

---

## [08.24.04] - 2026-08-24

### 🔄 Sincronización de Fuentes, Estabilidad MPEG-TS de Audio y Unificación de Navegación

#### Backend C++ — Sincronización de Fuentes y Estabilidad de Streaming TS
- **Sincronización Total de Fuentes (`Proxy::plugins_json()`)**:
  - Filtrado de fuentes desactivadas (`!is_plugin_enabled()`). Al apagar o deshabilitar una lista/fuente en `/fuentes` o `plugins_state.json`, esta se elimina de inmediato de `/statplugin` sin dejar elementos o fuentes huérfanas.
- **Alineación Estricta de Paquetes MPEG-TS a 188 Bytes (`Broadcast::broadcast_chunk()`)**:
  - Implementado búfer residual en `Broadcast` para garantizar que todo bloque transmitido a clientes HTTP/VLC esté exactamente alineado a múltiplos de 188 bytes (tamaño estandarizado de paquete MPEG-TS).
  - Previene desincronizaciones de audio y pérdida de paquetes en VLC Media Player, FFmpeg y reproductores legacy.
- **Detección Local de WARP y Advertencia de ISP Directo (`Proxy::get_network_diagnostics()`)**:
  - Verificación de puertos SOCKS5/HTTP locales de WARP (`127.0.0.1:40001` / `host.docker.internal:40001`).
  - Reporte claro y advertencia de "Ruta Directa (Sin Protección)" cuando el tráfico sale directamente por el ISP local.

#### UI & Navegación Global
- **Homogeneización del Menú de Navegación**:
  - Unificada la barra de navegación `.nav-links` en todas las plantillas HTML (`/`, `/statplugin/`, `/fuentes/`, `/epg/`, `/player/index.html`, `/player/legacy.html`).
  - Incluidos de forma coherente los accesos: `[Dashboard] [Canales] [Fuentes] [EPG] [Reproductor] [Reproductor Legacy]`.
- **Reproductor IPTV Legacy (`/player/legacy.html`)**:
  - Mantenida la compatibilidad ultra ligera para navegadores antiguos y WebKit ES5 (iOS 12).
  - Añadido banner informativo de compatibilidad de audio para transmisiones en códecs AC3/E-AC3 con recomendación de enlace directo TS / VLC.
- **Respuesta Interactiva en Diagnóstico de Red**:
  - Manejo de excepciones en botón "Diagnosticar Red" con feedback in-page mediante notificaciones Toast.

---

## [08.24.03] - 2026-08-24

### 🛠️ Corrección Crítica de Enrutamiento en `ChannelVerifier` y Mejoras de Diagnóstico

#### Backend C++ — Corrección del Host y Fallback de Red
- **Resolución dinámica de Host y Puerto en `ChannelVerifier` (`channel_verifier.cpp`)**:
  - Corrección del fallo de conexión en Fase 1 que generaba falsos negativos ("Error / Bloqueado").
  - Soporte de variables de entorno `ACE_HOST` / `ACESTREAM_HOST` / `ACE_ENGINE_HTTP_PORT` para adaptar el Worker Pool al entorno del contenedor.
  - Fallback inteligente en Fase 1: si la conexión inicial a `127.0.0.1` o `aceserve-modern` en puerto 6878 falla, se reintenta automáticamente contra el host alternativo de la red Docker y se actualiza el motor activo en runtime.
  - Registro de errores explícito en logs (`[verifier] Error de conexión al motor AceStream en Fase 1`) para depuración inmediata.
  - Sincronización automática de `channel_verifier_.set_ace_engine()` al solicitar cambios de motor vía `set_engine()`.

#### Panel de Nivel de Protección de Red (`/statplugin`)
- **Nuevo Endpoint Backend `/statplugin?action=network_diag`**:
  - Detección de **Cloudflare WARP** (`Activo` / `Modo Proxy` / `Desconectado`) mediante traza `/cdn-cgi/trace`.
  - Detección de **Tailscale Mesh Network** (`Conectado` / `Inactivo`) inspeccionando la presencia de interfaces de red.
  - Detección de IP pública de salida y proveedor/ISP.
  - Generación del indicador de **"Ruta Segura"** (🟢 Verde si transita por túnel con evasión de bloqueos de ISP activa / 🟡 Amarillo/Gris si la ruta es directa por ISP local).
- **Banner UI en `/statplugin`**:
  - Incorporada tarjeta interactiva de Estado de Protección de Red con badges de estado y botón de actualización manual.

#### Reproductor IPTV Bento (`/player`)
- **Visualización de Content ID en Barra de Estado**:
  - Añadida insignia interactiva `#player-content-id-box` en la cabecera mostrando el hash en reproducción actual y botón de copia rápida con notificación Toast (`✓ Content ID copiado`).
- **Resaltado Activo de Tarjeta de Canal en Cuadrícula (Bento Grid)**:
  - Destacado visual en tiempo real de la tarjeta en reproducción (`.bento-card.is-playing`) con borde brillante verde (`#4CAF50`), resplandor dinámico y distintivo `🔴 REPRODUCIENDO EN VIVO`.

---

## [08.24.02] - 2026-08-24

### ⚡ Refactorización Profunda del Módulo de Verificación de Canales (`/statplugin`)

#### Backend C++ — Nuevo `ChannelVerifier` con Worker Pool Asíncrono

- **`channel_verifier.hpp` / `channel_verifier.cpp`** — Nuevo módulo independiente:
  - **Worker Pool** con `std::counting_semaphore<2>` (C++20): máximo 2 verificaciones concurrentes accediendo al motor AceStream simultáneamente, garantizando que el motor no se sobrecargue.
  - **Cola de tareas FIFO** protegida por `std::mutex` + `std::condition_variable` para despacho ordenado de verificaciones pendientes.
  - **Estado persistente en memoria** por Content ID: `std::unordered_map<cid, VerifyResult>` con `std::shared_mutex` (readers-writer lock) para acceso concurrente eficiente sin bloqueos entre lecturas.

- **Pipeline de Verificación en 4 Fases** (sobre API HTTP `http://<ace_host>:<ace_http_port>/ace/getstream`):
  - **Fase 1 (Handshake):** `GET /ace/getstream?id={cid}&format=json` con timeout de 3s.
  - **Fase 2 (Resolución Torrent):** Valida `stat_url` + `command_url`; detecta error `"Cannot retrieve torrent"`.
  - **Fase 3 (DHT & Swarm):** Espera activa de 2.75s total (polls cada 400ms) consultando `stat_url` para obtener `peers` y `status`.
  - **Fase 4 (Bitrate Real):** Lee `speed_down` (bytes/s). Si `status == "dl"` y `speed_down > 0`, clasifica como reproducible.
  - **CIERRE OBLIGATORIO:** `GET {command_url}?method=stop` con timeout de 2s. **Siempre ejecutado**, incluso en caso de error, para liberar sockets inmediatamente y evitar sesiones huérfanas.

- **Clasificación de Estados (`ChannelHealth` enum):**
  - `ONLINE` 🟢: `status=="dl"` && `peers>=2` && `speed_down > 102400` bytes/s (≈ 100 KB/s / 800 Kbps)
  - `LOW_PEERS` 🟡: `(dl|prebuf)` && `peers>=1` && `speed_down <= threshold`
  - `OFFLINE` 🔴: `peers==0` && `speed_down==0` (no elimina el ID, sólo lo marca inactivo)
  - `BLOCKED` ⛔: Timeout de handshake o error `"Cannot retrieve torrent"`
  - `ERROR` ❌: Excepción inesperada / respuesta no parseable
  - `PENDING`: En cola, verificación en curso
  - `UNKNOWN`: Nunca verificado

- **Configuración flexible:**
  - Host/puerto del motor: `config_.ace_host` / `config_.ace_http_port` con fallback `127.0.0.1:6878`.
  - Soporte para variable de entorno `ACE_ENGINE_HTTP_PORT` para sobreescribir el puerto en runtime.
  - Umbral de bitrate configurable en runtime (`ChannelVerifier::set_speed_threshold()`), constante `kDefaultSpeedThreshold = 102400` bytes/s.

#### Nuevos Endpoints HTTP (`/statplugin`)

| Endpoint | Descripción |
|---|---|
| `?action=verify&content_id=<hash>[&timeout_ms=<ms>]` | Verificación síncrona completa de un CID (respuesta con `health`, `peers`, `speed_down`, `phase_reached`) |
| `?action=verify_batch&ids=<h1>,<h2>,...` | Encola múltiples CIDs para verificación asíncrona; retorna estado actual inmediato |
| `?action=get_health` | Mapa completo de estados en memoria (preparado para selección de mejor stream por canal EPG) |
| `?action=get_health_one&content_id=<hash>` | Estado de un CID sin lanzar nueva verificación |

#### Compatibilidad y Otros Cambios

- Los endpoints legacy `check_channel` y `check_peers` (vía `AceClient` TCP) se mantienen **sin modificaciones** para compatibilidad total con el frontend existente.
- Integración en `Proxy`: `ChannelVerifier` inicializado en el constructor con los parámetros del motor.
- Añadido `src/channel_verifier.cpp` al target `httpaceproxycpp_core` en `CMakeLists.txt`.
- Versión bumpeada a `08.24.02` en `config.hpp`.

---

## [08.24.01] - 2026-08-24


### 📦 Exportación e Importación de Fuentes M3U y JSON (`/fuentes`)
- **Sistema de Exportación Estandarizado M3U y JSON:**
  - Exportación de listas en formato M3U estándar (`fuentes_httpaceproxy.m3u`) con directivas `#EXTM3U` y etiquetas `#EXTINF` con metadatos (`tvg-id`, `tvg-name`, `group-title`, `status`), plenamente compatible tanto con HTTPAceProxy como con cualquier reproductor IPTV genérico (VLC, Tivimate, Kodi, IPTV Smarters).
  - Exportación de copia de seguridad completa en formato JSON (`fuentes_httpaceproxy.json`) con las fuentes predefinidas y personalizadas.
- **Sistema de Importación Inteligente de Fuentes:**
  - Panel interactivo en `/fuentes` para cargar archivos `.m3u`, `.m3u8`, `.json` o `.txt`, o pegar código fuente directamente.
  - Soporte de modos de importación: **Fusionar (Merge)** (añadir y actualizar fuentes manteniendo las existentes) y **Reemplazar (Replace)** (sustituir listas personalizadas).
  - Backend C++ en `/config?action=export_sources` y `/config?action=import_sources` para procesamiento asíncrono y refresco en caliente.

### 🎨 Notificaciones In-Page y Modales de Confirmación Unificados
- **Componente Global de Modal de Confirmación (`showConfirmModal`):**
  - Incorporado en `footer.js` y disponible en todas las vistas de la app (`/fuentes`, `/listas`, `/statplugin`, `/`, `/player`, `/epg`).
  - Overlay de diseño moderno con desenfoque (`backdrop-filter`), animaciones suaves, adaptación a tema Claro / Oscuro e iconos contextuales (`🗑️` / `❓`).
- **Eliminación Total de Popups Nativos (`confirm` y `alert`):**
  - Reemplazadas todas las llamadas emergentes del navegador (`192.168.1.54 dice`) por notificaciones Toast in-page y modales contextuales.

### 📺 Reproducción Directa por Content ID en Reproductor IPTV (`/player`)
- **Entrada Manual de Content ID en el Reproductor:**
  - Añadido un campo de texto interactivo con botón **▶ Reproducir ID** en la cabecera del reproductor IPTV (`/player/index.html`).
  - Extracción automática de hash de 40 caracteres desde texto o enlaces `acestream://` / `content_id/...`.
  - Inicio de reproducción instantánea en directo en el reproductor Bento HTML5 (`mpegts.js`) con desplazamiento suave.

---

## [08.22.02] - 2026-08-22

### 🎨 Homogeneización y Barra Inferior (Footer Unificado)
- **Componente de Footer estandarizado en todas las vistas (`/`, `/player/`, `/fuentes/`, `/epg/`, `/statplugin/`, `/listas/`):**
  - **Sección Izquierda:** IP del servidor y nombre de host (`IP: <ip> (<hostname>)`). Al hacer clic, copia automáticamente la IP al portapapeles con feedback visual (`✓ Copiado!`).
  - **Sección Central:** Botón centrado para alternar entre tema Claro y Oscuro con icono dinámico (Sol/Luna) y persistencia en `localStorage`.
  - **Sección Derecha:** Versión activa del sistema (`VERSIÓN: 08.22.02`). Al hacer clic, copia el identificador de versión al portapapeles con feedback visual (`✓ Copiado!`).
- **Módulo JS Compartido ([footer.js](file:///opt/HTTPAceProxy/httpaceproxycpp/http/js/footer.js)):**
  - Gestión centralizada de eventos de copiado seguro al portapapeles con fallback.
  - Sincronización automática con las APIs `/stat/?action=get_status` y `/config?action=get_config`.
  - Eliminación de discrepancias visuales de altura, etiquetas y alineación entre paneles.

---

## [08.22.01] - 2026-08-22

### 🚀 Nuevas Funcionalidades y Fuentes
- **Integración nativa de la fuente `Elcano.top by @Lucas_m_o_o_m ... Vacaciones en el Mar`:**
  - Conexión dinámica con la pasarela IPNS `https://k51qzi5uqu5dh5qej4b9wlcr5i6vhc7rcfkekhrxqek5c9lk6gdaiik820fecs.ipns.inbrowser.link/hashes.json`.
  - Configurable y editable en caliente desde la interfaz web de Fuentes (`/fuentes`).
  - Carga optimizada de canales deportivos, TDT y eventos categorizados por grupos (`DAZN`, `LA LIGA`, `LIGA DE CAMPEONES`, `MOVISTAR`, `DEPORTES`, `TDT`, etc.).
- **Deduplicación inteligente en el Core C++:**
  - Sistema de control de duplicados (`std::set<std::string> seen_hashes`) en `ElcanoPlugin` y listas compuestas para evitar colisiones de hashes entre múltiples fuentes.
- **Soporte extendido de formatos de lista remotos y locales:**
  - Parser JSON universal en C++ y JavaScript para esquemas basados en `hashes: [...]` (TokyoGhoulles, Elcano), `stations: [...]` (Af1c1onados) y arrays directos.
  - Parser para listas en texto plano estructurado (`hashes.txt`) con delimitadores de grupo `=== GRUPO ===` y hashes `acestream://`.

### 🌐 Mejoras en Red e IPFS
- **Resolución Multi-Pasarela IPFS/IPNS de alta velocidad (`HttpClient`):**
  - Soporte directo para subdominios `*.ipns.dweb.link` y `*.ipfs.dweb.link`.
  - Conexión concurrente/fallback a través de `dweb.link`, `ipfs.io`, `cloudflare-ipfs.com` y `pinata.cloud`.
  - Ajuste de timeout de conexión dinámico (`CURLOPT_CONNECTTIMEOUT`) y seguimiento de redirecciones HTTP 301/302 automáticas.

### 📺 Interfaz del Reproductor Web (`/player`)
- **Visualización y Copiado interactivo de ID de AceStream:**
  - Inclusión de badge con el hash de AceStream en todas las tarjetas de canal.
  - Copiado automático al portapapeles con un solo clic y feedback visual (`✓ Copiado!`, efecto resplandor verde y notificación toast).
- **Homogeneización del mapeo de canales:**
  - Sistema `globalChannelAceIdMap` para retener y sincronizar los identificadores de canales entre todas las pestañas y vistas (AIO, Elcano, TokyoGhoulles, etc.).
- **Identificación de origen en resultados de búsqueda:**
  - Las cabeceras de categorías y tarjetas indican la lista de procedencia (`GRUPO • Fuente`, ej: `FORMULA 1 • Elcano`).

### 📊 Monitorización y Diagnóstico (`/statplugin`)
- **Corrección de colapso y renderizado de canales:**
  - Sustitución del cálculo de altura en línea (`maxHeight = scrollHeight`) por control de visualización CSS puro (`.plugin.collapsed .channels { display: none; }`), garantizando que todas las listas aparezcan abiertas y visibles por defecto.
- **Exposición de versión del sistema:**
  - Inclusión de la clave `version` en las respuestas de `/statplugin?action=get_plugins` y enlace con `/config?action=get_config`.
