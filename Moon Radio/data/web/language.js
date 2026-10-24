// The radio stores the shared language; browser storage never overrides it.
(() => {
    async function syncLanguage() {
        if (document.hidden || window.radioLanguageChanging) return;
        try {
            const response = await fetch('/api/language', { cache: 'no-store' });
            if (!response.ok) return;
            const { language } = await response.json();
            if (!['hu', 'en', 'de', 'pl'].includes(language)) return;
            const pageLanguage = document.documentElement.lang;
            if (language === pageLanguage) return;
            const match = location.pathname.match(/(?:^|\/)(index|settings|search)(?:_[a-z]{2}\.html)?$/);
            const page = match ? match[1] : 'index';
            location.replace('/web/' + page + '_' + language + '.html' + location.search + location.hash);
        } catch (_) { /* Retry when the radio is reachable again. */ }
    }
    syncLanguage();
    setInterval(syncLanguage, 5000);
    document.addEventListener('visibilitychange', syncLanguage);
})();
