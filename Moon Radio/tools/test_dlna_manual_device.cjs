// Real-radio test: optional manual server, duplicate connection, failed connection,
// and the actual browser form. Writes reports/screenshots under build-dlna/.
// MOON_RADIO_URL, MOON_DLNA_IP and MOON_DLNA_PORT select local test devices.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_PATH || 'playwright');
const base = process.env.MOON_RADIO_URL || 'http://192.168.31.10';
const ip = process.env.MOON_DLNA_IP || '192.168.31.103';
const port = process.env.MOON_DLNA_PORT || '8000';
const pause = ms => new Promise(r=>setTimeout(r,ms));
async function request(path, data, expected) {
  const r = await fetch(base+path, data ? {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data).toString()} : {});
  if(expected)assert.equal(r.status,expected);else assert(r.ok,`${path}: ${r.status}`);
  return r.json();
}
async function settled() {
  for(let i=0;i<70;i++) {const s=await request('/api/dlna');if(!s.busy)return s;await pause(500);}
  throw Error('DLNA request timed out');
}
async function connect() {
  await request('/api/dlna/connect',{ip,port},202);
  const s=await settled();assert.equal(s.error,'');assert.equal(s.object,'0');assert(s.items.length>0);return s;
}
(async()=>{
  let browser;const report={ip,port};
  try {
    let root=await connect();report.server=root.heading;report.root=root;
    await request('/api/dlna/servers',{});const list=await settled();
    root=await connect();
    await request('/api/dlna/servers',{});assert.equal((await settled()).items.length,list.items.length,'reconnect must not duplicate servers');
    root=await connect();
    for(const data of [{ip:'999.1.2.3',port},{ip,port:'0'},{ip,port:'65536'},{ip,port:'8000junk'}]) await request('/api/dlna/connect',data,400);
    assert.equal((await settled()).revision,root.revision,'invalid requests must not replace the view');
    await request('/api/dlna/connect',{ip,port:'65534'},202);
    const failed=await settled();assert.equal(failed.error,'connect');assert.equal(failed.revision,root.revision);assert.deepEqual(failed.items,root.items);
    report.failurePreservesListing=true;root=await connect();
    browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
    const page=await browser.newPage({viewport:{width:390,height:900}});const errors=[];page.on('pageerror',e=>errors.push(String(e)));
    await page.goto(base+'/dlna');
    await page.locator('#manualIp').fill(ip);await page.locator('#manualPort').fill(port);
    await page.locator('#manualConnect').click();
    await page.waitForFunction(()=>!document.querySelector('#manualConnect').disabled);
    await page.getByRole('heading',{name:root.heading,exact:true}).waitFor();
    assert.equal(await page.locator('body').evaluate(e=>e.scrollWidth>innerWidth),false);
    const form=await page.locator('#manualForm').boundingBox(),intro=await page.locator('[data-t="intro"]').boundingBox();
    assert(form.y+form.height<=intro.y);
    await page.screenshot({path:'build-dlna/manual-backup/manual-device-mobile.png',fullPage:true});
    const music=root.items.find(x=>x.name==='Music'&&x.directory);
    if(music){
      await page.getByRole('button',{name:/Music$/}).click();
      await page.getByRole('button',{name:/I'll Be Missing You/}).waitFor();
      report.music=await settled();assert.equal(report.music.error,'');
    }
    await page.setViewportSize({width:1100,height:1000});
    await page.screenshot({path:'build-dlna/manual-backup/manual-device-desktop.png',fullPage:true});
    assert.equal(errors.length,0,errors.join('\n'));report.browserForm=true;
    fs.writeFileSync('build-dlna/manual-backup/device-test.json',JSON.stringify(report,null,2));
    console.log(JSON.stringify({server:report.server,duplicateAvoided:true,failurePreservesListing:true,browserForm:true,musicCount:report.music?.items.length}));
  }finally{if(browser)await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
