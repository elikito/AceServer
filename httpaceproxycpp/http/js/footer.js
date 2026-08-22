/**
 * HTTPAceProxy - Unified Footer Component
 * Proporciona coherencia en la barra inferior (Footer):
 *  - Sección Izquierda: IP y Hostname (clic para copiar IP)
 *  - Sección Central: Alternador de tema Claro / Oscuro
 *  - Sección Derecha: Versión de la app (clic para copiar versión)
 */

(function () {
    const SUN_SVG = '<path d="M12 7a5 5 0 1 1-4.99 5A5 5 0 0 1 12 7zm0 2a3 3 0 1 0 3 3 3 3 0 0 0-3-3zm0-8a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0V2a1 1 0 0 1 1-1zm0 18a1 1 0 0 1 1 1v2a1 1 0 0 1-2 0v-2a1 1 0 0 1 1-1zM5.636 4.222a1 1 0 0 1 0 1.414L4.222 7.05a1 1 0 1 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zm14.142 14.142a1 1 0 0 1 0 1.414l-1.414 1.414a1 1 0 0 1-1.414-1.414l1.414-1.414a1 1 0 0 1 1.414 0zM1 12a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2H2a1 1 0 0 1-1-1zm18 0a1 1 0 0 1 1-1h2a1 1 0 0 1 0 2h-2a1 1 0 0 1-1-1zM5.636 19.778a1 1 0 0 1-1.414 0L2.808 18.364a1 1 0 1 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414zm14.142-14.142a1 1 0 0 1-1.414 0L16.95 4.222a1 1 0 0 1 1.414-1.414l1.414 1.414a1 1 0 0 1 0 1.414z"/>';
    const MOON_SVG = '<path d="M12 3c.132 0 .263 0 .393.007a7.5 7.5 0 0 0 7.92 12.446A9 9 0 1 1 12 3zm0-2a11 11 0 1 0 10.978 11.978A9.5 9.5 0 0 1 12 1.022V1z"/>';

    let currentIp = window.location.hostname || '127.0.0.1';
    let currentVersion = '08.22.02';

    function copyToClipboard(text) {
        if (navigator.clipboard && window.isSecureContext) {
            return navigator.clipboard.writeText(text);
        }
        return new Promise((resolve, reject) => {
            const textArea = document.createElement('textarea');
            textArea.value = text;
            textArea.style.position = 'fixed';
            textArea.style.opacity = '0';
            document.body.appendChild(textArea);
            textArea.focus();
            textArea.select();
            try {
                const successful = document.execCommand('copy');
                document.body.removeChild(textArea);
                if (successful) resolve();
                else reject(new Error('execCommand copy failed'));
            } catch (err) {
                document.body.removeChild(textArea);
                reject(err);
            }
        });
    }

    function initFooter() {
        const footerLeft = document.querySelector('.footer-left');
        const footerRight = document.querySelector('.footer-right');
        const serverIpEl = document.getElementById('server-ip');
        const versionEls = document.querySelectorAll('.app-version');
        const themeToggle = document.getElementById('theme-toggle');
        const themeIcon = document.getElementById('theme-icon');

        // 1. Mostrar IP inicial
        if (serverIpEl) {
            serverIpEl.textContent = currentIp;
        }

        // 2. Obtener estado del sistema (IP, Hostname y Versión)
        fetch('/stat/?action=get_status')
            .then(r => r.json())
            .then(data => {
                const sysHostname = data.server_info?.hostname || data.sys_info?.hostname || '';
                const ver = data.version || data.server_info?.version || '';
                if (sysHostname && serverIpEl) {
                    serverIpEl.textContent = `${currentIp} (${sysHostname})`;
                }
                if (ver) {
                    currentVersion = ver;
                    versionEls.forEach(el => el.textContent = ver);
                }
            })
            .catch(() => {});

        fetch('/config?action=get_config')
            .then(r => r.json())
            .then(cfg => {
                if (cfg && cfg.version) {
                    currentVersion = cfg.version;
                    versionEls.forEach(el => el.textContent = cfg.version);
                }
            })
            .catch(() => {});

        // 3. Copiado de IP al hacer clic en footer-left
        if (footerLeft) {
            footerLeft.style.cursor = 'pointer';
            footerLeft.title = 'Clic para copiar la IP del servidor';
            footerLeft.addEventListener('click', () => {
                copyToClipboard(currentIp).then(() => {
                    const originalText = serverIpEl ? serverIpEl.textContent : '';
                    if (serverIpEl) {
                        serverIpEl.textContent = '✓ Copiado!';
                        serverIpEl.classList.add('copied');
                    }
                    setTimeout(() => {
                        if (serverIpEl) {
                            serverIpEl.textContent = originalText;
                            serverIpEl.classList.remove('copied');
                        }
                    }, 1500);
                }).catch(err => console.error('Error al copiar IP:', err));
            });
        }

        // 4. Copiado de Versión al hacer clic en footer-right
        if (footerRight) {
            footerRight.style.cursor = 'pointer';
            footerRight.title = 'Clic para copiar el número de versión';
            footerRight.addEventListener('click', () => {
                copyToClipboard(currentVersion).then(() => {
                    versionEls.forEach(el => {
                        const orig = el.textContent;
                        el.textContent = '✓ Copiado!';
                        el.classList.add('copied');
                        setTimeout(() => {
                            el.textContent = orig;
                            el.classList.remove('copied');
                        }, 1500);
                    });
                }).catch(err => console.error('Error al copiar versión:', err));
            });
        }

        // 5. Gestión del tema Claro / Oscuro
        function updateThemeIcon(isLight) {
            if (!themeIcon) return;
            themeIcon.innerHTML = isLight ? SUN_SVG : MOON_SVG;
        }

        if (localStorage.getItem('theme') === 'light') {
            document.body.classList.add('light-theme');
            updateThemeIcon(true);
        } else {
            document.body.classList.remove('light-theme');
            updateThemeIcon(false);
        }

        if (themeToggle) {
            themeToggle.addEventListener('click', (e) => {
                e.stopPropagation();
                document.body.classList.toggle('light-theme');
                const isLight = document.body.classList.contains('light-theme');
                localStorage.setItem('theme', isLight ? 'light' : 'dark');
                updateThemeIcon(isLight);
            });
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initFooter);
    } else {
        initFooter();
    }
})();
