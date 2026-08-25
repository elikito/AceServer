# Registro de Cambios (Changelog) - HTTPAceProxy

Todos los cambios notables en este proyecto se documentan en este archivo.

El formato sigue las directrices de [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

## [08.25.08] - 2026-08-25

### 🚀 Favoritos Avanzados, Tokenizador Estricto y Curación de Candidatos

#### Tokenizador Canónico y Eliminación de Falsos Positivos (`stream_scorer.cpp`)
- **Matching Estricto de Límites de Palabra**:
  - Eliminado el emparejamiento por subcadena parcial difusa: `dazn-1` ahora solo empareja exactamente con `dazn-1` o `dazn 1`, evitando falsos positivos como `eleven-dazn-1`, `dazn-1-2`, o `dazn-f1`.
  - El token de disciplina `f1` o `formula1` se distingue estrictamente de números simples.
- **Filtro y Penalización de Idiomas / Países Extranjeros**:
  - Detección automática mediante `detect_is_foreign()` de sufijos y etiquetas de país (`(PT)`, `(DE)`, `(RU)`, `(PL)`, `(UK)`, `(IT)`, `(FR)`, `(TR)`, etc.), penalizando streams extranjeros con -50 pts en el cálculo de scoring frente a feeds locales.
- **Perfiles de Calidad Automáticos (Mirrors FHD/HD)**:
  - Generación de enlaces diferenciados por resolución: `/auto/<slug>-fhd` (1080p/FHD) y `/auto/<slug>-hd` (720p/HD) además del mejor global `/auto/<slug>`.

#### Panel EPG (`/epg/index.html`): Curation Drawer & Acciones Directas
- **Acciones Rápidas por Canal**:
  - Botones integrados en cada tarjeta de canal: **📋 Copiar URL Virtual** (`/auto/<slug>/stream.ts`), **▶ Ver en Web Player** y **⭐ Favorito**.
- **Inspector y Curación de Candidatos (Curation Drawer)**:
  - Nuevo botón interactivo `⚙ Fuentes` que despliega un panel en tiempo real con todos los Content IDs asignados al canal, mostrando calidad (`1080p`, `720p`, `4K`), peers, puntuación calculada, lista origen y estado de salud (`ONLINE`, `LOW_PEERS`, `OFFLINE`).
  - Interruptores/toggles individuales para **habilitar o excluir manualmente** streams rotos o no deseados.
  - Persistencia de fuentes deshabilitadas (`disabled_cids`) en `/http/listas/epg_favorites.json` vía `POST /epg?action=toggle_candidate`.

#### Reproductores Web (`/player/index.html` y `/player/legacy.html`)
- **Pestaña ⭐ Favoritos**:
  - Añadida la pestaña / selector de canales `⭐ Favoritos` como primera opción en ambos reproductores web, cargando directamente `/channels/favoritos.m3u`.

#### Metadatos en Dashboard (`/stat`)
- **Nombre Real del Canal en Conexiones Activas**:
  - Resolución y asignación automática del nombre real del canal (ej. `Teledeporte 1080p * (Auto)` o `Teledeporte (Auto)`) en la tabla de conexiones activas en lugar de `stream / stream`.

---

## [08.25.07] - 2026-08-25

### ⭐️ Persistencia de Favoritos EPG & Redirección Absoluta con Pre-Verificación

#### Redirección Virtual `/auto/<slug>` (`proxy.cpp`)
- **Cabecera `Location` Absoluta**:
  - Garantizado que las respuestas `HTTP 307 Temporary Redirect` en `/auto/<slug>` devuelven la URL absoluta construida con `raw_host` (`http://<host>:<puerto>/content_id/<cid>/stream.ts`), maximizando la compatibilidad con reproductores IPTV externos (VLC, Kodi, TiviMate, OTT Navigator, Smart TVs).
- **Verificación Ligera Pre-Redirección**:
  - Si todos los candidatos a un canal se encuentran en estado `UNKNOWN`, se dispara una verificación síncrona ligera (2.5s) sobre el candidato prioritario antes de redirigir, evitando enviar al cliente a un enjambre inactivo con 0 peers / 0 KB/s.

#### Persistencia y Sincronización de Favoritos EPG (`plugins.cpp` / `epg/index.html`)
- **Persistencia en Servidor (`epg_favorites.json`)**:
  - Implementados los endpoints `GET /epg?action=get_favorites` y `POST /epg?action=set_favorites`.
  - Los canales favoritos se almacenan de forma persistente en `/http/listas/epg_favorites.json`.
- **Sincronización Bidireccional Frontend**:
  - La interfaz EPG (`/epg/index.html`) sincroniza automáticamente los favoritos con el backend al cargar la página y al alternar la estrella ⭐ en cualquier canal.
- **Filtro Estricto en `/channels/favoritos.m3u`**:
  - El endpoint `/channels/favoritos.m3u` exporta **únicamente** los canales marcados como favoritos por el usuario.
  - La lista completa de canales automáticos se mantiene accesible en `/channels/auto.m3u` y `/auto/playlist.m3u`.

---

## [08.25.06] - 2026-08-25

### 🎯 Motor de Selección Automática y Fallback Inteligente (Fase 1: Teledeporte)

#### Algoritmo de Normalización y Agrupación Canónica (`stream_scorer.cpp`)
- **Normalización Multicadena y Slugs Universales**:
  - Implementada la función `canonical_name()` y `canonical_slug()` para agrupar streams redundantes del mismo canal:
    - Eliminación de sufijos y etiquetas de calidad: `1080p`, `720p`, `FHD`, `HD`, `4K`, `UHD`, `SD`, `HEVC`, `H.265`.
    - Eliminación de modificadores y caracteres especiales: `*`, `**`, `(back)`, `backup`, `[fhd]`, etc.
    - Normalización de acentos UTF-8 (`á`, `é`, `í`, `ó`, `ú`, `ñ`, `ü`) y caracteres en minúsculas.
  - **Ejemplo**: `Teledeporte 720p **`, `Teledeporte 1080p *`, `Teledeporte 1080p **` y `TELEDEPORTE FHD` se agrupan unívocamente bajo el slug canónico `teledeporte`.

#### Evaluador y Clasificador de Enjambres (`StreamScorer`)
- **Fórmula de Puntuación Inteligente**:
  - Puntuación ponderada basada en:
    $$\text{Score} = (\text{Peers} \times 10) + (\text{SpeedDown KB/s} \times 0.5) + \text{BonusCalidad} + \text{BonusActivo}$$
  - Bonificaciones por resolución: `1080p/FHD` (+30 pts), `720p/HD` (+15 pts), `4K/UHD` (+40 pts).
  - Prioridad de sesión activa (+100 pts) si el stream ya está siendo transmitido por `BroadcastManager` para reusar en caliente.
  - Penalización a streams marcados como `OFFLINE` o `BLOCKED`.

#### Endpoints de Despacho y Playlists Dinámicas (`proxy.cpp`)
- **Ruta Virtual Persistente (`/auto/<slug>`)**:
  - Permite reproducir mediante `/auto/teledeporte` o `/auto?channel=Teledeporte`.
  - Busca todos los candidatos coincidentes en las listas cargadas, evalúa su estado y redirige de forma transparente al mejor Content ID en tiempo real (`HTTP 307 Temporary Redirect` a `/content_id/<CID>/stream.ts`).
  - Endpoint de introspección JSON: `/auto/teledeporte?action=resolve` para consultar el ranking detallado de candidatos y puntuaciones.
- **Lista Dinámica de Favoritos (`/channels/favoritos.m3u` & `/auto/playlist.m3u`)**:
  - Exporta una lista M3U estructurada con los canales consolidados apuntando a sus URLs automáticas `/auto/<slug>/stream.ts`.

---

## [08.25.05] - 2026-08-25

### 🚀 Bypass de Canales Activos & Desbloqueo del Worker Pool de Verificación

#### Verificación de Canales & Rendimiento (`channel_verifier.cpp` / `proxy.cpp`)
- **Bypass Inmediato para Canales en Reproducción Activa**:
  - Incorporado mecanismo de bypass de latencia cero para cualquier Content ID que ya posea una sesión o broadcast activo con clientes (`speed_down > 0` o `peers > 0`).
  - Devuelve instantáneamente el estado `ONLINE` / `LOW_PEERS` con los peers y bitrate en vivo observados por el proxy, evitando crear sesiones duplicadas e innecesarias en el motor AceStream.
- **Resolución de Bloqueos en el Worker Pool**:
  - Corregido el defecto crítico que provocaba errores de *"timeout esperando verificación en curso"* o *"timeout de verificación síncrona"* al consultar CIDs pendientes.
  - Implementada purga automática de verificaciones zombis o colgadas que superen los 12 segundos.
  - Implementado timeout estricto de seguridad (`try_acquire_for(8s)`) en la adquisición de semáforos del worker pool y ajustados los timeouts por fase (handshake a 4s, poll a 2s).
  - Garantizada la notificación de variables de condición (`sync_cv_`) y liberación de mutexes mediante punteros compartidos (`std::shared_ptr`) ante cualquier excepción o salida temprana.

---

## [08.25.04] - 2026-08-25

### 🛡️ Modo No Interactivo (--accept-tos) y Persistencia de Registro WARP

#### Docker & Montajes de Persistencia
- **Montaje de Datos y Registro de Cloudflare WARP**:
  - Añadido el montaje del directorio `/var/lib/cloudflare-warp:/var/lib/cloudflare-warp:ro` en `docker-compose.yml` y `docker-compose-httpaceproxycpp.yml`.
  - Permite que el cliente `warp-cli` dentro del contenedor acceda a las credenciales, registro (`reg.json`, `conf.json`) y base de datos de estado del host sin requerir re-registro manual.

#### Backend C++ (`plugins.cpp`)
- **Flag No Interactivo `--accept-tos`**:
  - Incorporado el parámetro `--accept-tos` en todas las llamadas de sistema a `warp-cli` (`warp-cli --accept-tos connect`, `warp-cli --accept-tos disconnect`, etc.), previniendo bloqueos por prompts interactivos de confirmación de términos de servicio dentro del contenedor.

---

## [08.25.03] - 2026-08-25

### ⚡ Integración de Cloudflare WARP en Docker & Ajustes de Negociación de Red

#### Docker & Comunicación con el Host
- **Montaje de Socket IPC y Binario de Cloudflare WARP**:
  - Configurados los volúmenes en `docker-compose.yml` (y `docker-compose-httpaceproxycpp.yml`) para comunicar de forma transparente el backend C++ con el daemon `warp-svc` del host:
    - `/run/cloudflare-warp:/run/cloudflare-warp` (Socket IPC).
    - `/usr/bin/warp-cli:/usr/bin/warp-cli:ro` (Binario de control en modo lectura).
  - Diseñado con tolerancia a fallos para nodos que no tengan WARP instalado, garantizando que el arranque del contenedor nunca se interrumpa.

#### Backend C++ (`plugins.cpp`)
- **Validación de Disponibilidad de `warp-cli`**:
  - Añadida comprobación previa de existencia y ejecutabilidad de `warp-cli` antes de invocar órdenes del sistema. Si el binario o socket no está accesible, se devuelve una respuesta JSON estructurada con `status: "error"` y el diagnóstico de red actual en lugar de fallar silenciosamente.
- **Incremento del Tiempo de Negociación (2000 ms)**:
  - Ampliado el retardo tras la ejecución de `warp_connect`, `warp_disconnect` y `warp_toggle` de 500 ms a **2000 ms** (`std::chrono::milliseconds(2000)`), permitiendo a Cloudflare WARP completar el *handshake* de la interfaz virtual y el túnel seguro antes de consultar el diagnóstico actualizado.

---

## [08.25.01] - 2026-08-25

### 🚀 Unificación, Modularización, Cabeceras y Mejoras EPG/UI

#### UI, Navegación y Arquitectura Frontend
- **Extracción de Componentes de Navegación (`navbar.css` y `navbar.js`)**:
  - Creado `/stat/css/navbar.css` con diseño moderno en *glassmorphism* (`backdrop-filter: blur(14px)`), tokens armonizados, micro-animaciones en *hover*, estado `.active` con halo luminoso e icono hamburguesa responsivo animado a 'X'.
  - Creado `/stat/js/navbar.js` para detección dinámica del enlace activo según `window.location.pathname`, control del cajón desplegable móvil y eventos de autocierre por clic externo y tecla `Escape`.
  - Eliminado el código duplicado de navegación en todas las páginas (`/`, `/statplugin/`, `/fuentes/`, `/epg/`, `/player/index.html`, `/listas/`).
- **Estructura y Orden Unificado del Menú Superior**:
  - **HTTPAceProxy** (Brand / Enlace raíz a `/stat`).
  - **Dashboard** (`/stat`)
  - **Canales** (`/statplugin/`)
  - **Fuentes** (`/fuentes/`)
  - **EPG** (`/epg/`)
  - **Reproductor** (`/player/index.html`)
  - **Reproductor Legacy** (`/player/legacy.html`)
- **Unificación Tipográfica Global**:
  - Incorporada la fuente Google Fonts **Inter** (`wght@400;500;600;700`) tanto en `navbar.css` como en las cabeceras HTML de todas las vistas, garantizando idéntico ancho, grosor y kerning del logotipo y enlaces.
- **Menú Legacy Exclusivo para iOS 12 (`/player/legacy.html`)**:
  - Menú ultraligero y plano, 100% libre de animaciones o filtros gráficos pesados, totalmente optimizado para Safari WebKit / iOS 12.5.8 con flexbox seguro.

#### Estandarización de Cabeceras (`header-section`)
- **Homogeneización Visual y de Altura**:
  - Centralizadas las clases `.container` (`max-width: 1400px; padding: 24px 20px;`) y `.header-section` (`min-height: 48px; margin-bottom: 24px; padding-bottom: 16px; border-bottom: 1px solid rgba(255, 255, 255, 0.08);`) en `navbar.css`.
  - Migrado `/statplugin/` de `.page-header` a `.header-section` con jerarquía uniforme (`h2` + `p`).
  - Corregidas alturas desiguales entre páginas unificando los contenedores de títulos y acciones.

#### Funcionalidades EPG y Reloj en Tiempo Real
- **Reloj en Vivo del Sistema (`🕒 HH:MM:SS`)**:
  - Añadido indicador de hora actual del cliente actualizado cada segundo en:
    - **EPG** (`/epg/`): En la barra de estadísticas de la cabecera.
    - **Reproductor Bento** (`/player/index.html`): En el subtítulo de la cabecera.
    - **Reproductor Legacy** (`/player/legacy.html`): En la cabecera con compatibilidad ES5.
- **Mejoras en el Parser XMLTV (`parseXMLTVDate`)**:
  - Reescrita la expresión regular y cálculo de timestamp para soportar offsets de huso horario (`+0200`, `+02:00`, `+0000`, UTC y local), garantizando que las pestañas "● En Emisión" y "⏭ Próximos" sincronicen a la perfección con la hora real.
- **Corrección de Error en EPG**:
  - Añadido control de null-safety en `document.getElementById('epg-source-url')` y estadísticas, resolviendo la excepción `Cannot set properties of null (setting 'textContent')` que impedía la descarga de la guía.

#### Correcciones en Portapapeles (`footer.js`)
- **Eliminación del Bug de Copiado de Versión / IP**:
  - Resuelto el problema por el cual el texto *"✓ Copiado!"* se copiaba en el portapapeles en pulsaciones sucesivas al leer el DOM mutado.
  - Implementado almacenamiento en memoria de valores canónicos inmutables (`canonicalVersion`, `canonicalIp`, `canonicalHostname`).
  - Control de temporizadores con `clearTimeout` para evitar que el estado visual quede bloqueado tras múltiples clics rápidos.
  - Eliminado el manejador local duplicado `bindFooter()` en `statplugin/index.html`.

---

## [08.24.06] - 2026-08-24

### 🛠️ Estandarización de Navegación, Control WARP Web y Corrección de Verificador

#### UI & Navegación Global
- **Barra de Navegación Unificada**:
  - Estandarizado el marcado HTML y estilos CSS de `.navbar` en todas las páginas web (`/`, `/statplugin/`, `/fuentes/`, `/epg/`, `/player/`, `/listas/`) manteniendo estilo uniforme con marcado de enlace activo en cápsula oscura y tipografía coherente.
  - Excluida explícitamente la vista `/player/legacy.html` para preservar su diseño minimalista original de WebKit ES5 / iPad.
- **Corrección de Enlaces en Reproductor Legacy (`/player/legacy.html`)**:
  - Restaurada la generación limpia de URLs TS (`/content_id/<hash>/<name>.ts`) en `parseM3ULegacy()`, garantizando compatibilidad nativa con VLC Media Player y clientes IPTV externos.

#### Backend C++ & Funcionalidades de Red
- **Control Web Interactivo de Cloudflare WARP**:
  - Creados los endpoints `/statplugin?action=warp_connect`, `warp_disconnect`, `warp_toggle` para ejecutar de forma segura comandos `warp-cli` y retornar el estado actualizado en tiempo real.
  - Añadido el botón de acción interactivo `[⚡ Conectar WARP]` / `[🔌 Desconectar WARP]` en la tarjeta de Protección de Red de `/statplugin`.
- **Eliminación de Falsos Negativos en `ChannelVerifier`**:
  - Ampliado el timeout de handshake (Fase 1) de 3s a **6s** y la ventana de observación DHT de 2.75s a **4.5s** (`channel_verifier.cpp`).
  - Refactorizada la clasificación en `classify()` para reconocer estados activos de prebuffering (`dl`, `prebuf`, `buf`), previniendo falsas marcas de "Error / Bloqueado" o "Caídos" en enlaces reproducibles.

---

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
