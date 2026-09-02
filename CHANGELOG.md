# Registro de Cambios (Changelog) - HTTPAceProxy

Todos los cambios notables en este proyecto se documentan en este archivo.

El formato sigue las directrices de [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

## [09.02.02] - 2026-09-02

### 🚀 Resolución de fuentes locales Docker, Sincronización de Tema y Mejoras en Web Player / Statplugin

#### 1. Resolución de Fuentes Locales Docker (`127.0.0.1` / IPFS / ZeroNet)
- **Fallback Automático en Contenedor Docker**: En `HttpClient::get` (`http_client.cpp`), si se solicita una URL dirigida a `127.0.0.1` o `localhost` (puertos 8080, 8180, 43110, etc.), el proxy prueba automáticamente resolución alternativa hacia:
  - `http://ipfs-node:8080/` para peticiones de IPFS.
  - `http://host.docker.internal:<PORT>/` conectando con el host gateway.
  - `http://172.17.0.1:<PORT>/` hacia el bridge de red por defecto de Docker.
- **Docker Compose Networking**: Añadida directiva `extra_hosts: ["host.docker.internal:host-gateway"]` en el servicio `httpaceproxy` dentro de `docker-compose.yml`.

#### 2. Sincronización y Toggle Inmediato de Modo Claro / Oscuro
- **Centralización en `navbar.js` y `footer.js`**: Eliminada duplicidad de escuchadores de eventos click que generaban condiciones de carrera entre `<html>` y `<body>`.
- **Conmutación Instantánea sin Recarga**: `toggleTheme()` y `applyTheme()` actualizan simultáneamente `document.documentElement` y `document.body` (`data-theme` y clase `light-theme`), sincronizan `localStorage` (`theme` y `aceproxy-theme`) y despachan el evento global `themeChanged`.

#### 3. Normalización y Lógica del Buscador (`/player` y `/statplugin`)
- **Búsqueda Avanzada Multicriterio por Tokens**: Normalización profunda (`normalizeSearchText`) eliminando tildes, signos diacríticos, puntuación y aliases (`m+`, `movistar+`, `m.`, `\bm\b` -> `movistar`).
- **Matching Desordenado**: Búsqueda insensible al orden de las palabras (`matchSearchTokens`), permitiendo que términos como "m+ deportes" coincidan con "14. M Deportes FHDa" y "Movistar Deportes 1".
- **Espera a Resolución de Catálogo**: Tanto el reproductor como statplugin aseguran la carga y resolución completa de fuentes antes de ejecutar el filtro, evitando listas vacías.

#### 4. Mejoras en el Reproductor Web (`/player`)
- **Visualización y Copia de Content ID Completo**: Los 40 caracteres hexadecimales se muestran en tipografía monoespaciada (`.id-mono-text` / `.player-cid-badge`). Clic directo copia el hash al portapapeles con toast flotante `✓ ID copiado` y feedback inline temporal `✓ Copiado!`. Eliminado el icono redundante de portapapeles.
- **Indicador de Salud de Enlace**: Badge unificado en la cabecera del reproductor (`#player-health-badge`) que consulta el estado real (`ONLINE`, `LOW_PEERS`, `OFFLINE`) con recuento de peers vía `/statplugin?action=get_health_one`.
- **Manejo de Fallos de Reproducción y Watchdog**: Temporizador de arranque (10-12s) y captura de errores MSE (`MEDIA_MSE_ERROR` por HEVC o E-AC3). Si el canal no arranca, cancela el spinner de reconexión y muestra overlay con motivo explícito, botón directo «Abrir en VLC» y botón «Copiar enlace stream».

#### 5. Consistencia de Diseño entre `/statplugin` y `/player`
- Homogeneización de tarjetas bento, badges de estado (`badge-online`, `badge-low`, `badge-offline`), paleta de colores CSS compartida y tipografía monoespaciada para hashes AceStream.

---

## [09.02.01] - 2026-09-02

### ⚙️ Sistema de Filtros Regex Personalizados por Canal y Reparación del Modo Claro/Oscuro

#### 1. Sistema de Filtros Regex Personalizados por Canal (`proxy.cpp` / `config/channel_filters.json`)
- **Persistencia en backend**: Almacenamiento JSON persistente en `config/channel_filters.json` (con tolerancia a `.bak` y sincronización) mapeando `slug` -> patrones regex / palabras clave.
- **Endpoints REST**:
  - `GET /api/channel_filters`: Devuelve el mapa JSON de filtros activos (o el filtro de un canal si se especifica `?slug=...`).
  - `POST /api/channel_filters`: Guarda o actualiza las reglas para un canal `slug`.
- **Motor de Ingesta y Matching**:
  - Evaluación contra expresiones regulares para cualquier fuente M3U/JSON (incluyendo IPFS/IPNS). Si coincide, se añade automáticamente como candidato válido al slug correspondiente.
  - Compatibilidad de sintaxis: soporte transparente para flags inline `(?i)` en `std::regex` convirtiéndolo a case-insensitive y soporte para lookaheads negativos `(?!...)`.
- **Reglas predeterminadas de fábrica para la familia Movistar Deportes**:
  - `m-deportes`: `(?i)(m\+|m\.|movistar)[\s_]*deportes(?![\s_]*[2-8])`
  - `m-deportes-2` a `m-deportes-8`: `(?i)(m\+|m\.|movistar)[\s_]*deportes[\s_]*[2-8]`

#### 2. UI EPG: Botón «Personalizar» y Modal de Filtros (`epg/index.html` / `epg.js`)
- **Nuevo Botón en la Tarjeta de Canal**: Añadido el botón `⚙ Personalizar` en la botonera superior derecha de cada tarjeta de canal junto a `Copiar URL`, `Ver` y `Fuentes`.
- **Modal Interactivo de Configuración**:
  - Campo de texto editable para la expresión regular o palabras clave del canal.
  - Botón **«Copiar Regla»**: copia al portapapeles con toast flotante de confirmación.
  - Botón **«Pegar y Aplicar»**: pega directamente desde el portapapeles en el campo.
  - Botón **«Guardar y Reindexar»**: persiste la regla vía API REST y refresca/reindexa inmediatamente los candidatos del canal.
  - Botón para restablecer la regla a los valores de fábrica o eliminarla.

#### 3. Reparación Integral del Modo Claro / Oscuro (`http/css/navbar.css` y `http/js/navbar.js`)
- **Normalización de Variables CSS Semánticas**: Definidos tokens CSS estándar (`--bg-primary`, `--bg-card`, `--text-primary`, `--text-secondary`, `--border-color`) aplicados uniformemente bajo `[data-theme="dark"]` y `[data-theme="light"]`.
- **Contraste y Legibilidad**: Corregido contraste de textos, títulos, badges, cids y tarjetas para evitar texto ilegible en modo claro.
- **Eliminación de Estilos Hardcodeados**: Reemplazados colores fijos (como `#ffffff` en pills sobre fondo claro) en `/player/index.html` y `/epg/index.html`.
- **Persistencia**: Sincronización asegurada en `localStorage.getItem('theme')` y `localStorage.getItem('aceproxy-theme')`.

---

## [08.30.08] - 2026-08-30

### ⚡ Indexación de Fuentes Dinámicas IPFS en EPG, Caché de Registros y Test Global de Salud

#### 1. Fusión e Indexación de Fuentes Dinámicas IPFS (`proxy.cpp` / `plugins.cpp`)
- Mapeo inteligente y compacto de slugs de canales (`dazn-laliga`, `dazn-la-liga`, `la-1`, etc.) garantizando que las listas IPNS/IPFS dinámicas (como `elcano_ipns_backuo` con sus 86 canales) se vinculen correctamente a sus canales EPG correspondientes.
- Etiquetado explícito del origen de la fuente dinámica en la columna "Lista / Plugin" (`elcano_ipns_backuo`, `interna`, etc.).
- Soporte en `CustomListPlugin` para URLs dinámicas IPNS con auto-resolución de `hashes.json` y fallback a `hashes_acestream.m3u`.

#### 2. Optimización de Resolución DHT y Caché en Memoria (`http_client.cpp`)
- Ampliado timeout de resolución DHT en el nodo Kubo local a **10 segundos** para permitir búsquedas en frío sin fallos.
- Implementada caché en memoria de respuestas IPFS/IPNS con TTL diferenciado (60s para IPNS, 300s para IPFS inmutable) para respuestas inmediatas (< 1ms) en consultas recurrentes.

#### 3. Auto-Verificación y Botón Global de Salud en EPG (`epg/index.html`)
- **Auto-check inteligente**: Al abrir la pestaña/drawer de "Fuentes" de cualquier canal, si los candidatos figuran sin verificar, se dispara automáticamente la comprobación en segundo plano.
- **Botón `⚡ Testear Salud de Favoritos`**: Añadido en la barra superior de `/epg/` para comprobar en paralelo (lotes concurrentes) el estado y peers de todos los canales favoritos con notificación de progreso y actualización en vivo.

---

## [08.30.07] - 2026-08-30

### 🌐 Integración de Nodo IPFS/IPNS Local Seguro (Kubo) y Reescritura Inteligente

#### 1. Servicio `ipfs-node` en `docker-compose.yml`
- Integrado contenedor `ipfs/kubo:latest` conectado a la red interna `aceproxy-net`.
- Configurado perfil de bajo consumo `IPFS_PROFILE=server,lowpower` para evitar saturación de memoria y conexiones.
- Mapeo de puertos seguro:
  - Gateway HTTP: `"127.0.0.1:8180:8080"` (puerto interno `http://ipfs-node:8080` accesible por el proxy).
  - API RPC: `"127.0.0.1:5010:5001"`.
  - Swarm P2P: `"4010:4010/tcp"` (Solo TCP para preservar las tablas NAT del router).
- Volúmenes persistentes en `./config/ipfs_data` y `./config/ipfs_staging`.

#### 2. Resolución y Reescritura Automática IPFS (`http_client.cpp` / `util.cpp`)
- Reescritura automática de URLs IPFS / IPNS al gateway local (`http://ipfs-node:8080/` o `http://127.0.0.1:8180/`) con timeout ágil de 4 segundos.
- Fallback automático a múltiples gateways públicos (`https://ipfs.io/`, `https://dweb.link/`, `https://cloudflare-ipfs.com/`, `https://gateway.pinata.cloud/`).
- Validación permisiva de esquemas `ipfs://`, `ipns://`, `http://127.0.0.1:8080/`, `http://127.0.0.1:8180/` y `http://ipfs-node:8080/` en el gestor de fuentes (`fuentes/index.html`).

---

## [08.30.06] - 2026-08-30

### 🔧 Corrección de Compilación C++ en Re-comprobación de Fuentes

#### 1. Serialización Explícita de `ChannelCandidate` (`proxy.cpp`)
- Corregido error de compilación en `recheck_sources`: serialización directa de los campos de `ChannelCandidate` a `Json::object` evitando invocación inexistente de método miembro `to_json()`.

#### 2. Extracción Cualificada de Content ID (`proxy.cpp`)
- Sustituida la llamada no declarada `extract_content_id` por `extract_acestream_content_url` con sanitización de prefijos (`acestream://`, `infohash://`) y parámetros de query para el lote de verificación global de fuentes.

---

## [08.30.05] - 2026-08-30

### 📺 Optimización de Exportación M3U de Favoritos con Cabecera EPG DobleM y Logos Absolutos

#### 1. Inyección de Cabecera EPG Global Obligatoria (`proxy.cpp`)
- Inyección garantizada en la primera línea de `/channels/favoritos.m3u`:
  `#EXTM3U url-tvg="https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv.xml" tvg-shift="0"`
  proporcionando compatibilidad inmediata con clientes IPTV externos (TiviMate, OTT Navigator, VLC, Kodi, etc.).

#### 2. Metadatos M3U Plus, Mapeo `tvg-id` y URLs Absolutas de Logos (`proxy.cpp`)
- Formato estricto para cada canal de favoritos con numeración secuencial de dial:
  `#EXTINF:-1 tvg-id="{canonical_tvg_id}" tvg-name="{canonical_name}" tvg-logo="{logo_url}" group-title="Favoritos",{dial}. {canonical_name} FHDa`
- Prioridad jerárquica de `tvg-logo` (logo personalizado en `custom_logos.json` > logo XMLTV / fuente original).
- Conversión automática de rutas relativas (`/logos/...`) a URLs absolutas (`http://{host}:8888/logos/...`) para descarga remota en reproductores externos.
- Mapeo canónico normalizado de `tvg-id` para canales españoles compatibles con la guía de dobleM (`La1.es`, `DAZN1.es`, `DAZNLaLiga.es`, `MVamos.es`, `MLaLigaTV.es`, etc.).

#### 3. Generación Selectiva de Variantes (`proxy.cpp`)
- Si un canal dispone de fuente HD real (720p), se generan ambas variantes: `{dial}. {canonical_name} FHDa` (`/auto/{slug}-fhda`) y `{dial}. {canonical_name} 720a` (`/auto/{slug}-720a`).
- Si solo dispone de stream SD/FHD sin 720p real, se genera únicamente la entrada `{dial}. {canonical_name} FHDa` apuntando a `/auto/{slug}`.

---

## [08.30.03] - 2026-08-30

### 🎨 Mejoras de Interfaz, Conmutador de Tema Global, Drag & Drop y Traducción

#### 1. Conmutador de Tema Claro / Oscuro Unificado (`navbar.js` / `navbar.css`)
- Conmutador interactivo con icono Sol/Luna restaurado en toda la navegación superior.
- Alternancia reactiva de `data-theme="light"` / `data-theme="dark"` en `document.documentElement` y `body.light-theme`.
- Persistencia sincronizada en `localStorage.getItem('aceproxy-theme')`.
- Variables CSS de tema claro unificadas (`#f8fafc` fondo, `#ffffff` tarjetas, `#e2e8f0` bordes, `#0f172a` texto).

#### 2. Delimitación de Drag & Drop y Selección Libre de Texto (`epg/index.html`)
- Eliminado `draggable="true"` global de las tarjetas de canal EPG para permitir selección y copia libre de texto (títulos, descripciones, horarios).
- Arrastre restringido exclusivamente al icono con puntitos (`.drag-handle` / `⠿`).

#### 3. Visualización Completa de IDs y Traducción de Estados (`epg/index.html` / `player/index.html` / `statplugin/index.html`)
- Visualización del Content ID completo de 40 caracteres en formato monoespaciado con botón interactivo de copiado.
- Notificación toast visual al copiar cualquier ID: `📋 ID copiado al portapapeles: <hash>`.
- Traducción integral de estados al español:
  - `ONLINE` ➔ **`ACTIVO`**
  - `OFFLINE` ➔ **`CAÍDO`**
  - `BLOCKED` ➔ **`BLOQUEADO`**
  - `LOW_PEERS` ➔ **`ENJAMBRE LENTO`**
  - `UNKNOWN` ➔ **`SIN VERIFICAR`**
  - `CHECKING` / `PENDING` ➔ **`VERIFICANDO`**

---

## [08.30.02] - 2026-08-30

### ⚙️ Correcciones en EPG, Re-comprobación de Fuentes y Gestor de Motores

#### 1. Corrección del Botón "Copiar URL" en EPG (`epg/index.html` / `epg.js`)
- En cada fila/tarjeta de canal en `/epg/`, el botón **📋 Copiar URL** ahora copia al portapapeles de forma directa y garantizada la URL virtual del canal: `http://<host>:8888/auto/<channel-slug>` (con fallback por portapapeles restringido).

#### 2. Re-comprobación Forzada de Fuentes y Candidatos (`proxy.cpp` / `plugins.cpp` / `epg/index.html`)
- Añadido botón destacado **⟳ Re-comprobar Fuentes** en el cajón de fuentes y candidatos.
- Endpoint `POST /statplugin?action=recheck_sources` que limpia la caché de salud del canal, sondea en paralelo el enjambre de peers en tiempo real y recalcula las puntuaciones al instante sin reiniciar el proxy.

#### 3. Panel Monitor y Gestor de Motores AceStream (`statplugin/index.html` / `proxy.cpp` / `plugins.cpp`)
- Nuevo panel de control para los motores `aceserve-modern`, `aceserve-compat-light` y `aceserve-compat-stable`.
- Indicadores visuales de estado (Activo/Verde, En reposo/Amarillo, Desconectado/Rojo), puertos API (62062) y HTTP (6878).
- Acciones en caliente: **★ Activar como Principal**, **⟳ Reiniciar Motor** y **Detener / Arrancar**.

---

## [08.30.01] - 2026-08-30

### 🚀 Optimización Integral de Variantes, EPG, Reproductor y Logos

#### 1. Simplificación de Variantes (`proxy.cpp` / `proxy.hpp` / `/channels/favoritos.m3u`)
- **Consolidación a 2 Variantes Máximas por Canal**:
  - `[Nombre Canal] FHDa`: Mejor stream `>= 1080p` (o mejor stream global si no hay 1080p).
  - `[Nombre Canal] 720a`: Mejor stream `720p` (se emite únicamente si existe una fuente real HD 720p).
- **Generación en Playlist M3U (`/channels/favoritos.m3u`)**:
  - Exportación con las dos variantes ordenadas según el dial establecido en la EPG, con inyección de logos personalizados.

#### 2. Guía EPG y Exportación (`epg/index.html` / `epg.js`)
- **Buscador Universal en Tiempo Real**:
  - Filtra instantáneamente por nombre de canal, títulos de programas, descripciones y categorías en toda la guía.
- **Botón Copiar URL M3U de Favoritos**:
  - Incorporado botón `📋 Copiar URL M3U` en la barra de acciones de favoritos junto a *Guardar Orden* para copiar `http://<host>:8888/channels/favoritos.m3u`.
- **Loader Descriptivo y Renderizado Optimizado**:
  - Estado de carga informativo (*"Cargando favoritos y eventos de la guía..."*) con sincronización de estado.

#### 3. Editor y Selector de Logos de Canales (`custom_logos.json` / `plugins.cpp` / `proxy.cpp`)
- **Modal Interactivo de Personalización de Logos**:
  - Clic en el logo del canal en la EPG abre el modal para elegir entre logos encontrados en listas M3U activas y XMLTV, introducir URL externa o subir un archivo local.
- **Persistencia y Aplicación Inmediata**:
  - Guardado en `/app/config/custom_logos.json` con backup `.bak` y propagación inmediata a `/epg/`, `/player/` y `/channels/favoritos.m3u`.

#### 4. Reproductor Bento y Alternador de Vistas (`player/index.html`)
- **Selector de Vista Cuadrícula / Filas**:
  - Botones en cabecera para alternar entre *Vista Bento (Cuadrícula)* y *Vista Filas (Compacta estilo Legacy)* con persistencia en `localStorage`.
- **Homogeneización y Logos Dinámicos**:
  - Integración de logos personalizados y coherencia de favicon e iconos entre todas las secciones.

---

## [08.27.07] - 2026-08-27

### 📱 Refactorización Integral Mobile-First y Tablet 4:3 (iPad Air)

#### 1. Eliminación de Emojis en Código Fuente Frontend
- Sustituidos todos los emojis hardcodeados por iconos SVG vectoriales inline de alta definición y texto semántico accesible.

#### 2. EPG y Organizador de Canales Favoritos (`epg/index.html`)
- **Pestañas y Acciones Responsive (`<= 640px`)**:
  - Pestañas con `flex-wrap: wrap` y sub-barra `.fav-actions-bar` adaptada al ancho completo en móvil con botones simétricos.
- **Touch Targets Ergonómicos para Diales**:
  - Diales y botones de posición adaptados a touch area de 44px con `touch-action: manipulation` para evitar zooms accidentales por doble tap.

#### 3. Smart Footer para Pantallas Bajas (`navbar.css` / `footer.js`)
- Añadida regla `@media (max-height: 520px)` que convierte el footer en posición relativa en vista horizontal (Samsung S24 Ultra landscape) para eliminar solapamientos.

#### 4. Reproductor Bento y Reproductor Legacy (`player/index.html` / `player/legacy.html`)
- **Formulario Directo CID en Móvil (`<= 480px`)**:
  - Adaptado a distribución vertical al 100% de ancho sin truncamiento de texto.
- **Grid Adaptativo iPad Air 9.7" (Aspect Ratio 4:3)**:
  - Configurado `grid-template-columns: repeat(auto-fill, minmax(220px, 1fr))` para 3 columnas en portrait y 4 columnas en landscape sin huecos muertos laterales.
  - Limitación de altura del reproductor a `max-height: 48vh` en tablets.

#### 5. Gestor de Fuentes y Diagnóstico (`fuentes/index.html` / `statplugin/index.html`)
- Tablas con scroll horizontal táctil suave (`overflow-x: auto; -webkit-overflow-scrolling: touch`).
- Cabecera de acciones en Fuentes adaptada a grid de 2 columnas en móviles.

---

## [08.27.06] - 2026-08-27

### 🎛️ Rediseño Reactivo del Organizador de Canales Favoritos

#### 1. Barra de Acciones y Ordenación Alfabética Instantánea (`epg/index.html`)
- **Barra de Acciones en Favoritos**:
  - Incorporados dos botones de acción rápida en la cabecera de la pestaña ⭐ Favoritos:
    - **🔤 Ordenar (A-Z)**: Reordena automáticamente todas las tarjetas de favoritos en el DOM de 1 a N por nombre de canal en orden alfabético.
    - **💾 Guardar Orden**: Botón destacado con estilo esmeralda que envía el array ordenado actual al endpoint `POST /epg?action=set_favorites_order` y notifica al usuario con feedback visual.

#### 2. Reordenación Reactiva e Intercambio de Posiciones (1-based index)
- **Drag & Drop en Tiempo Real**:
  - Al arrastrar y soltar una tarjeta se reubica el nodo en el DOM en tiempo real y se recalculan automáticamente todos los números de dial (1, 2, 3... N) secuencialmente.
- **Flechas de Desplazamiento ▲ / ▼**:
  - Movimiento intuitivo hacia arriba o abajo intercambiando posiciones y recalculando diales en caliente.
- **Edición Manual de Dial**:
  - Cambio numérico directo que desplaza la tarjeta a la posición indicada e intercala el resto de canales sin duplicidades.
- **Limpieza Visual CSS**:
  - Ocultadas las flechas predeterminadas del navegador (`-webkit-inner-spin-button`, `-moz-appearance: textfield`) para mantener un diseño limpio y moderno con botones personalizados.

---

## [08.27.05] - 2026-08-27

### ⭐ Reordenación Interactiva de Favoritos y Agrupación Limpia por Canal

#### 1. Reordenación Interactiva de Favoritos (`epg.js` / `epg/index.html` / `plugins.cpp` / `proxy.cpp`)
- **Control de Orden en la Pestaña Favoritos de EPG**:
  - Implementada reordenación de canales favoritos mediante **Drag & Drop** nativo y control numérico de dial editable con botones de ajuste rápido `▲` / `▼`.
  - Persistencia instantánea del orden personalizado vía `POST /epg?action=set_favorites_order` hacia `/app/config/epg_favorites.json`.
- **Generación de Lista M3U (`/channels/favoritos.m3u`)**:
  - Respetado de forma estricta el orden de diales definido por el usuario al generar el playlist M3U.

#### 2. Agrupación por Canal en el Reproductor (`player/index.html`)
- **Organización de Secciones por Canal**:
  - Cada canal favorito se renderiza como su propia categoría/grupo visual con su nombre y logo oficial.
  - Opciones de calidad agrupadas limpiamente bajo cada canal:
    - `[Canal] (Mejor Stream Auto)`
    - `[Canal] 1080p (FHD Auto)` (si existen fuentes FHD)
    - `[Canal] 720p (HD Auto)` (si existen fuentes HD)
- **Secuencia y Orden en Reproductor**:
  - El reproductor respeta la secuencia exacta de canales establecida en la EPG sin desordenar las categorías.

---

## [08.27.04] - 2026-08-27

### 🚀 Corrección Crítica de Entrypoints en Motores AceStream y Fallback de Red DNS

#### 1. Corrección Crítica de `docker-compose.yml` (Contenedores AceStream)
- **Eliminación de `command` personalizado**:
  - Retirada la directiva `command: ["--max-upload-rate", "500"]` en los contenedores de motor (`aceserve-modern`, `aceserve-compat-light`, `aceserve-compat-stable`, `aceserve`) para no romper su `ENTRYPOINT` nativo (`/srv/ace/start-engine.sh` / scripts de inicio propios).
- **Garantía de Red Bridge Común (`aceproxy-net`)**:
  - Asegurada la conectividad e interconexión interna de todos los servicios en `aceproxy-net` con resolución DNS fluida.

#### 2. Fallback de Conexión y Resolución DNS en el Proxy (`ace_client.cpp`)
- Implementada lista ordenada de candidatos de fallback en `AceClient::connect_socket()`:
  - Si la resolución DNS o conexión a `config_.ace_host` (ej. `aceserve-modern`) falla o tarda en estabilizarse, se prueba automáticamente contra `127.0.0.1`, `172.17.0.1` (gateway Docker) y los otros motores activos del pool (`aceserve-compat-stable`, `aceserve-compat-light`).

---

## [08.27.03] - 2026-08-27

### 🧹 Reaper Automático de Sesiones Inactivas, Desconexión Limpia y Control de Subida

#### 1. Reaper y Garbage Collector de Streams Fantasma (`broadcast.cpp` / `proxy.cpp`)
- **Detección y Cierre Inmediato en TCP Reset/FIN**:
  - Al desconectarse cualquier cliente o reproductor (VLC, Web Player, etc.), se emite de inmediato el comando `STOP` y `SHUTDOWN` por control socket Telnet y `GET /ace/stop` vía HTTP para detener de raíz el consumo de recursos.
- **Watchdog Periódico de Sesiones Inactivas**:
  - Implementado hilo reaper en segundo plano en `BroadcastManager` con auditoría periódica cada 30 segundos.
  - Cierre forzado y liberación automática de cualquier cliente/stream sin lectura de datos durante más de **20 segundos**.
  - Si el proxy queda sin clientes activos, se envía una señal de `STOP` global a los motores AceStream para silenciar la actividad P2P de fondo.

#### 2. Contención de Tráfico P2P y Procesos Zombies (`docker-compose.yml`)
- Añadida la directiva `init: true` en los servicios de los motores AceStream (`aceserve-modern`, `aceserve-compat-light`, `aceserve-compat-stable`) para gestión adecuada de señales `SIGTERM`/`SIGINT` y recolección de procesos zombi (PID 1 reaper).
- Configurado el argumento `--max-upload-rate 500` en los contenedores de motor para contener la subida parásita y preservar el ancho de banda.

---

## [08.27.02] - 2026-08-27

### 💾 Persistencia Aislada en `/config` y Parser Mejorado de Importación de Fuentes

#### 1. Directorio de Configuración Persistente Fuera de Git (`/config` / `/app/config/`)
- **Migración y Aislamiento de Estado**:
  - Implementada gestión dinámica de directorio de configuración mediante `Config::get_config_dir()` (`/app/config`, `/opt/HTTPAceProxy/config`, o `config/`).
  - Almacenamiento persistente de datos de usuario:
    - `epg_favorites.json` & `epg_favorites.json.bak`
    - `plugins_state.json`
    - `sources.json`
    - `custom_lists.json`
    - Listas locales (`/app/config/listas/locales/`)
  - **Aislamiento en `.gitignore`**:
    - Directorio `config/` y archivos `.json` de datos dinámicos ignorados por Git.
    - Inicialización con plantillas por defecto sin sobreescribir datos existentes de usuario.
  - **Volumen Docker**:
    - Añadido montaje `/opt/HTTPAceProxy/config:/app/config` en `docker-compose.yml` y `httpaceproxycpp/docker-compose-httpaceproxycpp.yml`.

#### 2. Parser Atómico y Robusto de Importación de Fuentes (`/sources?action=import` & `/config?action=import_sources`)
- Soporte para schema JSON estándar exportado:
  - `custom_lists`: lista de fuentes personalizadas con `name`, `title`, `url`, `enabled`.
  - `urls`: mapa clave-valor de URLs de plugins principales (`newera`, `elcano`, `af1c1onados`, `acepl`, `epg`).
  - Formatos JSON alternativos (arrays planos de listas personalizadas y objetos directos).
- Activación e inserción atómica de listas y URLs de plugins.
- Feedback visual estandarizado `{ "status": "success", "ok": true, "imported": N, "imported_count": N, "message": "..." }` evitando bloqueos de interfaz en el frontend.

---

## [08.27.01] - 2026-08-27

### 🌐 Detección/Control de Cloudflare WARP y Compatibilidad de Red Multi-Entorno

#### 1. Detección de Estado Real de Cloudflare WARP vía SOCKS5 (`plugins.cpp` / `proxy.cpp` / `statplugin`)
- **Comprobación Real SOCKS5**:
  - Implementada verificación HTTP directa hacia `https://cloudflare.com/cdn-cgi/trace` a través de los listeners SOCKS5 locales (`127.0.0.1:4001`, `172.17.0.1:4001`, `host.docker.internal:4001`).
  - Detección precisa de respuesta `warp=on` o `warp=plus`:
    - `warp_connected: true`
    - `traffic_route: "Cloudflare WARP (SOCKS5 Blindado)"`
    - `egress_ip`: Extracción directa de la IP pública reportada por Cloudflare.
    - `isp_name`: "Cloudflare WARP Mesh Network".
    - `safe_route: true`
  - Fallback sin túnel o `warp=off` reporta `warp_connected: false` y la IP directa/Tailscale.
- **Acciones y Control WARP (`warp_connect`, `warp_disconnect`, `warp_toggle`)**:
  - Ejecución de comandos `warp-cli` (`warp-cli mode proxy && warp-cli proxy port 4001 && warp-cli connect` y `warp-cli disconnect`).
  - Pausa asíncrona de 1 segundo para permitir la estabilización del socket antes de retornar el diagnóstico actualizado.

#### 2. Compatibilidad de Red Multi-Entorno (Dev vs Prod)
- Cero IPs estáticas o locales hardcodeadas; compatibilidad total entre el entorno de desarrollo (N150 / `192.168.1.54`) y producción (N100 / `192.168.1.198`).
- Uso exclusivo de sockets locales (`127.0.0.1`), gateway Docker (`172.17.0.1`) y resolución dinámica.
- Aislamiento estricto de Servarr manteniéndose dentro del scope de `/opt/HTTPAceProxy`.

#### 3. Confirmación y Validación de Variantes de Resolución en Favoritos (`player/index.html` / `player.js`)
- Renderizado garantizado de Content IDs diferenciados para cada variante de resolución en Favoritos:
  - *Mejor Stream Auto* (`/auto/<slug>`): Mayor puntuación global de salud y bitrate.
  - *1080p FHD Auto* (`/auto/<slug>-fhd`): Candidato prioritario `>= 1080p`.
  - *720p HD Auto* (`/auto/<slug>-hd`): Candidato prioritario `720p`.

---

## [08.26.04] - 2026-08-26

### 🎯 Corrección Estricta de Variantes de Resolución y Asignación de Content IDs en Favoritos

#### 1. Corrección Estricta de Variantes de Resolución en Favoritos (`proxy.cpp` / `player/index.html`)
- **Diferenciación Real por Calidad y Resolución**:
  - **Tarjeta "Mejor Stream Auto" (`/auto/<slug>`)**: Ordena los candidatos por puntuación de salud y selecciona estrictamente el de mayor puntaje global, inyectando su Content ID exacto en `ace-id="..."`.
  - **Tarjeta "1080p (FHD Auto)" (`/auto/<slug>-fhd`)**: Filtra exclusivamente candidatos con `quality >= StreamQuality::FHD_1080` (1080p/4K). Si no existen fuentes 1080p, no se genera la tarjeta ni se hereda ningún hash SD.
  - **Tarjeta "720p (HD Auto)" (`/auto/<slug>-hd`)**: Filtra exclusivamente candidatos con `quality == StreamQuality::HD_720` (720p), asignando su Content ID 720p correspondiente (ej. DAZN LaLiga asigna `eb49757...`/`385abcf...` en lugar de duplicar el hash 1080p).
- **Identificadores en Tarjetas y Reproductor Web**:
  - El badge `ID: xxxxx...` de cada tarjeta y el banner superior del reproductor muestran de forma exacta el Content ID asociado tras aplicar el filtro de calidad específico.

#### 2. Sincronización de Fuentes EPG
- Eliminación de hashes huérfanos o cacheados; las opciones y variantes derivan exclusivamente de las fuentes vivas indexadas para cada slug canónico.

---

## [08.26.03] - 2026-08-26

### 🛡️ Erradicación Total de Canales Predeterminados y Persistencia Reactiva 1:1

#### 1. Erradicación Total de Canales Predeterminados en el Reproductor (`player/index.html` / `player/legacy.html`)
- **Limpieza de Estado Inicial y Cero Inyecciones Forzadas**:
  - Si el backend devuelve un array vacío `[]` para favoritos (`GET /epg?action=get_favorites` o `/channels/favoritos.m3u`), la pestaña **⭐ Favoritos** queda completamente vacía.
  - Se muestra el mensaje explícito: *"No tienes canales en Favoritos. Añádelos desde la Guía EPG"* con enlace directo a `/epg/`.
  - Eliminados todos los canales por defecto residuales (`epg_favorites.json` inicializado en limpio con `{"favorites": [], "disabled_cids": []}`).
  - Prohibida cualquier inyección de relleno (La 1, Teledeporte, Eurosport, Dazn 1, Dazn 2, etc.).

#### 2. Sincronización Reactiva 1:1 entre EPG y Reproductor
- Persistencia estricta al marcar o desmarcar canales en `/epg/index.html`.
- El reproductor lee directamente de la fuente de verdad única sin duplicar tarjetas ni añadir variantes fantasma.

#### 3. Persistencia de Fuentes y Plugins
- Mapeo y almacenamiento garantizado de `plugins_state.json` y `epg_favorites.json` para conservar el estado de configuración y favoritos entre reinicios de contenedor.

---

## [08.26.02] - 2026-08-26

### 🌟 Sincronización Estricta de Favoritos, Variantes de Calidad y Puntuación de Streams

#### 1. Sincronización Estricta de Favoritos (`player.js` / `favorites.cpp` / `proxy.cpp`)
- **Eliminación Total de Canales Fantasma o Residuales**:
  - La pestaña **⭐ Favoritos** en el reproductor web (`/player/index.html` y `/player/legacy.html`) renderiza únicamente los canales marcados como favoritos en el almacenamiento persistente (`epg_favorites.json` / `GET /epg?action=get_favorites`).
  - Si en EPG hay un número exacto de favoritos (ej. 3 canales), el reproductor mostrará exclusivamente esos 3 canales, erradicando cualquier inyección residual o fallback forzado.

#### 2. Corrección de Variantes de Calidad en Favoritos (Auto, 1080p, 720p)
- **Selección Estricta por Resolución**:
  - **Auto**: Selección del Content ID con mayor puntuación global dentro de todos los candidatos del canal.
  - **1080p (FHD Auto)**: Selección estricta y exclusiva del mejor Content ID clasificado como `1080p`/`4K`. Si un canal no tiene fuente 1080p, no genera ni duplica hashes SD y retorna error 404 en la URL dedicada `/auto/<slug>-fhd`.
  - **720p (HD Auto)**: Selección estricta del mejor Content ID clasificado como `720p`. Si no existe fuente 720p, no duplica SD.
  - En la generación de listas M3U (`generate_favorites_playlist` y `generate_auto_playlist`), las entradas `1080p (FHD Auto)` y `720p (HD Auto)` solo se emiten si la resolución correspondiente está presente en el pool de candidatos.

#### 3. Ajuste del Algoritmo de Puntuación (`stream_scorer.cpp`)
- **Ponderación de Calidad vs Peers**:
  - **Calidad 1080p / 4K**: Base mínima de +100 puntos (+120 para 4K) si tiene al menos 3 peers activos (+30 si tiene < 3 peers).
  - **Calidad 720p**: Base de +60 puntos si tiene al menos 3 peers activos (+15 si tiene < 3 peers).
  - **Calidad SD**: Aportación máxima de peers acotada a +30 puntos para prevenir que streams SD con muchos peers superen a emisiones 1080p/720p saludables y estables.

---

## [08.26.01] - 2026-08-26

### ⚽ Normalización Avanzada de Slugs con Hashes y Reparación de Control WARP

#### 1. Normalización de Nombres y Regex de Canales (`stream_scorer.cpp` / `proxy.cpp`)
- **Soporte para Sufijos Hash y Flechas de Origen**:
  - Implementada la función de limpieza y descarte de sufijos de procedencia / origen con flechas unicode y ascii (`→`, `➔`, `➜`, `➡`, `⇒`, `-->`, `->`, `==>`, `=>`).
  - Reconocimiento y filtrado automático de identificadores hash alfanuméricos/hexadecimales de 4 caracteres (`[a-f0-9]{4}`) como `936c`, `2929`, `9f1a`, `9e38`, `ad6d`.
  - Normalización unificada para patrones como:
    - `M+ LALIGA 936c → ELCANO` ➔ `m-laliga`
    - `M+ LALIGA FHD 2929 → NEW ERA VI` ➔ `m-laliga`
    - `M+ LALIGA 9f1a → ELCANO` ➔ `m-laliga`
    - `M+ LALIGA 9e38 → SPORT TV` ➔ `m-laliga`
    - `M+ LALIGA 2 936c → ELCANO` ➔ `m-laliga-2`
    - `M+ LALIGA 3 FHD 2929 → NEW ERA VI` ➔ `m-laliga-3`
  - Inclusión garantizada de todas estas fuentes en el pool de candidatos de **M+ LaLiga** en `/epg/` y `/auto/m-laliga`.
  - Prevención de falsos positivos en detección de canales extranjeros (`detect_is_foreign`) causados por sufijos de origen.

#### 2. Reparación del Control de Cloudflare WARP (`plugins.cpp` / `proxy.cpp`)
- **Adaptación a la Nueva Sintaxis de `warp-cli`**:
  - En la acción `warp_connect`: ejecución de la secuencia `warp-cli mode proxy && warp-cli proxy port 4001 && warp-cli connect`.
  - En la acción `warp_disconnect`: ejecución de `warp-cli disconnect`.
  - En la acción `warp_toggle`: conmutación automática entre modo proxy y desconexión según el estado actual.
  - Pausa de 1 segundo tras la invocación del comando de sistema antes de retornar el diagnóstico de red actualizado con el estado reportado por `warp-cli status` y la comprobación de puertos proxy (4001 / 40001).

---

## [08.25.13] - 2026-08-25

### 🎯 Sincronización Estricta de Favoritos en Reproductor Web y Backend

#### Sincronización Estricta en el Reproductor Web (`/player/`)
- **Fuente de Verdad Única**:
  - La pestaña **⭐ Favoritos** en el reproductor moderno (`/player/index.html`) y clásico (`/player/legacy.html`) carga dinámica y exclusivamente desde `/channels/favoritos.m3u` con el parámetro de refresco reactivo `?t=<timestamp>`, evitando cualquier caché HTTP o desincronización local.
  - Si en el backend solo está marcado un conjunto específico de canales (ej. `DAZN LaLiga`), el reproductor reflejará exactamente esos canales sin mostrar canales fantasma ni listas mock.
  - Presentación de pantalla de estado vacía estilizada cuando no existan canales favoritos, con botón de acceso directo a la Guía EPG.

#### Consistencia en Generador Backend (`proxy.cpp`)
- **Filtrado Estricto de Slugs en Favoritos**:
  - En `Proxy::generate_favorites_playlist()` y `Proxy::generate_auto_playlist()`, validación estricta que filtra contra el conjunto exacto de `fav_slugs` persistido en `epg_favorites.json`.

---

## [08.25.12] - 2026-08-25

### 🚀 Reparación de Statplugin, Grupo ⭐ Favoritos en `/aio` y Blindaje EPG

#### Reparación Inmediata de `statplugin` y Diagnóstico de Red
- **Endpoint de Plugins (`/statplugin?action=get_plugins` & `plugins.cpp`)**:
  - Protección con bloques `try/catch` y recuperación automática para devolver siempre HTTP 200 OK con JSON estructurado, evitando errores HTTP 500 ante plugins vacíos o no inicializados.
- **Diagnóstico y Control de Cloudflare WARP (`proxy.cpp` & `plugins.cpp`)**:
  - Sanitización de IP pública: filtrado preventivo que descarta `127.0.0.1` o rangos privados en el diagnóstico público cuando no hay ruta externa.
  - Reintento automático de conexión en `warp_connect` y `warp_toggle` antes de reportar el estado actualizado de la red.

#### Inyección del Grupo "⭐ Favoritos" en la Lista Global `/aio`
- **Generador de Lista AIO (`plugins.cpp` `AioPlugin` & `proxy.cpp` `/channels/aio.m3u`)**:
  - Inyección en la primera posición de la lista global `/aio` de los canales marcados en Favoritos con el tag `group-title="⭐ Favoritos"`.
  - Rutas virtuales automáticas persistentes: `http://<host>:<puerto>/auto/<slug>/stream.ts`.
  - Omisión limpia del bloque cuando no existen favoritos configurados.

#### Blindaje Definitivo de Favoritos y Eliminación de Listas Fantasma
- **Persistencia Inmune y Respaldo**:
  - Guardado sincronizado en `/app/http/listas/epg_favorites.json` y respaldo automático en `epg_favorites.json.bak`.
  - Eliminación definitiva de inyecciones por defecto forzadas (como `teledeporte` u otros canales); si la lista de favoritos está vacía, `/channels/favoritos.m3u` devuelve un M3U limpio sin canales no seleccionados.
- **Sincronización Bidireccional EPG**:
  - Carga inmediata en el montaje de `/epg/index.html` contra `GET /epg?action=get_favorites` y actualización instantánea del contador de estrellas de favoritos.

---

## [08.25.11] - 2026-08-25

### 🛡️ Validación Estricta de Fuentes Dinámicas y Acceso Rápido al Editor Interno

#### Validación de URLs en Fuentes Dinámicas Personalizadas (`/fuentes/` & `/listas/`)
- **Frontend (`fuentes/index.html` & `listas/index.html`)**:
  - Implementada validación obligatoria para que cualquier dirección URL de lista personalizada comience obligatoriamente por `http://`, `https://` o ruta local `/listas/locales/` (o `/`).
  - Detección inteligente de texto multilínea, nombres de canales (`Nombre - Hash` o `Nombre, Hash`), URLs `acestream://` o hashes planos de 40 caracteres hexadecimales pegados en el campo de URL remota.
  - Bloqueo inmediato del guardado y alerta al usuario: *"Has pegado canales/hashes en el campo de URL. Usa la sección 'Editar / Importar Lista Interna' para gestionar canales individuales."* con redirección asistida al editor interno.
- **Backend C++ (`proxy.cpp`, `util.cpp`, `plugins.cpp`)**:
  - Función de validación de sistema `httpace::is_valid_source_url()` para verificar la validez sintáctica de URLs de listas remotas y locales.
  - En `save_custom_list`, rechazo de URLs inválidas o hashes planos con código HTTP 400.
  - En `import_sources` y `create_plugins`, descarte automático de entradas de fuentes personalizadas con formato incorrecto.

#### Acceso Visible al Editor de Lista Interna
- **Botón Destacado en el Panel Superior (`/fuentes/`)**:
  - Incorporado botón verde esmeralda **"📝 Editor de Lista Interna"** en la barra de acciones superior.
  - Interacción dinámica (`openInternalEditor()`) con desplazamiento suave (*smooth scroll*), foco inmediato en el `<textarea>` y resalte perimetral luminoso (*glow effect*).
  - Tarjeta estilizada y delimitada para gestionar canales y hashes individuales sin interferir con las fuentes dinámicas.

---

## [08.25.10] - 2026-08-25

### 📝 Corrección Definitiva del Guardado de Lista Interna (Frontend & Backend)

#### Frontend en Fuentes (`/fuentes/index.html` & `/listas/index.html`)
- **Aislamiento Estricto de Lista Interna**:
  - El botón "Guardar Lista Interna" toma el texto íntegro del `<textarea>` y envía exclusivamente un único `POST /config?action=save_internal` (o `/statplugin?action=save_internal`) con payload JSON `{ "content": "..." }`.
  - Se elimina cualquier llamada o fragmentación hacia `custom_lists` / `save_plugins`, impidiendo la creación de entradas fragmentadas tipo `fuente_1`, `fuente_2`... en `plugins_state.json`.

#### Parser Robusto de Lista Interna (`proxy.cpp` `save_internal`)
- **Tolerancia de Pares y Líneas en Blanco**:
  - El parser tolera líneas en blanco intermedias entre el Nombre del Canal y su Hash/URL de 40 caracteres, manteniendo el `pending_name` intacto a través de saltos de línea.
  - Soporte de delimitadores adicionales (`,`, `-`, `:`, `;`, `|`) para nombres pegados con hashes en una misma línea.

---

## [08.25.09] - 2026-08-25

### 💾 Módulo de Lista Interna y Persistencia Garantizada de Favoritos

#### Importador de Lista Interna (`save_internal` & `proxy.cpp`)
- **Compilación a Archivo M3U Único**:
  - Corrección en el guardado de canales internos: en lugar de inyectar entradas individuales en `plugins_state.json`, el importador compila y guarda un único archivo `/app/http/listas/locales/interna.m3u` (y `Interna.m3u`).
  - Registro de la lista interna como una única fuente `custom_list`:
    `{ "name": "interna", "title": "Interna", "url": "/listas/locales/interna.m3u", "enabled": true }`.
  - Soporte universal de formatos de entrada: texto plano (`Nombre, Hash` o `Nombre \n Hash`), listas de URLs y bloques `#EXTINF:`.
  - Compatibilidad de endpoints: recepción y procesamiento de `save_internal` tanto en `/config` como en `/statplugin` y `/fuentes`.

#### Persistencia Garantizada de Favoritos (`epg_favorites.json`)
- **Directorio de Datos Persistente**:
  - Asegurada la ruta de persistencia en `/app/http/listas/epg_favorites.json` (mapeada a `/opt/HTTPAceProxy/httpaceproxycpp/http/listas/epg_favorites.json` en el host).
  - Creación automática y recursiva de directorios padre con `std::filesystem::create_directories(favs_file.parent_path())` tanto al arrancar el proxy como al guardar configuraciones.
- **Protección contra Sobrescrituras Accidentales**:
  - Al iniciar el proxy, se valida si el archivo `epg_favorites.json` ya existe en disco; de ser así, se carga íntegramente respetando la selección del usuario y nunca se sobreescribe con valores por defecto.
  - Añadido `*favorites.json` a `.gitignore` para evitar que comandos Git en el host sobreescriban los favoritos configurados por el usuario.

---

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
