// Run with Node.js. No radio or network connection required.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const languages = ['hu', 'en', 'de', 'pl'];
const script = fs.readFileSync(path.join(root, 'data/web/language.js'), 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));

(async () => {
    let pages = 0;
    for (const language of languages) {
        for (const family of ['index', 'settings', 'search']) {
            const html = fs.readFileSync(path.join(root, `data/web/${family}_${language}.html`), 'utf8');
            assert(html.includes('<script src="/web/language.js" defer></script>'));
            for (const [, code] of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) {
                new vm.Script(code, { filename: `${family}_${language}.html` });
            }
            ++pages;
            for (const target of languages) {
                let redirected;
                const context = {
                    window: {}, document: { hidden: false, documentElement: { lang: language }, addEventListener() {} },
                    location: { pathname: `/web/${family}_${language}.html`, search: '?a=1', hash: '#panel', replace(value) { redirected = value; } },
                    fetch: async () => ({ ok: true, json: async () => ({ language: target }) }),
                    setInterval() {}
                };
                vm.runInNewContext(script, context);
                await flush();
                assert.equal(redirected, target === language ? undefined : `/web/${family}_${target}.html?a=1#panel`);
            }
        }
        const settings = fs.readFileSync(path.join(root, `data/web/settings_${language}.html`), 'utf8');
        const change = settings.slice(settings.indexOf('        async function changeWebLanguage'), settings.indexOf('        function togglePanel'));
        for (const succeeds of [true, false]) {
            const selected = { value: language, disabled: false };
            let alertCount = 0;
            let posted;
            const context = {
                currentWebLanguage: language,
                webLanguagePaths: Object.fromEntries(languages.map(code => [code, `/web/settings_${code}.html`])),
                document: { getElementById: () => selected },
                window: { location: {} },
                fetch: async (url, options) => {
                    assert.equal(url, '/api/language');
                    assert.equal(options.method, 'POST');
                    posted = JSON.parse(options.body).language;
                    return { ok: succeeds, json: async () => ({ ok: succeeds }) };
                },
                alert() { ++alertCount; }
            };
            vm.createContext(context);
            vm.runInContext(change, context);
            const target = language === 'en' ? 'hu' : 'en';
            await context.changeWebLanguage(target);
            assert.equal(posted, target);
            if (succeeds) {
                assert.equal(context.window.location.href, `/web/settings_${target}.html`);
                assert.equal(alertCount, 0);
            } else {
                assert.equal(context.window.location.href, undefined);
                assert.equal(selected.value, language);
                assert.equal(selected.disabled, false);
                assert.equal(alertCount, 1);
            }
        }
    }
    // Offline/invalid server responses must leave the current page usable.
    for (const fetch of [async () => { throw Error('offline'); }, async () => ({ ok: false }), async () => ({ ok: true, json: async () => ({ language: 'xx' }) })]) {
        vm.runInNewContext(script, { window: {}, document: { hidden: false, documentElement: { lang: 'hu' }, addEventListener() {} }, fetch, setInterval() {}, location: { replace() { assert.fail('unexpected redirect'); } } });
        await flush();
    }
    console.log(`OK: all ${pages} HTML pages parse; 48 language/page combinations, 8 save success/failure cases, and offline/invalid responses pass.`);
})().catch(error => { console.error(error); process.exitCode = 1; });
