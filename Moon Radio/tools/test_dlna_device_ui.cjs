// Smoke test against the real radio; restores radio source after browsing.
const {chromium} = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const fs = require('node:fs');
const assert = require('node:assert/strict');
const base = process.env.MOON_RADIO_URL || 'http://192.168.31.10';
const pause = ms => new Promise(r => setTimeout(r, ms));
async function api(path, data) {
  const r = await fetch(base + path, data ? {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data).toString()} : {});
  assert(r.ok, `${path}: HTTP ${r.status}`); return r.json();
}
async function settled() {
  for(let i=0;i<40;i++) { const s=await api('/api/dlna'); if(!s.busy)return s; await pause(500); }
  throw Error('DLNA operation timed out');
}
(async()=>{
  let browser;
  const report = {};
  try {
    await api('/api/dlna/scan', {});
    let state = await settled();
    assert(!state.error, state.error);
    assert(state.items.length > 0, 'No media servers');
    report.servers = state.items.map(i=>i.name);
    await api('/api/dlna/browse',{server:'0',id:'0',title:state.items[0].name,offset:'0',revision:String(state.revision)});
    state = await settled(); assert(!state.error,state.error); assert(state.items.length > 0);
    report.rootEntries = state.items.map(i=>i.name);
    browser = await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
    const page=await browser.newPage({viewport:{width:1100,height:1000}});
    const errors=[];page.on('pageerror',e=>errors.push(String(e)));
    await page.goto(base+'/');
    await page.locator('#dlna-browser #items button').first().waitFor();
    assert.equal(await page.locator('#list').isVisible(),false);
    assert.equal(await page.getByRole('link',{name:'DLNA',exact:true}).count(),1);
    assert.equal(await page.locator('#newName').isVisible(),false);
    await page.screenshot({path:'build-dlna/device-dlna-ui.png',fullPage:true});
    report.embeddedBrowser = true;
    report.status = await api('/api/status');
    assert.equal(report.status.logoSource, 'music_server');
    assert.equal(errors.length,0,errors.join('\n'));
    await page.getByRole('button',{name:'📻 Rádió',exact:true}).click();
    await page.locator('#newName').waitFor({state:'visible'});
    assert.equal(await page.locator('#list').isVisible(),true);
    report.radioReturn = true;
    fs.writeFileSync('build-dlna/device-ui-test.json',JSON.stringify(report,null,2));
    console.log(JSON.stringify({servers:report.servers,rootEntries:report.rootEntries,embeddedBrowser:true,radioReturn:true}));
  } finally {
    if(browser)await browser.close();
    await api('/api/source',{source:'radio'});
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
