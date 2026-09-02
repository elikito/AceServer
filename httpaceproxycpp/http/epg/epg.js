/**
 * HTTPAceProxy — EPG Channel Filters & Customization Helper (v09.02.01)
 */
(function(window) {
    'use strict';

    const ChannelFilters = {
        async getFilter(slug) {
            const res = await fetch(`/api/channel_filters?slug=${encodeURIComponent(slug)}`);
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            return await res.json();
        },

        async saveFilter(slug, rules) {
            const patterns = Array.isArray(rules) ? rules : [rules];
            const res = await fetch('/api/channel_filters', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ slug, rules: patterns })
            });
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            return await res.json();
        },

        async deleteFilter(slug) {
            const res = await fetch(`/api/channel_filters?slug=${encodeURIComponent(slug)}&action=delete`, {
                method: 'POST'
            });
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            return await res.json();
        }
    };

    window.AceChannelFilters = ChannelFilters;
})(window);
