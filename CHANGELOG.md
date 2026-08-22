# Registro de Cambios (Changelog) - HTTPAceProxy

Todos los cambios notables en este proyecto se documentan en este archivo.

El formato sigue las directrices de [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

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
