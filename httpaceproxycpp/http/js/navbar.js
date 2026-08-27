/**
 * HTTPAceProxy — Unified Navigation Component (v08.27.05)
 */
(function () {
    'use strict';

    function initNavbar() {
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

            // Close when clicking outside
            document.addEventListener('click', function (event) {
                if (!nav.contains(event.target)) {
                    closeMenu();
                }
            });

            // Close on Escape key
            document.addEventListener('keydown', function (event) {
                if (event.key === 'Escape') {
                    closeMenu();
                }
            });

            // Close when clicking any nav link
            navLinks.querySelectorAll('a').forEach(link => {
                link.addEventListener('click', () => {
                    closeMenu();
                });
            });

            // Close when resizing to desktop
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
