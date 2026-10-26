// Browser integration checks with a simulated radio; no device is modified.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');

(async () => {
    const browser = await chromium.launch({channel:'msedge', headless:true});
    try {
        for (const language of ['hu', 'en', 'de', 'pl']) {
            const page = await browser.newPage({viewport:{width:520, height:900}});
            let source = 'radio', playing = '', missing = false, badFile = false;
            const errors = [];
            page.on('pageerror', error => errors.push(error.message));
            await page.route('http://moon.test/**', async route => {
                const url = new URL(route.request().url());
                const fields = new URLSearchParams(route.request().postData() || '');
                const json = (data, status = 200) => route.fulfill({status, contentType:'application/json', body:JSON.stringify(data)});
                const status = {source, url:playing, currentIndex:0, volume:8, paused:false,
                    stationName:source === 'sd' ? 'SD' : 'Test Radio', title:playing,
                    codec:'MP3', bitrate:128, logoPath:'/logos/sd_card.png', logoSource:'sd_card'};
                if (url.pathname === '/api/status') return json(status);
                if (url.pathname === '/api/stations') return json({...status, stations:[{name:'Test Radio', url:'http://radio.test', homepage:''}]});
                if (url.pathname === '/api/source') {
                    if (missing && fields.get('source') === 'sd') return json({ok:false}, 400);
                    source = fields.get('source'); return json({ok:true});
                }
                if (url.pathname === '/api/sd/play') {
                    if (badFile) return json({ok:false}, 400);
                    playing = fields.get('path'); return json({ok:true});
                }
                if (url.pathname === '/api/sd/list') {
                    if (missing) return json({error:'SD unavailable'}, 503);
                    const folder = url.searchParams.get('path');
                    const offset = Number(url.searchParams.get('offset'));
                    const entries = folder === '/' ? [{name:'Zene', path:'/Zene', directory:true}]
                        : folder === '/Zene' ? (offset ? [{name:'02.flac', path:'/Zene/02.flac', directory:false}]
                            : [{name:'01 Árvíz & zene.mp3', path:'/Zene/01 Árvíz & zene.mp3', directory:false}]) : [];
                    return json({path:folder, entries, more:folder === '/Zene' && !offset, nextOffset:offset + entries.length});
                }
                if (url.pathname === '/api/language') return json({language});
                if (url.pathname.startsWith('/api/')) return json({value:200, percent:75});
                let file = url.pathname === '/' ? `data/web/index_${language}.html` : 'data' + url.pathname;
                file = path.resolve(root, file);
                if (!file.startsWith(root + path.sep) || !fs.existsSync(file)) return route.fulfill({status:404, body:''});
                const ext = path.extname(file);
                return route.fulfill({body:fs.readFileSync(file), contentType:ext === '.html' ? 'text/html; charset=utf-8' : ext === '.js' ? 'application/javascript' : ext === '.css' ? 'text/css' : 'image/png'});
            });
            await page.goto('http://moon.test/');
            await page.waitForFunction(() => window.moonSdMode === false);
            const badge = page.locator('button').filter({hasText:/^▣ /});
            missing = true;
            await badge.click();
            await page.waitForFunction(() => document.querySelector('#status').textContent.includes('SD'));
            assert.equal(await page.evaluate(() => window.moonSdMode), false);
            missing = false;
            await badge.click();
            await page.locator('#list button', {hasText:'Zene'}).click();
            await page.locator('#list button', {hasText:'01 Árvíz & zene.mp3'}).waitFor();
            assert.equal(await page.locator('#newName').isVisible(), false);
            // The existing radio search handler must not process SD entries.
            await page.locator('#search').fill('árvíz');
            await page.locator('#search').press('z');
            await page.locator('#search').fill('');
            badFile = true;
            await page.locator('#list button', {hasText:'01 Árvíz & zene.mp3'}).click();
            await page.waitForFunction(() => document.querySelector('#status').textContent.endsWith('.'));
            assert.equal(playing, '');
            badFile = false;
            await page.locator('#list button', {hasText:'01 Árvíz & zene.mp3'}).click();
            await page.waitForFunction(() => document.querySelector('#list .active'));
            assert.equal(playing, '/Zene/01 Árvíz & zene.mp3');
            // Pagination keeps already loaded files.
            await page.locator('button').filter({hasText:/^(További fájlok|More files|Weitere Dateien|Więcej plików)$/}).click();
            await page.locator('#list button', {hasText:'02.flac'}).waitFor();
            assert.equal(await page.locator('#list button').count(), 2);
            // A source change from another client must restore the radio list.
            source = 'radio';
            await page.evaluate(() => window.fetchStatus());
            await page.locator('#list button.station-btn', {hasText:'Test Radio'}).waitFor();
            assert.equal(await page.locator('#newName').isVisible(), true);
            assert.deepEqual(errors, []);
            // Both secondary pages expose the SD switch and navigate home.
            for (const family of ['settings', 'search']) {
                await page.goto(`http://moon.test/web/${family}_${language}.html`);
                await page.locator('button').filter({hasText:/^▣ /}).click();
                await page.waitForURL('http://moon.test/');
                await page.waitForFunction(() => window.moonSdMode === true);
                source = 'radio';
            }
            await page.close();
        }
        console.log('OK: SD switching, missing card/file errors, folders, Unicode names, search, pagination, cross-client state and all 12 localized pages.');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
