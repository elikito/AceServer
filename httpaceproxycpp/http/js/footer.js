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

    let canonicalIp = window.location.hostname || '127.0.0.1';
    let canonicalHostname = '';
    let canonicalVersion = '08.27.05';
    let ipResetTimer = null;
    let verResetTimer = null;

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
            serverIpEl.textContent = canonicalIp;
        }

        // 2. Obtener estado del sistema (IP, Hostname y Versión)
        fetch('/stat/?action=get_status')
            .then(r => r.json())
            .then(data => {
                canonicalHostname = data.server_info?.hostname || data.sys_info?.hostname || '';
                const ver = data.version || data.server_info?.version || '';
                if (serverIpEl && !ipResetTimer) {
                    serverIpEl.textContent = canonicalHostname ? `${canonicalIp} (${canonicalHostname})` : canonicalIp;
                }
                if (ver) {
                    canonicalVersion = ver;
                    if (!verResetTimer) {
                        versionEls.forEach(el => el.textContent = ver);
                    }
                }
            })
            .catch(() => {});

        fetch('/config?action=get_config')
            .then(r => r.json())
            .then(cfg => {
                if (cfg && cfg.version) {
                    canonicalVersion = cfg.version;
                    if (!verResetTimer) {
                        versionEls.forEach(el => el.textContent = cfg.version);
                    }
                }
            })
            .catch(() => {});

        // 3. Copiado de IP al hacer clic en footer-left (copia siempre la IP canónica)
        if (footerLeft) {
            footerLeft.style.cursor = 'pointer';
            footerLeft.title = 'Clic para copiar la IP del servidor';
            footerLeft.addEventListener('click', () => {
                copyToClipboard(canonicalIp).then(() => {
                    if (ipResetTimer) clearTimeout(ipResetTimer);
                    if (serverIpEl) {
                        serverIpEl.textContent = '✓ Copiado!';
                        serverIpEl.classList.add('copied');
                    }
                    ipResetTimer = setTimeout(() => {
                        if (serverIpEl) {
                            serverIpEl.textContent = canonicalHostname ? `${canonicalIp} (${canonicalHostname})` : canonicalIp;
                            serverIpEl.classList.remove('copied');
                        }
                        ipResetTimer = null;
                    }, 1500);
                }).catch(err => console.error('Error al copiar IP:', err));
            });
        }

        // 4. Copiado de Versión al hacer clic en footer-right (copia siempre la versión canónica)
        if (footerRight) {
            footerRight.style.cursor = 'pointer';
            footerRight.title = 'Clic para copiar el número de versión';
            footerRight.addEventListener('click', () => {
                copyToClipboard(canonicalVersion).then(() => {
                    if (verResetTimer) clearTimeout(verResetTimer);
                    const footerBadge = footerRight.querySelector('.version-badge') || footerRight.querySelector('.app-version') || footerRight;
                    if (footerBadge) {
                        footerBadge.textContent = '✓ Copiado!';
                        footerBadge.classList.add('copied');
                    }
                    verResetTimer = setTimeout(() => {
                        if (footerBadge) {
                            footerBadge.textContent = canonicalVersion;
                            footerBadge.classList.remove('copied');
                        }
                        verResetTimer = null;
                    }, 1500);
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

    // Global Toast Notification Helper
    window.showToast = window.showToast || function (message, type = 'success') {
        let toast = document.getElementById('toast-notification');
        if (!toast) {
            toast = document.createElement('div');
            toast.id = 'toast-notification';
            document.body.appendChild(toast);
        }
        toast.className = 'toast-notification ' + (type === 'error' ? 'error' : 'success');
        toast.textContent = message;
        toast.classList.add('show');
        clearTimeout(window.toastTimeout);
        window.toastTimeout = setTimeout(() => {
            toast.classList.remove('show');
        }, 3500);
    };

    // Global Confirm Modal Helper (Replaces native browser confirm())
    window.showConfirmModal = function ({
        title = 'Confirmar Acción',
        message = '',
        confirmText = 'Aceptar',
        cancelText = 'Cancelar',
        isDanger = true,
        onConfirm
    } = {}) {
        let overlay = document.getElementById('custom-confirm-modal');
        if (!overlay) {
            overlay = document.createElement('div');
            overlay.id = 'custom-confirm-modal';
            overlay.className = 'custom-modal-overlay';
            overlay.innerHTML = `
                <div class="custom-modal-box">
                    <div class="custom-modal-icon" id="custom-confirm-icon">⚠️</div>
                    <div class="custom-modal-title" id="custom-confirm-title"></div>
                    <div class="custom-modal-body" id="custom-confirm-message"></div>
                    <div class="custom-modal-actions">
                        <button type="button" class="custom-modal-btn btn-cancel" id="custom-confirm-btn-cancel"></button>
                        <button type="button" class="custom-modal-btn" id="custom-confirm-btn-ok"></button>
                    </div>
                </div>
            `;
            document.body.appendChild(overlay);
        }

        if (!document.getElementById('custom-modal-styles')) {
            const style = document.createElement('style');
            style.id = 'custom-modal-styles';
            style.textContent = `
                .custom-modal-overlay {
                    position: fixed;
                    top: 0; left: 0; width: 100vw; height: 100vh;
                    background: rgba(0, 0, 0, 0.65);
                    backdrop-filter: blur(4px);
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    z-index: 10000;
                    opacity: 0;
                    pointer-events: none;
                    transition: opacity 0.2s ease;
                }
                .custom-modal-overlay.show {
                    opacity: 1;
                    pointer-events: auto;
                }
                .custom-modal-box {
                    background: var(--card-bg, #1a1a1e);
                    border: 1px solid var(--border-color, #2d2d34);
                    color: var(--text-color, #e1e1e6);
                    border-radius: 12px;
                    padding: 24px;
                    width: 90%;
                    max-width: 440px;
                    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
                    transform: scale(0.95);
                    transition: transform 0.2s ease;
                    text-align: center;
                }
                body.light-theme .custom-modal-box {
                    background: #ffffff;
                    border-color: #e4e4e7;
                    color: #18181b;
                }
                .custom-modal-overlay.show .custom-modal-box {
                    transform: scale(1);
                }
                .custom-modal-icon {
                    font-size: 32px;
                    margin-bottom: 12px;
                }
                .custom-modal-title {
                    font-size: 18px;
                    font-weight: 700;
                    margin-bottom: 8px;
                    color: var(--text-color, #e1e1e6);
                }
                body.light-theme .custom-modal-title {
                    color: #18181b;
                }
                .custom-modal-body {
                    font-size: 14px;
                    color: var(--text-muted, #a8a8b2);
                    margin-bottom: 24px;
                    line-height: 1.5;
                    white-space: pre-line;
                }
                body.light-theme .custom-modal-body {
                    color: #71717a;
                }
                .custom-modal-actions {
                    display: flex;
                    gap: 12px;
                    justify-content: center;
                }
                .custom-modal-btn {
                    padding: 10px 20px;
                    font-weight: 600;
                    font-size: 14px;
                    border-radius: 8px;
                    cursor: pointer;
                    border: none;
                    transition: background-color 0.2s, transform 0.1s;
                }
                .custom-modal-actions .btn-cancel {
                    background: var(--stat-bg, #202024);
                    color: var(--text-color, #e1e1e6);
                    border: 1px solid var(--border-color, #2d2d34);
                }
                body.light-theme .custom-modal-actions .btn-cancel {
                    background: #f4f4f5;
                    color: #18181b;
                    border-color: #e4e4e7;
                }
                .custom-modal-actions .btn-cancel:hover {
                    background: var(--border-color, #2d2d34);
                }
                body.light-theme .custom-modal-actions .btn-cancel:hover {
                    background: #e4e4e7;
                }
                .custom-modal-actions .btn-ok-danger {
                    background: #e53935;
                    color: #ffffff;
                }
                .custom-modal-actions .btn-ok-danger:hover {
                    background: #c62828;
                }
                .custom-modal-actions .btn-ok-primary {
                    background: var(--brand-color, #4CAF50);
                    color: #ffffff;
                }
                .custom-modal-actions .btn-ok-primary:hover {
                    filter: brightness(1.1);
                }
                
                .toast-notification {
                    position: fixed;
                    bottom: 75px;
                    right: 24px;
                    background: #2a2a30;
                    color: #ffffff;
                    padding: 12px 20px;
                    border-radius: 8px;
                    border-left: 4px solid var(--brand-color, #4CAF50);
                    box-shadow: 0 4px 15px rgba(0,0,0,0.3);
                    z-index: 9999;
                    font-size: 14px;
                    opacity: 0;
                    transform: translateY(10px);
                    transition: opacity 0.3s ease, transform 0.3s ease;
                    pointer-events: none;
                }
                body.light-theme .toast-notification {
                    background: #ffffff;
                    color: #18181b;
                    box-shadow: 0 4px 15px rgba(0,0,0,0.1);
                }
                .toast-notification.show {
                    opacity: 1;
                    transform: translateY(0);
                }
                .toast-notification.error {
                    border-left-color: #f44336;
                }
            `;
            document.head.appendChild(style);
        }

        const titleEl = document.getElementById('custom-confirm-title');
        const msgEl = document.getElementById('custom-confirm-message');
        const cancelBtn = document.getElementById('custom-confirm-btn-cancel');
        const okBtn = document.getElementById('custom-confirm-btn-ok');
        const iconEl = document.getElementById('custom-confirm-icon');

        if (titleEl) titleEl.textContent = title;
        if (msgEl) msgEl.textContent = message;
        if (cancelBtn) cancelBtn.textContent = cancelText;
        if (okBtn) {
            okBtn.textContent = confirmText;
            okBtn.className = 'custom-modal-btn ' + (isDanger ? 'btn-ok-danger' : 'btn-ok-primary');
        }
        if (iconEl) iconEl.textContent = isDanger ? '🗑️' : '❓';

        function close() {
            overlay.classList.remove('show');
        }

        cancelBtn.onclick = () => { close(); };
        okBtn.onclick = () => { close(); if (onConfirm) onConfirm(); };
        overlay.onclick = (e) => { if (e.target === overlay) close(); };

        requestAnimationFrame(() => overlay.classList.add('show'));
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initFooter);
    } else {
        initFooter();
    }
})();
