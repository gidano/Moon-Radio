(() => {
    const labels = {
        hu: {radio:'📻 Rádió', sd:'▣ SD-kártya', back:'↑ Szülőkönyvtár', refresh:'↻ Újraolvasás', more:'További fájlok', empty:'Nincs zenei fájl vagy könyvtár.', error:'Az SD-kártya nem olvasható. Ellenőrizd a kártyát, majd próbáld újra.', playError:'A fájl nem játszható le.', loading:'SD beolvasása…', search:'🔍 SD-fájlok keresése…'},
        en: {radio:'📻 Radio', sd:'▣ SD card', back:'↑ Parent folder', refresh:'↻ Refresh', more:'More files', empty:'No music files or folders.', error:'Cannot read the SD card. Check the card and try again.', playError:'Cannot play this file.', loading:'Reading SD…', search:'🔍 Search SD files…'},
        de: {radio:'📻 Radio', sd:'▣ SD-Karte', back:'↑ Übergeordneter Ordner', refresh:'↻ Aktualisieren', more:'Weitere Dateien', empty:'Keine Musikdateien oder Ordner.', error:'SD-Karte nicht lesbar. Karte prüfen und erneut versuchen.', playError:'Datei kann nicht abgespielt werden.', loading:'SD wird gelesen…', search:'🔍 SD-Dateien suchen…'},
        pl: {radio:'📻 Radio', sd:'▣ Karta SD', back:'↑ Folder nadrzędny', refresh:'↻ Odśwież', more:'Więcej plików', empty:'Brak plików muzycznych lub folderów.', error:'Nie można odczytać karty SD. Sprawdź kartę i spróbuj ponownie.', playError:'Nie można odtworzyć pliku.', loading:'Odczyt SD…', search:'🔍 Szukaj plików SD…'}
    };
    const t = labels[document.documentElement.lang] || labels.en;
    const main = !!document.getElementById('list');
    const bar = document.createElement('div');
    bar.style.cssText = 'display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;max-width:520px;margin:12px auto';
    const button = (text, action) => {
        const element = document.createElement('button');
        element.type = 'button';
        element.textContent = text;
        element.className = 'control-btn';
        element.style.cssText = 'width:auto;height:auto;min-height:42px;min-width:0;padding:8px;font-size:clamp(14px,3.8vw,20px);white-space:nowrap;border-radius:12px';
        element.onclick = action;
        return element;
    };
    const heading = document.querySelector('h1');
    if (!heading) return;
    heading.after(bar);
    let mode, directory = '/', entries = [], more = false, nextOffset = 0, generation = 0, currentPath = '';
    const radio = button(t.radio, () => choose('radio'));
    const sd = button(t.sd, () => choose('sd'));
    const dlna = document.createElement('a');
    dlna.href = '/dlna'; dlna.textContent = 'DLNA'; dlna.className = 'control-btn';
    dlna.style.cssText = 'width:auto;height:auto;min-height:42px;min-width:0;padding:8px;font-size:clamp(14px,3.8vw,20px);white-space:nowrap;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;box-sizing:border-box;text-decoration:none;color:inherit';
    bar.append(radio, sd, dlna);
    const browser = document.createElement('div');
    browser.hidden = true;
    if (main) document.getElementById('list').before(browser);
    const dlnaHost = document.createElement('div');
    dlnaHost.hidden = true;
    if (main) document.getElementById('list').before(dlnaHost);
    let dlnaComponent = null;
    const search = document.getElementById('search');
    const searchPlaceholder = search?.placeholder;
    const addForm = document.getElementById('newName')?.parentElement;
    const trackLabel = document.querySelector('.pc-stream-label');
    const originalTrackLabel = trackLabel?.textContent;
    async function mountDlna() {
        const request = generation;
        try {
            const response = await fetch('/dlna', {cache:'no-store'});
            if (!response.ok) throw new Error('DLNA page unavailable');
            const html = await response.text();
            if (mode !== 'dlna' || request !== generation) return;
            const template = new DOMParser().parseFromString(html, 'text/html').getElementById('dlna-browser');
            if (!template || !window.mountDlnaBrowser) throw new Error('DLNA browser unavailable');
            dlnaHost.replaceChildren(document.importNode(template, true));
            dlnaComponent = window.mountDlnaBrowser(dlnaHost.firstElementChild, true);
        } catch (_) {
            if (mode === 'dlna' && request === generation) message(t.playError);
        }
    }
    const message = text => { if (main) document.getElementById('status').textContent = text; else alert(text); };
    async function post(url, fields) {
        const response = await fetch(url, {method:'POST', body:new URLSearchParams(fields)});
        if (!response.ok) throw new Error('Request failed');
    }
    async function choose(source) {
        radio.disabled = sd.disabled = true;
        try {
            await post('/api/source', {source});
            if (!main) { location.href = '/'; return; }
            const response = await fetch('/api/status', {cache:'no-store'});
            if (!response.ok) throw new Error('Status failed');
            const state = await response.json();
            const previousMode = mode;
            window.updateSdState(state);
            if (source === 'sd' && previousMode === 'sd') await load(directory, false);
            window.fetchStatus?.();
        } catch (_) { message(t.error); }
        finally { radio.disabled = sd.disabled = false; }
    }
    function render() {
        if (mode !== 'sd') return;
        browser.replaceChildren();
        const controls = document.createElement('div');
        controls.style.cssText = 'display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:12px 0';
        const path = document.createElement('span');
        path.textContent = '▣ ' + directory;
        path.style.overflowWrap = 'anywhere';
        controls.append(button(t.back, () => load(directory.slice(0, directory.lastIndexOf('/')) || '/')), button(t.refresh, () => load(directory)), path);
        browser.append(controls);
        const list = document.getElementById('list');
        list.replaceChildren();
        const query = (search?.value || '').toLocaleLowerCase();
        entries.filter(e => e.name.toLocaleLowerCase().includes(query))
            .sort((a,b) => Number(b.directory)-Number(a.directory) || (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
            .forEach(entry => {
                const row = button((entry.directory ? '📁 ' : '♪ ') + entry.name, async () => {
                    if (entry.directory) return load(entry.path);
                    row.disabled = true;
                    try {
                        await post('/api/sd/play', {path:entry.path});
                        currentPath = entry.path;
                        render();
                        window.fetchStatus?.();
                    } catch (_) { message(t.playError); }
                    finally { row.disabled = false; }
                });
                row.className = 'station-btn';
                row.style.cssText = 'width:100%;text-align:left;overflow-wrap:anywhere';
                row.classList.toggle('active', entry.path === currentPath);
                list.append(row);
            });
        if (!entries.length) message(t.empty);
        if (more) browser.append(button(t.more, () => load(directory, true)));
    }
    async function load(path, append = false) {
        const request = ++generation;
        message(t.loading);
        try {
            const response = await fetch('/api/sd/list?' + new URLSearchParams({path, offset:append ? nextOffset : 0}), {cache:'no-store'});
            if (!response.ok) throw new Error('SD unavailable');
            const data = await response.json();
            if (request !== generation || mode !== 'sd') return;
            directory = data.path;
            entries = append ? entries.concat(data.entries) : data.entries;
            more = data.more;
            nextOffset = data.nextOffset;
            message(t.sd + ' · ' + entries.length + (more ? '+' : ''));
            render();
        } catch (_) { if (request === generation && mode === 'sd') message(t.error); }
    }
    window.updateSdState = state => {
        const changed = mode !== state.source;
        mode = state.source || 'radio';
        window.moonSdMode = mode === 'sd';
        window.moonDlnaMode = mode === 'dlna';
        radio.setAttribute('aria-pressed', String(mode === 'radio'));
        sd.setAttribute('aria-pressed', String(mode === 'sd'));
        if (mode === 'dlna') dlna.setAttribute('aria-current','page'); else dlna.removeAttribute('aria-current');
        radio.style.outline = mode === 'radio' ? '2px solid #7fd' : '';
        sd.style.outline = mode === 'sd' ? '2px solid #7fd' : '';
        dlna.style.outline = mode === 'dlna' ? '2px solid #7fd' : '';
        if (!main) return;
        browser.hidden = mode !== 'sd';
        dlnaHost.hidden = mode !== 'dlna';
        document.getElementById('list').hidden = mode === 'dlna';
        document.getElementById('status').hidden = mode === 'dlna';
        if (search) search.hidden = mode === 'dlna';
        if (trackLabel) trackLabel.textContent = mode === 'dlna' ? 'DLNA' : mode === 'sd' ? 'SD' : originalTrackLabel;
        if (addForm) addForm.style.display = mode !== 'radio' ? 'none' : '';
        if (search) search.placeholder = mode === 'sd' ? t.search : searchPlaceholder;
        const trackChanged = currentPath !== state.url;
        currentPath = state.url || '';
        if (changed) {
            ++generation;
            dlnaComponent?.destroy(); dlnaComponent = null;
            dlnaHost.replaceChildren();
            if (search) search.value = '';
            if (mode === 'sd') {
                document.getElementById('list').replaceChildren();
                load('/');
            } else if (mode === 'dlna') mountDlna();
            else window.fetchStations?.();
        } else if (mode === 'sd' && trackChanged) render();
    };
    if (search) search.addEventListener('input', () => { if (mode === 'sd') render(); });
    fetch('/api/status', {cache:'no-store'}).then(r => r.json()).then(window.updateSdState).catch(() => {});
})();

