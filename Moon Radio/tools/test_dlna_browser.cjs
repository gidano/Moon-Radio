// Optional rendered UI smoke test: requires Playwright and Edge on Windows.
// Uses local simulated responses; never connects to a real radio/media server.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_PATH || 'playwright');

(async () => {
  let state = {busy:false,revision:0,servers:true,items:[],error:''};
  let audio = {source:'radio',stationName:'Moon Radio',title:'',volume:3};
  const requests = [], errors = [];
  const server = http.createServer(async (req,res) => {
    let body = ''; for await (const chunk of req) body += chunk;
    const args = Object.fromEntries(new URLSearchParams(body));
    requests.push({url:req.url,args});
    let result;
    if (req.url === '/api/language') result = {language:'hu'};
    else if (req.url === '/api/stations') result = {stations:[{name:'Test radio',url:'http://test/radio',logo:'nologo'}],currentIndex:0,volume:3,paused:false,stationName:'Test radio',title:'Test'};
    else if (req.url === '/api/buffer') result = {percent:70,filled:7000,size:10000};
    else if (req.url === '/api/brightness') result = {value:204};
    else if (req.url === '/api/source') { audio.source = args.source; if(args.source==='radio') Object.assign(audio,{stationName:'Moon Radio',title:''}); result = {ok:true}; }
    else if (req.url === '/api/status') result = audio;
    else if (req.url === '/api/dlna') result = state;
    else if (req.url === '/api/dlna/connect') {
      state = args.port === '65534' ? {...state,error:'connect'} :
        {busy:false,revision:state.revision+1,servers:false,server:0,object:'0',heading:'Manual NAS',offset:0,more:false,error:'',items:[{name:'Zene / Albumok',id:'music',directory:true}]};
      audio.source='dlna';result={ok:true};
    }
    else if (req.url === '/api/dlna/scan') {
      state = {busy:false,revision:1,servers:true,items:[{name:'Windows Media Player · PC'},{name:'VAULT - Luna Veil'}],error:''}; audio.source = 'dlna'; result = {ok:true};
    } else if (req.url === '/api/dlna/servers') {
      state = {busy:false,revision:state.revision+1,servers:true,items:[{name:'Windows Media Player · PC'},{name:'VAULT - Luna Veil'}],error:''}; result = {ok:true};
    } else if (req.url === '/api/dlna/browse') {
      state = {busy:false,revision:state.revision + 1,servers:false,server:0,object:args.id,heading:args.title,offset:0,more:false,error:'',
        items:args.id === '0' ? [{name:'Zene / Albumok',id:'music',directory:true}] :
        [{name:'Árvíztűrő tükörfúrógép – a very long album title <test>',artist:'Előadó & zenekar',id:'track',directory:false,playable:true}],
        dlnaPsram:48000,dlnaPeak:120000,budget:524288,internalFree:80000,internalLargest:32000,psramFree:5000000}; result = {ok:true};
    } else if (req.url === '/api/dlna/play') {
      audio = {...audio,source:'dlna',stationName:'DLNA',title:'Előadó – Árvíztűrő tükörfúrógép'}; result = {ok:true};
    } else if (req.url.startsWith('/api/')) result = {ok:true};
    if (result) {res.setHeader('Content-Type','application/json'); res.end(JSON.stringify(result)); return;}
    const file = req.url === '/dlna' ? 'dlna.html' : req.url === '/' ? 'index_hu.html' :
      /^\/web\/[a-zA-Z0-9_.-]+$/.test(req.url) ? req.url.slice(5) : null;
    if (!file) {res.writeHead(404); res.end(); return;}
    if (!fs.existsSync(path.join('data/web',file))) {res.writeHead(404); res.end(); return;}
    res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html; charset=utf-8');
    res.end(fs.readFileSync(path.join('data/web',file)));
  });
  await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser = await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
    const page = await browser.newPage({viewport:{width:390,height:844}});
    page.on('pageerror',e => errors.push(e.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/dlna`);
    const manualBox=await page.locator('#manualForm').boundingBox();
    const introBox=await page.locator('[data-t="intro"]').boundingBox();
    assert(manualBox.y+manualBox.height<=introBox.y,'manual fields must precede the intro');
    await page.locator('#manualIp').fill('999.1.2.3');
    await page.locator('#manualConnect').click();
    assert(!requests.some(r=>r.url==='/api/dlna/connect'));
    await page.locator('#manualIp').fill('192.168.31.103');
    await page.locator('#manualPort').fill('8000');
    await page.locator('#manualConnect').click();
    await page.getByRole('heading',{name:'Manual NAS',exact:true}).waitFor();
    assert.deepEqual(requests.find(r=>r.url==='/api/dlna/connect').args,{ip:'192.168.31.103',port:'8000'});
    await page.locator('#manualPort').fill('65534');
    await page.locator('#manualConnect').click();
    await page.getByText(/Ezen a címen nem sikerült/).waitFor();
    assert.equal(await page.locator('#items button').count(),1,'failure must retain the previous directory');
    await page.getByRole('button',{name:'Szerverek keresése'}).click();
    await page.getByRole('button',{name:'Windows Media Player · PC'}).click();
    await page.getByRole('button',{name:'Zene / Albumok'}).click();
    await page.getByRole('button',{name:/Árvíztűrő tükörfúrógép/}).click();
    await page.getByText('DLNA · Előadó – Árvíztűrő tükörfúrógép',{exact:true}).waitFor();
    await page.getByText('Memória',{exact:true}).click();
    assert.equal(await page.locator('body').evaluate(e => e.scrollWidth > innerWidth),false,'mobile horizontal overflow');
    await page.screenshot({path:'dlna-mobile-preview.png',fullPage:true});
    await page.setViewportSize({width:1100,height:900});
    await page.screenshot({path:'dlna-desktop-preview.png',fullPage:true});
    await Promise.all([
      page.waitForResponse(r => r.url().endsWith('/api/track_next')),
      page.getByRole('button',{name:'Következő szám'}).click()
    ]);
    assert(requests.some(r => r.url === '/api/track_next'));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.locator('#dlna-browser #items button').first().waitFor();
    assert.equal(await page.locator('#list').isVisible(),false);
    assert.equal(await page.locator('#newName').isVisible(),false);
    assert.equal(await page.getByRole('link',{name:'DLNA',exact:true}).count(),1);
    assert.equal(await page.locator('#dlna-browser #now').isVisible(),false);
    await page.getByRole('button',{name:'📻 Rádió',exact:true}).click();
    await page.locator('#newName').waitFor({state:'visible'});
    assert.equal(await page.locator('#dlna-browser').count(),0);
    assert.equal(await page.locator('#list').isVisible(),true);
    for (const width of [390,1100]) {
      await page.setViewportSize({width,height:900});
      const plus = await page.getByRole('button',{name:'Állomás hozzáadása',exact:true}).boundingBox();
      const homepage = await page.locator('#newHomepage').boundingBox();
      assert.equal(plus.width,48); assert.equal(plus.height,48);
      assert(Math.abs(homepage.y-plus.y)<1,'add button must align with the final input');
      assert.equal(await page.locator('body').evaluate(e=>e.scrollWidth>innerWidth),false);
      await page.screenshot({path:`build-dlna/source-header-${width}.png`,fullPage:true});
    }
    await page.getByRole('link',{name:'DLNA',exact:true}).click();
    await page.getByRole('heading',{name:'Hálózati zene',exact:true}).waitFor();
    assert.equal(errors.length,0,errors.join('\n'));
    console.log('DLNA browser tests passed: standalone + embedded browsing, header link, source return, 48px add button and mobile/desktop layouts.');
  } finally {if(browser) await browser.close(); server.close();}
})().catch(e => {console.error(e);process.exitCode=1;});
