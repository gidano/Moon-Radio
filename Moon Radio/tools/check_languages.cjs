// Run with Node.js. No radio or network connection required.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const languages = ['hu', 'en', 'de', 'pl'];
const script = fs.readFileSync(path.join(root, 'data/web/language.js'), 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));

// Audit the actual firmware dictionary, including escaped Unicode strings.
const dictionary = new Map();
for (const [, fields] of fs.readFileSync(path.join(root, 'src/i18n/Language.cpp'), 'utf8').matchAll(/^  \{(.*)\},?$/gm)) {
    const row = JSON.parse('[' + fields + ']');
    assert.equal(row.length, 4);
    assert(!dictionary.has(row[0]), 'Duplicate translation: ' + row[0]);
    assert(row.every(value => typeof value === 'string' && value.length));
    const placeholders = text => (text.match(/%(?:\d*\$)?[-+\d.]*[a-z]/gi) || []).sort().join(',');
    assert(row.every(value => placeholders(value) === placeholders(row[0])), 'Format placeholders: ' + row[0]);
    dictionary.set(row[0], row);
}
function auditFirmware(directory) {
    for (const entry of fs.readdirSync(directory, {withFileTypes:true})) {
        const filename = path.join(directory, entry.name);
        if (entry.isDirectory()) auditFirmware(filename);
        else if (entry.name.endsWith('.cpp')) {
            for (const [, literal] of fs.readFileSync(filename, 'utf8').matchAll(/Language::text\(("(?:[^"\\]|\\.)*")\)/g)) {
                assert(dictionary.has(JSON.parse(literal)), filename + ': missing translation ' + literal);
            }
        }
    }
}
auditFirmware(path.join(root, 'src'));
assert.deepEqual(dictionary.get('HANGERŐ'), ['HANGERŐ', 'VOLUME', 'LAUTSTÄRKE', 'GŁOŚNOŚĆ']);
assert.deepEqual(dictionary.get('KEDV'), ['KEDV', 'FAV', 'FAVOR', 'ULUB']);
assert.deepEqual(dictionary.get('MAGYAR'), ['MAGYAR', 'HUNGARY', 'UNGARN', 'WĘGRY']);
assert.deepEqual(dictionary.get('VEGYES'), ['VEGYES', 'MIXED', 'GEMISCHT', 'RÓŻNE']);

(async () => {
    for (const [language, expected] of [['hu','A fájlrendszer foglalt.'],['en','Filesystem is busy.'],['de','Dateisystem ist beschäftigt.'],['pl','System plików jest zajęty.']]) {
        const context = {window:{},document:{hidden:true,documentElement:{lang:language},addEventListener() {}},setInterval() {}};
        vm.runInNewContext(script, context);
        assert.equal(context.window.radioErrorText('filesystem busy'), expected);
        assert.equal(context.window.radioErrorText('Filesystem is busy.'), expected);
        assert(!context.window.radioErrorText('unrecognized technical error').includes('unrecognized'));
    }
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
        const testAnnouncement = settings.slice(settings.indexOf('        async function testClockTts()'), settings.indexOf('        function selectBackgroundPreset'));
        for (const voice of ['HU','EN','DE','PL','NL','FR','RO']) {
            let sent;
            const context = {
                document:{getElementById:id => id === 'clockTtsLanguage' ? {value:voice} : {}},
                fetch:async (url, options) => { sent = JSON.parse(options.body); return {ok:true}; },console
            };
            vm.createContext(context);
            vm.runInContext(testAnnouncement, context);
            await context.testClockTts();
            assert.equal(sent.language, voice);
            const expected = {HU:'Az idő',EN:'This is',DE:'Dies ist',PL:'To jest',NL:'Dit is',FR:'Ceci est',RO:'Acesta este'};
            assert(sent.text.startsWith(expected[voice]), `${language} page uses wrong test voice ${voice}`);
        }
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
    console.log(`OK: firmware dictionary coverage and format placeholders; ${pages} HTML pages; 48 language/page combinations; 8 save cases; 28 announcement language cases; localized errors and offline/invalid responses.`);
})().catch(error => { console.error(error); process.exitCode = 1; });
