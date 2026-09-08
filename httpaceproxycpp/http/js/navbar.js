/**
 * HTTPAceProxy — Unified Navigation Component (v09.08.04)
 * Provides two-row layout, 100% responsive navigation menu,
 * and Spotlight/ElasticSearch-like reactive channel/EPG search.
 */
(function () {
    'use strict';

    const SUN_SVG = '<path d="M12 7a5 5 0 1 1-4.99 5A5 5 0 0 1 12 7zm0 2a3 3 0 1 0 3 3 3 3 0 0 0-3-3zm0-8a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0V2a1 1 0 0 1 1-1zm0 18a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0v-2a1 1 0 0 1 1-1zM5.636 4.222a1 1 0 0 1 0 1.414L4.222 7.05a1 1 0 1 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zm14.142 14.142a1 1 0 0 1 0 1.414l-1.414 1.414a1 1 0 0 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zM1 12a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2H2a1 1 0 0 1-1-1zm18 0a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2h-2a1 1 0 0 1-1-1zM5.636 19.778a1 1 0 0 1-1.414 0L2.808 18.364a1 1 0 1 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414zm14.142-14.142a1 1 0 0 1-1.414 0L16.95 4.222a1 1 0 0 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414z"/>';
    const MOON_SVG = '<path d="M12 3c.132 0 .263 0 .393.007a7.5 7.5 0 0 0 7.92 12.446A9 9 0 1 1 12 3zm0-2a11 11 0 1 0 10.978 11.978A9.5 9.5 0 0 1 12 1.022V1z"/>';

    function getSavedTheme() {
        return localStorage.getItem('theme') || localStorage.getItem('aceproxy-theme') || 'dark';
    }

    function updateIcons(isLight) {
        document.querySelectorAll('#theme-icon, .theme-icon').forEach(el => {
            el.innerHTML = isLight ? SUN_SVG : MOON_SVG;
        });
    }

    function applyTheme(themeName) {
        const isLight = themeName === 'light';
        const mode = isLight ? 'light' : 'dark';
        document.documentElement.setAttribute('data-theme', mode);
        document.documentElement.classList.toggle('light', isLight);
        document.documentElement.classList.toggle('dark', !isLight);
        if (document.body) {
            document.body.setAttribute('data-theme', mode);
            document.body.classList.toggle('light-theme', isLight);
            document.body.classList.toggle('light', isLight);
            document.body.classList.toggle('dark', !isLight);
        }
        localStorage.setItem('theme', mode);
        localStorage.setItem('aceproxy-theme', mode);
        updateIcons(isLight);
        window.dispatchEvent(new CustomEvent('themeChanged', { detail: { theme: mode, isLight: isLight } }));
    }

    function toggleTheme(e) {
        if (e) {
            e.preventDefault();
            e.stopPropagation();
        }
        const current = document.documentElement.getAttribute('data-theme') ||
            (document.body && document.body.classList.contains('light-theme') ? 'light' : 'dark') ||
            'dark';
        const next = current === 'light' ? 'dark' : 'light';
        applyTheme(next);
    }

    window.applyTheme = applyTheme;
    window.toggleTheme = toggleTheme;

    // Apply immediately to prevent initial flicker
    applyTheme(getSavedTheme());

    // =========================================================================
    // Canonical Slug & Text Utilities
    // =========================================================================
    function toCanonicalSlug(name) {
        if (!name) return '';
        let n = name.toLowerCase()
            .normalize('NFD').replace(/[\u0300-\u036f]/g, '')
            .replace(/^\s*\d+[\.\s_–-]+/g, ' ') // Quitar prefijos tipo "12. "
            .replace(/[\(\[\{]\s*(?:mirror|replica|m|opt|alt)?\s*\d+\s*[\)\]\}]/gi, ' ')
            .replace(/\b(1080p|1080i|1080|720p|720i|720|576p|576i|480p|4k|uhd|fhda?|720a?|hd|sd|hevc|h265|h264|back|backup|opt|alt|directo|live|envivo|castellano|spanish|spain|espana|acestream)\b/gi, ' ')
            .replace(/[^a-z0-9]+/g, '-')
            .replace(/^-+|-+$/g, '');
        return n;
    }

    function normalizeSearchText(str) {
        if (!str) return '';
        return str
            .toLowerCase()
            .normalize('NFD').replace(/[\u0300-\u036f]/g, '')
            .replace(/movistar\+/g, 'movistar')
            .replace(/m\+/g, 'movistar')
            .replace(/m\./g, 'movistar')
            .replace(/\bm\b/g, 'movistar')
            .replace(/[^a-z0-9]+/g, ' ')
            .trim();
    }

    function extractDialNumber(name) {
        if (!name) return '';
        const m = name.match(/^\s*(\d{1,3})[\.\s_–-]+/);
        return m ? m[1] : '';
    }

    function formatTimeHour(date) {
        if (!date || isNaN(date.getTime())) return '';
        const h = String(date.getHours()).padStart(2, '0');
        const m = String(date.getMinutes()).padStart(2, '0');
        return `${h}:${m}`;
    }

    // =========================================================================
    // Global Channel & EPG Catalog Manager
    // =========================================================================
    let globalCatalog = {
        channels: [], // Deduplicated canonical channels: { name, slug, dial, logo, content_id, isFavorite, nowProg }
        favoritesSet: new Set(),
        customLogos: {},
        isLoaded: false,
        isLoading: false
    };

    async function loadGlobalCatalog() {
        if (globalCatalog.isLoaded || globalCatalog.isLoading) return;
        globalCatalog.isLoading = true;

        try {
            // 1. Cargar favoritos desde servidor y localStorage
            const localFavs = JSON.parse(localStorage.getItem('epg_favorites') || localStorage.getItem('ace_player_favorites') || '[]');
            localFavs.forEach(f => {
                if (f) {
                    globalCatalog.favoritesSet.add(f.toLowerCase());
                    const s = toCanonicalSlug(f);
                    if (s) globalCatalog.favoritesSet.add(s);
                }
            });

            const favPromise = fetch('/epg?action=get_favorites')
                .then(r => r.ok ? r.json() : null)
                .then(data => {
                    if (data && Array.isArray(data.favorites)) {
                        data.favorites.forEach(f => {
                            if (f) {
                                globalCatalog.favoritesSet.add(f.toLowerCase());
                                const s = toCanonicalSlug(f);
                                if (s) globalCatalog.favoritesSet.add(s);
                            }
                        });
                    }
                })
                .catch(() => { });

            // 2. Cargar logos personalizados
            const logoPromise = fetch('/epg?action=get_custom_logos')
                .then(r => r.ok ? r.json() : null)
                .then(data => {
                    if (data && data.logos) {
                        globalCatalog.customLogos = data.logos;
                    }
                })
                .catch(() => { });

            // 3. Cargar plugins y canales
            const pluginsPromise = fetch('/statplugin?action=get_plugins')
                .then(r => r.ok ? r.json() : null)
                .catch(() => null);

            const [_, __, pluginsData] = await Promise.all([favPromise, logoPromise, pluginsPromise]);

            const channelsMap = new Map(); // slug -> channel object

            if (pluginsData && Array.isArray(pluginsData.plugins)) {
                pluginsData.plugins.forEach(p => {
                    if (!p || !Array.isArray(p.channels)) return;
                    p.channels.forEach(ch => {
                        if (!ch || !ch.name) return;
                        const slug = toCanonicalSlug(ch.name);
                        if (!slug) return;

                        const dial = extractDialNumber(ch.name);
                        const cid = ch.content_id || '';
                        const customLogo = globalCatalog.customLogos[slug] || globalCatalog.customLogos[ch.name.toLowerCase()] || '';
                        const logo = customLogo || ch.logo || '';

                        const isFav = globalCatalog.favoritesSet.has(ch.name.toLowerCase()) ||
                            globalCatalog.favoritesSet.has(slug) ||
                            (dial && globalCatalog.favoritesSet.has(dial));

                        if (!channelsMap.has(slug)) {
                            channelsMap.set(slug, {
                                name: ch.name.replace(/^\s*\d+[\.\s_–-]+/, '').trim() || ch.name,
                                rawName: ch.name,
                                slug: slug,
                                dial: dial,
                                logo: logo,
                                content_id: cid,
                                isFavorite: isFav,
                                group: ch.group || p.name || 'General',
                                source: p.name || ''
                            });
                        } else {
                            // Actualizar con mejor logo o content_id si no tenía
                            const existing = channelsMap.get(slug);
                            if (!existing.logo && logo) existing.logo = logo;
                            if (!existing.content_id && cid) existing.content_id = cid;
                            if (isFav) existing.isFavorite = true;
                            if (!existing.dial && dial) existing.dial = dial;
                        }
                    });
                });
            }

            // Si hay canales en la página actual (ej. player o epg), complementar
            if (window.allChannels && Array.isArray(window.allChannels)) {
                window.allChannels.forEach(ch => {
                    const slug = toCanonicalSlug(ch.name);
                    if (!slug) return;
                    const dial = extractDialNumber(ch.name);
                    const isFav = globalCatalog.favoritesSet.has(ch.name.toLowerCase()) ||
                        globalCatalog.favoritesSet.has(slug);
                    if (!channelsMap.has(slug)) {
                        channelsMap.set(slug, {
                            name: ch.name.replace(/^\s*\d+[\.\s_–-]+/, '').trim() || ch.name,
                            rawName: ch.name,
                            slug: slug,
                            dial: dial,
                            logo: ch.logo || globalCatalog.customLogos[slug] || '',
                            content_id: ch.aceId || '',
                            isFavorite: isFav,
                            group: ch.group || 'General',
                            source: ch.source || ''
                        });
                    } else {
                        const ex = channelsMap.get(slug);
                        if (!ex.logo && ch.logo) ex.logo = ch.logo;
                        if (!ex.content_id && ch.aceId) ex.content_id = ch.aceId;
                        if (isFav) ex.isFavorite = true;
                    }
                });
            }

            // Convertir a lista ordenada
            globalCatalog.channels = Array.from(channelsMap.values());
            globalCatalog.isLoaded = true;

            // Intentar enriquecer con EPG activo en memoria o en caché
            enrichChannelsWithEpg();

        } catch (e) {
            console.warn('[Navbar] Error cargando catálogo global:', e);
        } finally {
            globalCatalog.isLoading = false;
        }
    }

    function enrichChannelsWithEpg() {
        const now = new Date();

        // 1. Si existe epgCache o epgProgrammes en el entorno window (ej. en /epg o /player)
        const progMap = window.epgProgrammes || window.epgCache || null;

        // 2. Si no, consultar caché ligera de sessionStorage
        let storedEpg = null;
        try {
            const raw = sessionStorage.getItem('ace_epg_now_cache');
            if (raw) storedEpg = JSON.parse(raw);
        } catch (e) { }

        globalCatalog.channels.forEach(ch => {
            let foundProg = null;

            if (progMap) {
                // Buscar programas para este canal por id o por slug
                const list = progMap[ch.slug] || progMap[ch.name] || progMap[ch.rawName] || null;
                if (Array.isArray(list)) {
                    foundProg = list.find(p => {
                        const st = p.start instanceof Date ? p.start : new Date(p.start);
                        const sp = p.stop instanceof Date ? p.stop : new Date(p.stop);
                        return st <= now && sp >= now;
                    });
                }
            }

            if (!foundProg && storedEpg && storedEpg[ch.slug]) {
                const sp = storedEpg[ch.slug];
                const st = new Date(sp.start);
                const et = new Date(sp.stop);
                if (st <= now && et >= now) {
                    foundProg = sp;
                }
            }

            if (foundProg) {
                const startDate = foundProg.start instanceof Date ? foundProg.start : new Date(foundProg.start);
                const stopDate = foundProg.stop instanceof Date ? foundProg.stop : new Date(foundProg.stop);
                ch.nowProg = {
                    title: foundProg.title || '',
                    desc: foundProg.desc || '',
                    timeRange: `${formatTimeHour(startDate)} - ${formatTimeHour(stopDate)}`
                };
            }
        });
    }

    // =========================================================================
    // Search Filtering & Ranking
    // =========================================================================
    function filterGlobalChannels(query) {
        if (!query || !query.trim()) return [];
        const normQuery = normalizeSearchText(query);
        const tokens = normQuery.split(/\s+/).filter(Boolean);
        if (tokens.length === 0) return [];

        const results = [];

        globalCatalog.channels.forEach(ch => {
            const nameNorm = normalizeSearchText(ch.name + ' ' + ch.rawName + ' ' + ch.slug);
            const dialNorm = ch.dial ? String(ch.dial) : '';
            const epgTitleNorm = ch.nowProg ? normalizeSearchText(ch.nowProg.title) : '';
            const epgDescNorm = ch.nowProg ? normalizeSearchText(ch.nowProg.desc) : '';
            const cidNorm = ch.content_id ? ch.content_id.toLowerCase() : '';

            // Token matching
            const allTokensMatch = tokens.every(tok => {
                return nameNorm.includes(tok) ||
                    dialNorm === tok ||
                    epgTitleNorm.includes(tok) ||
                    epgDescNorm.includes(tok) ||
                    cidNorm.includes(tok);
            });

            if (allTokensMatch) {
                // Calcular relevancia
                let score = 0;
                if (ch.isFavorite) score += 1000;
                if (nameNorm.startsWith(normQuery)) score += 500;
                else if (nameNorm.includes(normQuery)) score += 250;
                if (epgTitleNorm.includes(normQuery)) score += 150;
                if (dialNorm === normQuery) score += 400;

                results.push({ channel: ch, score: score });
            }
        });

        // Ordenar: 1º Favoritos y mayor score, 2º Resto
        results.sort((a, b) => b.score - a.score);

        return results.slice(0, 16).map(r => r.channel);
    }

    // =========================================================================
    // Render Results Dropdown
    // =========================================================================
    let selectedResultIndex = -1;

    function renderSearchResults(results, query, dropdownEl, listEl, headerEl) {
        listEl.innerHTML = '';
        selectedResultIndex = -1;

        if (results.length === 0) {
            headerEl.textContent = 'Sin resultados';
            listEl.innerHTML = `
                <div class="search-empty-state">
                    No se encontraron canales ni programas para "<strong>${escapeHtml(query)}</strong>"
                </div>
            `;
            dropdownEl.style.display = 'flex';
            return;
        }

        const favCount = results.filter(r => r.isFavorite).length;
        headerEl.textContent = `${results.length} coincidencias${favCount > 0 ? ` (${favCount} ⭐)` : ''}`;

        results.forEach((ch, idx) => {
            const item = document.createElement('div');
            item.className = 'search-result-item';
            item.dataset.index = idx;
            item.dataset.slug = ch.slug;
            item.dataset.cid = ch.content_id || '';

            const logoHtml = ch.logo
                ? `<img src="${escapeHtml(ch.logo)}" alt="" loading="lazy" onerror="this.onerror=null;this.parentNode.innerHTML='<span class=\\'logo-fallback\\'>${escapeHtml(ch.name.substring(0, 2).toUpperCase())}</span>';">`
                : `<span class="logo-fallback">${escapeHtml(ch.name.substring(0, 2).toUpperCase())}</span>`;

            const dialHtml = ch.dial ? `<span class="search-dial-badge">#${escapeHtml(ch.dial)}</span>` : '';
            const favHtml = ch.isFavorite ? `<span class="search-fav-star" title="Canal Favorito">⭐</span>` : '';

            let epgHtml = '';
            if (ch.nowProg && ch.nowProg.title) {
                epgHtml = `
                    <div class="search-channel-row-epg">
                        <span class="search-epg-time">🕒 ${escapeHtml(ch.nowProg.timeRange)}</span>
                        <span class="search-epg-title">${escapeHtml(ch.nowProg.title)}</span>
                    </div>
                `;
            } else {
                epgHtml = `
                    <div class="search-channel-row-epg">
                        <span style="opacity:0.6;">${escapeHtml(ch.group || 'En Directo')}</span>
                    </div>
                `;
            }

            let cidHtml = '';
            if (ch.content_id && ch.content_id.length >= 10) {
                const truncated = ch.content_id.substring(0, 6) + '...' + ch.content_id.substring(ch.content_id.length - 6);
                cidHtml = `<span class="search-cid-pill" title="Content ID: ${escapeHtml(ch.content_id)}">${escapeHtml(truncated)}</span>`;
            }

            item.innerHTML = `
                <div class="search-channel-logo">${logoHtml}</div>
                <div class="search-channel-info">
                    <div class="search-channel-row-top">
                        ${dialHtml}
                        <span class="search-channel-name">${escapeHtml(ch.name)}</span>
                        ${favHtml}
                    </div>
                    ${epgHtml}
                </div>
                <div class="search-channel-row-meta">
                    ${cidHtml}
                    <span class="search-action-hint">Ver ➔</span>
                </div>
            `;

            item.addEventListener('click', () => {
                selectChannelResult(ch);
            });

            listEl.appendChild(item);
        });

        dropdownEl.style.display = 'flex';
    }

    function selectChannelResult(ch) {
        if (!ch) return;

        // Ocultar dropdown
        const dropdown = document.getElementById('global-search-dropdown');
        if (dropdown) dropdown.style.display = 'none';

        const searchInput = document.getElementById('global-search-input');
        if (searchInput) searchInput.value = '';
        const clearBtn = document.getElementById('global-search-clear');
        if (clearBtn) clearBtn.style.display = 'none';

        const targetSlug = ch.slug;
        const targetCid = ch.content_id;

        // 1. Si ya estamos en /player/ o /player/index.html y existe función de reproducción nativa
        if (window.location.pathname.includes('/player') && typeof window.playChannelInWebPlayer === 'function') {
            const streamUrl = `/auto/${encodeURIComponent(targetSlug)}/stream.ts`;
            try {
                window.playChannelInWebPlayer(streamUrl, ch.name, targetCid || '');
                window.history.pushState({}, '', `/player?url=/auto/${encodeURIComponent(targetSlug)}`);
                return;
            } catch (e) {
                console.warn('[Navbar] Fallback de reproducción directa:', e);
            }
        }

        // 2. Redirigir directamente a /player con la URL virtual canónica
        if (targetSlug) {
            window.location.href = `/player?url=/auto/${encodeURIComponent(targetSlug)}`;
        } else if (targetCid) {
            window.location.href = `/player?cid=${encodeURIComponent(targetCid)}`;
        }
    }

    function escapeHtml(str) {
        if (!str) return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
    }

    // =========================================================================
    // Reestructuración Dinámica y Homogénea del Navbar (Two-Row Layout)
    // =========================================================================
    function setupTwoRowLayout(nav) {
        // Comprobar si ya está reestructurado
        let topBar = nav.querySelector('.navbar-top');
        let bottomBar = nav.querySelector('.navbar-bottom');

        if (!topBar || !bottomBar) {
            // Reestructurar dinámicamente preservando contenido
            const brandEl = nav.querySelector('.nav-brand') || document.createElement('a');
            if (!nav.querySelector('.nav-brand')) {
                brandEl.className = 'nav-brand';
                brandEl.href = '/stat';
                brandEl.textContent = 'HTTPAceProxy';
            }

            const navLinksEl = nav.querySelector('.nav-links') || document.createElement('div');
            navLinksEl.id = 'nav-links';
            navLinksEl.className = 'nav-links';

            // Limpiar nav
            nav.innerHTML = '';

            // Crear fila superior
            topBar = document.createElement('div');
            topBar.className = 'navbar-top';

            const brandSection = document.createElement('div');
            brandSection.className = 'navbar-brand-section';
            brandSection.appendChild(brandEl);

            const actionsSection = document.createElement('div');
            actionsSection.className = 'navbar-actions-section';
            actionsSection.innerHTML = `
                <div class="global-search-container" id="global-search-container">
                    <div class="global-search-box">
                        <svg class="search-icon" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2">
                            <circle cx="11" cy="11" r="8"></circle>
                            <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
                        </svg>
                        <input type="text" id="global-search-input" class="global-search-input" placeholder="Buscar canal o evento..." autocomplete="off" spellcheck="false" aria-label="Buscador global">
                        <button type="button" id="global-search-clear" class="global-search-clear" aria-label="Limpiar búsqueda" style="display:none;">&times;</button>
                        <span class="search-badge-kbd">/</span>
                    </div>
                    <div class="global-search-dropdown" id="global-search-dropdown" style="display:none;">
                        <div class="search-results-header" id="search-results-header">Resultados</div>
                        <div class="search-results-list" id="search-results-list"></div>
                    </div>
                </div>
            `;

            topBar.appendChild(brandSection);
            topBar.appendChild(actionsSection);

            // Crear fila inferior
            bottomBar = document.createElement('div');
            bottomBar.className = 'navbar-bottom';
            bottomBar.appendChild(navLinksEl);

            nav.appendChild(topBar);
            nav.appendChild(bottomBar);
        } else {
            // Si ya existe la estructura, asegurar que el buscador esté dentro de .navbar-actions-section
            if (!topBar.querySelector('#global-search-container')) {
                const actionsSection = topBar.querySelector('.navbar-actions-section') || topBar;
                const searchWrapper = document.createElement('div');
                searchWrapper.className = 'global-search-container';
                searchWrapper.id = 'global-search-container';
                searchWrapper.innerHTML = `
                    <div class="global-search-box">
                        <svg class="search-icon" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2">
                            <circle cx="11" cy="11" r="8"></circle>
                            <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
                        </svg>
                        <input type="text" id="global-search-input" class="global-search-input" placeholder="Buscar canal o evento..." autocomplete="off" spellcheck="false" aria-label="Buscador global">
                        <button type="button" id="global-search-clear" class="global-search-clear" aria-label="Limpiar búsqueda" style="display:none;">&times;</button>
                        <span class="search-badge-kbd">/</span>
                    </div>
                    <div class="global-search-dropdown" id="global-search-dropdown" style="display:none;">
                        <div class="search-results-header" id="search-results-header">Resultados</div>
                        <div class="search-results-list" id="search-results-list"></div>
                    </div>
                `;
                actionsSection.prepend(searchWrapper);
            }
        }

        // Asegurar que nav-links incluya todos los enlaces requeridos
        const navLinks = nav.querySelector('#nav-links');
        if (navLinks) {
            const requiredLinks = [
                { href: '/stat', text: 'Dashboard', key: 'stat' },
                { href: '/statplugin/', text: 'Canales', key: 'statplugin' },
                { href: '/fuentes/', text: 'Fuentes', key: 'fuentes' },
                { href: '/epg/', text: 'EPG', key: 'epg' },
                { href: '/player/index.html', text: 'Reproductor', key: 'player' },
                { href: '/player/legacy.html', text: 'Reproductor Legacy', key: 'legacy' },
                { href: '/mobile/index.html', text: 'Móvil', key: 'mobile' }
            ];

            const existingHrefs = Array.from(navLinks.querySelectorAll('a')).map(a => a.getAttribute('href') || '');
            requiredLinks.forEach(item => {
                const found = existingHrefs.some(h => {
                    if (item.key === 'legacy') return h.includes('legacy');
                    if (item.key === 'player') return h.includes('player') && !h.includes('legacy');
                    if (item.key === 'mobile') return h.includes('mobile') || h.endsWith('/m');
                    return h.includes(item.key);
                });
                if (!found) {
                    const a = document.createElement('a');
                    a.href = item.href;
                    a.textContent = item.text;
                    navLinks.appendChild(a);
                }
            });
        }
    }

    // =========================================================================
    // Inicialización del Navbar y Event Handlers
    // =========================================================================
    function initNavbar() {
        applyTheme(getSavedTheme());

        const nav = document.querySelector('.navbar');
        if (!nav) return;

        setupTwoRowLayout(nav);

        // Vincular botón de tema
        document.querySelectorAll('#theme-toggle, .theme-btn, .theme-toggle').forEach(btn => {
            btn.onclick = toggleTheme;
        });
        updateIcons(getSavedTheme() === 'light');

        // Resaltar pestaña activa
        const path = window.location.pathname.toLowerCase();
        const navLinks = document.getElementById('nav-links');
        if (navLinks) {
            const links = navLinks.querySelectorAll('a');
            links.forEach(link => {
                const href = (link.getAttribute('href') || '').toLowerCase();
                let isActive = false;

                if (href.includes('legacy') && path.includes('legacy')) {
                    isActive = true;
                } else if (!href.includes('legacy') && href.includes('player') && path.includes('player') && !path.includes('legacy')) {
                    isActive = true;
                } else if (href.includes('statplugin') && path.includes('statplugin')) {
                    isActive = true;
                } else if (href.includes('fuentes') && path.includes('fuentes')) {
                    isActive = true;
                } else if (href.includes('epg') && path.includes('epg')) {
                    isActive = true;
                } else if ((href.includes('mobile') || href.endsWith('/m')) && (path.includes('mobile') || path.endsWith('/m') || path.includes('/m/'))) {
                    isActive = true;
                } else if (href.includes('listas') && path.includes('listas')) {
                    isActive = true;
                } else if ((href === '/stat' || href === '/stat/' || href === '/') &&
                    (path === '/stat' || path === '/stat/' || path === '/' || path === '/index.html' || (path.endsWith('/stat') || path.endsWith('/stat/')))) {
                    isActive = true;
                }

                if (isActive) {
                    link.classList.add('active');
                } else {
                    link.classList.remove('active');
                }
            });
        }

        // =====================================================================
        // Setup Reactive Global Search
        // =====================================================================
        const searchInput = document.getElementById('global-search-input');
        const clearBtn = document.getElementById('global-search-clear');
        const dropdown = document.getElementById('global-search-dropdown');
        const resultsList = document.getElementById('search-results-list');
        const resultsHeader = document.getElementById('search-results-header');
        const searchContainer = document.getElementById('global-search-container');

        if (searchInput && dropdown && resultsList && resultsHeader) {
            let debounceTimer = null;

            function doSearch() {
                const q = searchInput.value.trim();
                if (clearBtn) {
                    clearBtn.style.display = q ? 'block' : 'none';
                }

                if (!q) {
                    dropdown.style.display = 'none';
                    return;
                }

                const filtered = filterGlobalChannels(q);
                renderSearchResults(filtered, q, dropdown, resultsList, resultsHeader);
            }

            searchInput.addEventListener('input', () => {
                clearTimeout(debounceTimer);
                debounceTimer = setTimeout(doSearch, 200); // 200 ms debounce
            });

            searchInput.addEventListener('focus', () => {
                // Iniciar precarga de catálogo en primer foco si no se había cargado
                if (!globalCatalog.isLoaded && !globalCatalog.isLoading) {
                    loadGlobalCatalog();
                }
                if (searchInput.value.trim()) {
                    doSearch();
                }
            });

            if (clearBtn) {
                clearBtn.addEventListener('click', () => {
                    searchInput.value = '';
                    clearBtn.style.display = 'none';
                    dropdown.style.display = 'none';
                    searchInput.focus();
                });
            }

            // Atajos de Teclado Globales: / o Cmd+K / Ctrl+K
            document.addEventListener('keydown', (e) => {
                const activeTag = document.activeElement ? document.activeElement.tagName.toLowerCase() : '';
                const isInputActive = (activeTag === 'input' || activeTag === 'textarea' || document.activeElement.isContentEditable);

                if ((e.key === '/' && !isInputActive) || ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k')) {
                    e.preventDefault();
                    searchInput.focus();
                    searchInput.select();
                } else if (e.key === 'Escape') {
                    if (dropdown.style.display !== 'none') {
                        dropdown.style.display = 'none';
                        searchInput.blur();
                    }
                } else if (dropdown.style.display !== 'none') {
                    const items = resultsList.querySelectorAll('.search-result-item');
                    if (items.length === 0) return;

                    if (e.key === 'ArrowDown') {
                        e.preventDefault();
                        selectedResultIndex = (selectedResultIndex + 1) % items.length;
                        highlightItem(items, selectedResultIndex);
                    } else if (e.key === 'ArrowUp') {
                        e.preventDefault();
                        selectedResultIndex = (selectedResultIndex - 1 + items.length) % items.length;
                        highlightItem(items, selectedResultIndex);
                    } else if (e.key === 'Enter') {
                        if (selectedResultIndex >= 0 && selectedResultIndex < items.length) {
                            e.preventDefault();
                            items[selectedResultIndex].click();
                        } else if (items.length > 0) {
                            e.preventDefault();
                            items[0].click();
                        }
                    }
                }
            });

            function highlightItem(items, idx) {
                items.forEach((it, i) => {
                    it.classList.toggle('selected', i === idx);
                });
                if (items[idx]) {
                    items[idx].scrollIntoView({ block: 'nearest' });
                }
            }

            // Cerrar dropdown al hacer clic fuera
            document.addEventListener('click', (e) => {
                if (searchContainer && !searchContainer.contains(e.target)) {
                    dropdown.style.display = 'none';
                }
            });
        }

        // Cargar catálogo global de forma diferida tras inicializar la página
        if ('requestIdleCallback' in window) {
            window.requestIdleCallback(() => loadGlobalCatalog(), { timeout: 2000 });
        } else {
            setTimeout(loadGlobalCatalog, 1000);
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initNavbar);
    } else {
        initNavbar();
    }
})();

