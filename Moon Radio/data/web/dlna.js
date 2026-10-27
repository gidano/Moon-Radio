'use strict';
(() => {
  const words = {
    HU: {home:'Vissza a rádióhoz',title:'Hálózati zene',intro:'PC vagy NAS megosztott zenetárának böngészése és lejátszása.',scan:'Szerverek keresése',radio:'Rádióforrás',up:'Vissza',previousPage:'Előző oldal',nextPage:'Következő oldal',previousTrack:'Előző szám',nextTrack:'Következő szám',pause:'Szünet / lejátszás',queueNote:'A számok az indításkor megnyitott oldalról követik egymást. Más mappa böngészése ezt nem módosítja.',memory:'Memória',busy:'Keresés vagy mappa betöltése…',empty:'Nincs megjeleníthető elem.',no_servers:'Nem találtam médiaszervert. Ellenőrizd a PC médiamegosztását, vagy próbáld újra a keresést.',browse:'A mappa nem tölthető be. Az előző lista megmaradt.',scanError:'A keresés nem sikerült. Próbáld újra.',memoryError:'Nincs elegendő memória, vagy a szerver válasza túl nagy.',requestError:'A művelet nem indítható. Várd meg a betöltést, vagy ellenőrizd a kapcsolatot és a szabad memóriát.',offline:'A rádió nem érhető el.',unsupported:'Nem támogatott hangformátum',serverList:'Médiaszerverek',used:'DLNA PSRAM használat',peak:'DLNA PSRAM csúcs',budget:'DLNA foglalási korlát',internal:'Szabad belső RAM',largest:'Legnagyobb belső szabad blokk',psram:'Szabad PSRAM',ready:'Válassz médiaszervert vagy zeneszámot.'},
    EN: {home:'Back to radio',title:'Network music',intro:'Browse and play a PC or NAS media library.',scan:'Find servers',radio:'Radio source',up:'Back',previousPage:'Previous page',nextPage:'Next page',previousTrack:'Previous track',nextTrack:'Next track',pause:'Pause / play',queueNote:'Tracks continue from the page open when playback started. Browsing another folder does not change this queue.',memory:'Memory',busy:'Finding servers or loading a folder…',empty:'No items to display.',no_servers:'No media server found. Check media sharing on the PC or scan again.',browse:'Cannot load folder. The previous list was kept.',scanError:'Scan failed. Please retry.',memoryError:'Not enough memory, or the server response is too large.',requestError:'Cannot start operation. Wait for loading, or check the connection and available memory.',offline:'The radio is unreachable.',unsupported:'Unsupported audio format',serverList:'Media servers',used:'DLNA PSRAM usage',peak:'DLNA PSRAM peak',budget:'DLNA allocation limit',internal:'Free internal RAM',largest:'Largest free internal block',psram:'Free PSRAM',ready:'Choose a media server or track.'},
    DE: {home:'Zurück zum Radio',title:'Netzwerkmusik',intro:'Musikbibliothek auf PC oder NAS durchsuchen und abspielen.',scan:'Server suchen',radio:'Radioquelle',up:'Zurück',previousPage:'Vorherige Seite',nextPage:'Nächste Seite',previousTrack:'Vorheriger Titel',nextTrack:'Nächster Titel',pause:'Pause / Wiedergabe',queueNote:'Die Titel der beim Start geöffneten Seite werden nacheinander gespielt. Andere Ordner ändern diese Liste nicht.',memory:'Speicher',busy:'Server suchen oder Ordner laden…',empty:'Keine Einträge.',no_servers:'Kein Medienserver gefunden. Medienfreigabe am PC prüfen oder erneut suchen.',browse:'Ordner konnte nicht geladen werden. Die vorherige Liste bleibt erhalten.',scanError:'Suche fehlgeschlagen. Erneut versuchen.',memoryError:'Nicht genug Speicher oder Serverantwort zu groß.',requestError:'Aktion nicht möglich. Laden abwarten oder Verbindung und freien Speicher prüfen.',offline:'Radio nicht erreichbar.',unsupported:'Audioformat nicht unterstützt',serverList:'Medienserver',used:'DLNA-PSRAM belegt',peak:'DLNA-PSRAM Höchstwert',budget:'DLNA-Speichergrenze',internal:'Interner RAM frei',largest:'Größter freier interner Block',psram:'PSRAM frei',ready:'Medienserver oder Titel auswählen.'},
    PL: {home:'Powrót do radia',title:'Muzyka sieciowa',intro:'Przeglądaj i odtwarzaj bibliotekę multimediów na komputerze lub NAS.',scan:'Szukaj serwerów',radio:'Źródło radiowe',up:'Wstecz',previousPage:'Poprzednia strona',nextPage:'Następna strona',previousTrack:'Poprzedni utwór',nextTrack:'Następny utwór',pause:'Pauza / odtwarzanie',queueNote:'Kolejne utwory pochodzą ze strony otwartej przy rozpoczęciu odtwarzania. Przeglądanie innych folderów nie zmienia kolejki.',memory:'Pamięć',busy:'Wyszukiwanie serwerów lub wczytywanie folderu…',empty:'Brak elementów.',no_servers:'Nie znaleziono serwera. Sprawdź udostępnianie multimediów na komputerze lub ponów wyszukiwanie.',browse:'Nie można wczytać folderu. Poprzednia lista została zachowana.',scanError:'Wyszukiwanie nie powiodło się. Spróbuj ponownie.',memoryError:'Za mało pamięci lub odpowiedź serwera jest zbyt duża.',requestError:'Nie można rozpocząć operacji. Poczekaj na wczytanie lub sprawdź połączenie i wolną pamięć.',offline:'Radio jest niedostępne.',unsupported:'Nieobsługiwany format audio',serverList:'Serwery multimediów',used:'Użycie PSRAM przez DLNA',peak:'Szczyt użycia PSRAM przez DLNA',budget:'Limit pamięci DLNA',internal:'Wolny wewnętrzny RAM',largest:'Największy wolny blok wewnętrzny',psram:'Wolny PSRAM',ready:'Wybierz serwer lub utwór.'}
  };
  window.mountDlnaBrowser = (root, embedded = false) => {
  let w = words.HU, state = null, path = [], pending = null, submitting = false, refreshing = false, rendered = '', notice = '';
  let disposed = false, timer = null;
  const el = id => root.querySelector('#' + id);
  if (embedded) root.querySelectorAll('[data-dlna-standalone]').forEach(node => { node.hidden = true; });
  const message = () => {
    const error = state?.error;
    el('message').textContent = notice || (submitting || state?.busy ? w.busy : error ?
      ({memory:w.memoryError,scan:w.scanError,browse:w.browse,no_servers:w.no_servers}[error] || w.requestError) : state?.items?.length ? w.ready : w.empty);
  };
  function render() {
    if (disposed) return;
    const busy = submitting || !!state?.busy;
    el('scan').disabled = busy;
    el('up').disabled = busy || !state || state.servers;
    el('previousPage').disabled = busy || !state || state.servers || !state.offset;
    el('nextPage').disabled = busy || !state?.more;
    el('heading').textContent = state?.servers ? w.serverList : state?.heading || '';
    el('page').textContent = state && !state.servers ? `${state.offset + 1}–${state.offset + state.items.length}` : '';
    const key = `${state?.revision}:${busy}:${w.title}`;
    if (key !== rendered) {
      rendered = key; el('items').replaceChildren();
      (state?.items || []).forEach((item, index) => {
        const li = document.createElement('li'), button = document.createElement('button');
        const icon = document.createElement('span'), copy = document.createElement('span');
        icon.textContent = state.servers ? '▣' : item.directory ? '📁' : '♪';
        copy.textContent = item.name;
        if (item.artist || (!state.servers && !item.directory && !item.playable)) {
          const small = document.createElement('small'); small.textContent = item.artist || w.unsupported; copy.append(small);
        }
        button.append(icon, copy); button.disabled = busy || (!state.servers && !item.directory && !item.playable);
        button.onclick = () => {
          if (state.servers) navigate({server:index,id:'0',title:item.name,offset:0}, []);
          else if (item.directory) navigate({server:state.server,id:item.id,title:item.name,offset:0}, path);
          else post('/api/dlna/play', {index,revision:state.revision});
        };
        li.append(button); el('items').append(li);
      });
    }
    el('memory').replaceChildren();
    for (const [key, label] of [['dlnaPsram',w.used],['dlnaPeak',w.peak],['budget',w.budget],['internalFree',w.internal],['internalLargest',w.largest],['psramFree',w.psram]]) {
      const dt = document.createElement('dt'), dd = document.createElement('dd');
      dt.textContent = label; dd.textContent = state?.[key] == null ? '—' : `${(state[key] / 1024).toFixed(1)} KiB`; el('memory').append(dt, dd);
    }
    message();
  }
  async function post(url, values = {}) {
    if (submitting || disposed) return false;
    submitting = true; notice = ''; render();
    let ok = false;
    try {
      const response = await fetch(url, {method:'POST',body:new URLSearchParams(values)});
      ok = response.ok;
      if (!ok) { notice = w.requestError; pending = null; }
    } catch { notice = w.offline; pending = null; }
    finally { submitting = false; await refresh(); }
    return ok;
  }
  async function navigate(target, ancestors) {
    pending = {revision:state.revision,path:[...ancestors,target]};
    if (!await post('/api/dlna/browse', {...target,revision:state.revision})) pending = null;
  }
  async function refresh() {
    if (refreshing || disposed) return;
    refreshing = true;
    try {
      const response = await fetch('/api/dlna', {cache:'no-store'});
      if (!response.ok) throw new Error();
      const next = await response.json();
      if (!Array.isArray(next.items)) throw new Error();
      if (notice === w.offline) notice = '';
      state = next;
      if (!state.busy && pending && state.revision !== pending.revision) {
        if (!state.error) path = pending.path;
        pending = null;
      } else if (!state.busy && state.error) pending = null;
      if (state.servers) path = [];
      if (!embedded) {
      const status = await fetch('/api/status', {cache:'no-store'});
      if (status.ok) {
        const audio = await status.json();
        el('now').textContent = [audio.stationName,audio.title].filter(Boolean).join(' · ') || '—';
        for (const id of ['previousTrack','pause','nextTrack']) el(id).disabled = audio.source !== 'dlna';
      }
      }
    } catch { notice = w.offline; }
    finally { refreshing = false; render(); }
  }
  el('scan').onclick = async () => { pending = null; await post('/api/dlna/scan'); };
  el('radio').onclick = () => post('/api/source', {source:'radio'});
  el('up').onclick = () => {
    if (path.length <= 1) { pending = null; post('/api/dlna/servers'); return; }
    const target = path[path.length - 2]; navigate(target, path.slice(0,-2));
  };
  for (const [id, direction] of [['previousPage',-1],['nextPage',1]]) el(id).onclick = () => {
    const current = path[path.length - 1] || {server:state.server,id:state.object,title:state.heading};
    const history = [...(current.previousOffsets || [])];
    const offset = direction > 0 ? state.nextOffset : history.length ? history.pop() : 0;
    if (direction > 0) history.push(state.offset);
    navigate({...current,offset,previousOffsets:history}, path.slice(0,-1));
  };
  el('previousTrack').onclick = () => post('/api/track_prev');
  el('nextTrack').onclick = () => post('/api/track_next');
  el('pause').onclick = () => post('/api/toggle');
  const onVisibility = () => { if (!document.hidden) refresh(); };
  const ready = (async () => {
    try { const r = await fetch('/api/language'); const {language} = await r.json(); w = words[String(language).toUpperCase()] || words.HU; document.documentElement.lang = (language || 'HU').toLowerCase(); } catch {}
    if (disposed) return;
    root.querySelectorAll('[data-t]').forEach(node => { node.textContent = w[node.dataset.t]; });
    if (!embedded) document.title = `Moon Radio · ${w.title}`;
    await refresh();
    if (disposed) return;
    timer = setInterval(onVisibility, 1200);
    document.addEventListener('visibilitychange', onVisibility);
  })();
  return {ready, destroy() { disposed = true; clearInterval(timer); document.removeEventListener('visibilitychange', onVisibility); }};
  };
  const root = document.getElementById('dlna-browser');
  if (root) window.mountDlnaBrowser(root);
})();
