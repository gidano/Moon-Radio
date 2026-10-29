// Run with: node --test tools/test_dlna_web.cjs
// Exercises the real page script against a simulated radio API, without
// requiring hardware or changing the user's media sharing settings.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const source = fs.readFileSync('data/web/dlna.js', 'utf8');

class Element {
  constructor() { this.children = []; this.textContent = ''; this.disabled = false; this.dataset = {}; }
  append(...nodes) { this.children.push(...nodes); }
  replaceChildren(...nodes) { this.children = nodes; }
  click() { return this.disabled ? undefined : this.onclick?.(); }
}
async function settle() { for (let i = 0; i < 12; ++i) await new Promise(resolve => setImmediate(resolve)); }
async function radio(language = 'hu') {
  const nodes = new Map();
  const get = id => { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); };
  const translated = ['title','scan','up','queueNote'].map(key => { const n = new Element(); n.dataset.t = key; return n; });
  const root = get('dlna-browser');
  root.querySelector = selector => get(selector.slice(1));
  root.querySelectorAll = selector => selector === '[data-t]' ? translated : [];
  let state = {busy:false,revision:0,servers:true,items:[],error:''};
  let status = {source:'radio',stationName:'Test radio',title:''};
  let tick;
  const calls = [];
  const document = {getElementById:get,createElement:() => new Element(),querySelectorAll:() => translated,documentElement:{},hidden:false,addEventListener:() => {},removeEventListener:() => {}};
  const fetch = async (url, options = {}) => {
    const args = Object.fromEntries(options.body || []);
    calls.push({url,args});
    if (url === '/api/language') return {ok:true,json:async () => ({language})};
    if (url === '/api/status') return {ok:true,json:async () => structuredClone(status)};
    if (url === '/api/dlna') return {ok:true,json:async () => structuredClone(state)};
    if (url === '/api/dlna/scan' || url === '/api/dlna/browse' || url === '/api/dlna/connect') state.busy = true;
    if (url === '/api/source') status.source = args.source;
    return {ok:true,json:async () => ({ok:true})};
  };
  const window = {};
  vm.runInNewContext(source, {window,document,fetch,URLSearchParams,setInterval:fn => {tick = fn;},clearInterval:()=>{},console});
  await settle();
  return {get,calls,document,translated,
    setState:s => {state = s;},
    setStatus:s => {status = s;},
    refresh:async () => {tick(); await settle();},
    click:async id => {await get(id).click(); await settle();},
    connect:async (ip,port) => {get('manualIp').value=ip;get('manualPort').value=port;await get('manualForm').onsubmit({preventDefault(){}});await settle();},
    clickItem:async i => {get('items').children[i].children[0].click(); await settle();}
  };
}
const page = (revision, offset, more, items, nextOffset = offset + 32) => ({busy:false,revision,servers:false,server:0,object:'music',heading:'Music',offset,nextOffset,more,error:'',items});
const folder = {name:'Music',id:'music',directory:true,playable:false};
const song = {name:'Árvíztűrő <song>',id:'track1',artist:'Artist & Friends',directory:false,playable:true};

test('all four lowercase radio languages select the correct translation', async () => {
  for (const [lang, title] of [['hu','Hálózati zene'],['en','Network music'],['de','Netzwerkmusik'],['pl','Muzyka sieciowa']]) {
    const r = await radio(lang);
    assert.equal(r.translated[0].textContent, title);
    assert.equal(r.document.documentElement.lang, lang);
  }
});

test('discovery, server selection, folder navigation, track playback and controls', async () => {
  const r = await radio();
  await r.click('scan');
  assert.equal(r.calls.at(-3)?.url === '/api/dlna/scan' || r.calls.some(c => c.url === '/api/dlna/scan'), true);
  assert.equal(r.get('scan').disabled, true);
  r.setState({busy:false,revision:1,servers:true,items:[{name:'Windows Media Library'}],error:''});
  await r.refresh(); await r.clickItem(0);
  const rootRequest = r.calls.find(c => c.url === '/api/dlna/browse');
  assert.deepEqual(rootRequest.args, {server:'0',id:'0',title:'Windows Media Library',offset:'0',revision:'1'});
  r.setState({...page(2,0,false,[folder]),object:'0',heading:'Windows Media Library'});
  await r.refresh(); await r.clickItem(0);
  assert.equal(r.calls.filter(c => c.url === '/api/dlna/browse').at(-1).args.id,'music');
  r.setState(page(3,0,false,[song]));
  r.setStatus({source:'dlna',stationName:'DLNA',title:song.name});
  await r.refresh();
  assert.equal(r.get('items').children[0].children[0].children[1].textContent,song.name);
  await r.clickItem(0);
  assert.deepEqual(r.calls.find(c => c.url === '/api/dlna/play').args,{index:'0',revision:'3'});
  await r.click('previousTrack'); await r.click('nextTrack'); await r.click('pause');
  assert(r.calls.some(c => c.url === '/api/track_prev'));
  assert(r.calls.some(c => c.url === '/api/track_next'));
  assert(r.calls.some(c => c.url === '/api/toggle'));
  await r.click('up');
  assert.equal(r.calls.filter(c => c.url === '/api/dlna/browse').at(-1).args.id,'0');
});

test('short server pages keep exact offsets on back/forward navigation', async () => {
  const r = await radio('en');
  r.setState(page(1,0,true,[song],8)); await r.refresh();
  await r.click('nextPage');
  assert.equal(r.calls.filter(c => c.url === '/api/dlna/browse').at(-1).args.offset,'8');
  r.setState(page(2,8,true,[song],16)); await r.refresh();
  await r.click('nextPage');
  r.setState(page(3,16,true,[song],24)); await r.refresh();
  await r.click('previousPage');
  assert.equal(r.calls.filter(c => c.url === '/api/dlna/browse').at(-1).args.offset,'8');
});

test('unsupported resources cannot be played and folder failures remain visible', async () => {
  const r = await radio('en');
  r.setState(page(1,0,false,[{...song,playable:false}])); await r.refresh();
  assert.equal(r.get('items').children[0].children[0].disabled,true);
  r.setState({...page(1,0,false,[folder]),error:'browse'}); await r.refresh();
  assert.match(r.get('message').textContent,/previous list/);
  assert.equal(r.get('items').children.length,1);
});

test('manual connection validates address and port, disables controls while busy, and retains failed listings', async () => {
  const r = await radio('en');
  for (const [ip,port] of [['256.1.2.3','8000'],['192.168.1.2','0'],['192.168.1.2','65536'],['radio.local','8000'],['224.1.2.3','8000']]) {
    await r.connect(ip,port);
    assert(!r.calls.some(c=>c.url==='/api/dlna/connect'));
    assert.match(r.get('message').textContent,/valid IP/);
  }
  r.setState(page(3,0,false,[folder])); await r.refresh();
  await r.connect(' 192.168.31.103 ','8000');
  assert.deepEqual(r.calls.find(c=>c.url==='/api/dlna/connect').args,{ip:'192.168.31.103',port:'8000'});
  assert.equal(r.get('manualConnect').disabled,true);
  assert.equal(r.get('manualIp').disabled,true);
  r.setState({...page(3,0,false,[folder]),error:'connect'}); await r.refresh();
  assert.equal(r.get('manualConnect').disabled,false);
  assert.match(r.get('message').textContent,/DLNA library/);
  assert.equal(r.get('items').children.length,1);
});
