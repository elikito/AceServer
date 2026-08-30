/**
 * HTTPAceProxy — Unified Navigation Component (v08.30.07)
 */
(function () {
    'use strict';

    const SUN_SVG = '<path d="M12 7a5 5 0 1 1-4.99 5A5 5 0 0 1 12 7zm0 2a3 3 0 1 0 3 3 3 3 0 0 0-3-3zm0-8a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0V2a1 1 0 0 1 1-1zm0 18a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0v-2a1 1 0 0 1 1-1zM5.636 4.222a1 1 0 0 1 0 1.414L4.222 7.05a1 1 0 1 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zm14.142 14.142a1 1 0 0 1 0 1.414l-1.414 1.414a1 1 0 0 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zM1 12a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2H2a1 1 0 0 1-1-1zm18 0a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2h-2a1 1 0 0 1-1-1zM5.636 19.778a1 1 0 0 1-1.414 0L2.808 18.364a1 1 0 1 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414zm14.142-14.142a1 1 0 0 1-1.414 0L16.95 4.222a1 1 0 0 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414z"/>';
    const MOON_SVG = '<path d="M12 3c.132 0 .263 0 .393.007a7.5 7.5 0 0 0 7.92 12.446A9 9 0 1 1 12 3zm0-2a11 11 0 1 0 10.978 11.978A9.5 9.5 0 0 1 12 1.022V1z"/>';

    function getSavedTheme() {
        return localStorage.getItem('aceproxy-theme') || localStorage.getItem('theme') || 'dark';
    }

    function updateIcons(isLight) {
        document.querySelectorAll('#theme-icon, .theme-icon').forEach(el => {
            el.innerHTML = isLight ? SUN_SVG : MOON_SVG;
        });
    }

    function applyTheme(themeName) {
        const isLight = themeName === 'light';
        document.documentElement.setAttribute('data-theme', isLight ? 'light' : 'dark');
        if (document.body) {
            document.body.classList.toggle('light-theme', isLight);
        }
        localStorage.setItem('aceproxy-theme', isLight ? 'light' : 'dark');
        localStorage.setItem('theme', isLight ? 'light' : 'dark');
        updateIcons(isLight);
    }

    function toggleTheme(e) {
        if (e) e.stopPropagation();
        const current = document.documentElement.getAttribute('data-theme') || (document.body && document.body.classList.contains('light-theme') ? 'light' : 'dark');
        const next = current === 'light' ? 'dark' : 'light';
        applyTheme(next);
    }

    // Apply immediately to head/html
    applyTheme(getSavedTheme());

    function initNavbar() {
        applyTheme(getSavedTheme());

        // Bind all theme toggles
        document.querySelectorAll('#theme-toggle, .theme-btn, .theme-toggle').forEach(btn => {
            btn.removeEventListener('click', toggleTheme);
            btn.addEventListener('click', toggleTheme);
        });

        const nav = document.querySelector('.navbar');
        if (!nav) return;

        const navToggle = document.getElementById('navbar-toggle');
        const navLinks = document.getElementById('nav-links');

        // 1. Highlight active link based on current location
        const path = window.location.pathname.toLowerCase();
        if (navLinks) {
            const links = navLinks.querySelectorAll('a');
            links.forEach(link => {
                const href = link.getAttribute('href').toLowerCase();
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

        // 2. Setup Responsive Hamburger Toggle & Event Handlers
        if (navToggle && navLinks) {
            function toggleMenu(e) {
                if (e) e.stopPropagation();
                const isOpen = navLinks.classList.toggle('show');
                navToggle.classList.toggle('is-active', isOpen);
                navToggle.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
            }

            function closeMenu() {
                if (navLinks.classList.contains('show')) {
                    navLinks.classList.remove('show');
                    navToggle.classList.remove('is-active');
                    navToggle.setAttribute('aria-expanded', 'false');
                }
            }

            navToggle.addEventListener('click', toggleMenu);

            document.addEventListener('click', function (event) {
                if (!nav.contains(event.target)) {
                    closeMenu();
                }
            });

            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape') {
                    closeMenu();
                }
            });

            navLinks.querySelectorAll('a').forEach(link => {
                link.addEventListener('click', () => {
                    closeMenu();
                });
            });

            window.addEventListener('resize', function () {
                if (window.innerWidth > 768) {
                    closeMenu();
                }
            });
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initNavbar);
    } else {
        initNavbar();
    }
})();
