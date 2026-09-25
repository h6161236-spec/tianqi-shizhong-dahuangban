/* Real Edge viewport checks and screenshots; Node 22+ built-ins only. */
const fs=require('node:fs'), path=require('node:path'), os=require('node:os');
const {spawn}=require('node:child_process');
const {pathToFileURL}=require('node:url');
const root=path.resolve(__dirname,'..'), output=path.join(root,'_shot');
fs.mkdirSync(output,{recursive:true});
const edge=['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe','C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(fs.existsSync);
if(!edge) throw Error('Microsoft Edge required');
const profile=fs.mkdtempSync(path.join(os.tmpdir(),'pika-check-'));
const child=spawn(edge,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check',
  '--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{windowsHide:true,stdio:'ignore'});
const delay=ms=>new Promise(r=>setTimeout(r,ms));
let socket;
(async()=>{
  const portFile=path.join(profile,'DevToolsActivePort');
  for(let i=0;!fs.existsSync(portFile)&&i<100;i++) await delay(100);
  const port=fs.readFileSync(portFile,'utf8').split('\n')[0];
  const targets=await (await fetch(`http://127.0.0.1:${port}/json`)).json();
  socket=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((r,j)=>{socket.onopen=r;socket.onerror=j});
  let sequence=0;const pending=new Map(),errors=[];
  socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);if(p){pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}}
    else if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails.text);};
  const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++sequence;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
  await call('Page.enable');await call('Runtime.enable');
  for(const item of [
    ['desktop',1440,1050,'page=1&spec=3'],['mobile',390,844,'page=1&spec=3'],
    ['clock',960,720,'page=0&tod=day&wx=clear&capture=1'],
    ['music',960,720,'page=1&spec=3&capture=1'],
    ['weather',960,720,'page=2&capture=1'],['date',960,720,'page=3&capture=1'],
    ['settings',960,720,'page=4&capture=1'],['keyboard',960,720,'page=4&modal=kb&capture=1'],
    ['long-lyrics',960,720,'page=1&spec=3&capture=1&lyric='+encodeURIComponent('每一句长歌词都应该完整显示，中英混排 Music 123 也不会挤出屏幕。这一段在第二页显示，随后回到开头。')]
  ]){
    const [name,width,height,hash]=item;
    await call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    // A unique query forces a document reload; changing only the hash would
    // leave this preview's startup state unchanged and produce false positives.
    await call('Page.navigate',{url:pathToFileURL(path.join(root,'ui.html')).href+'?review='+name+'#'+hash});
    await delay(400);
    const measured=await call('Runtime.evaluate',{expression:`JSON.stringify({ready:!!window.PIKA_FONTS,page:S.page,modal:S.modal?.kind,capture:document.body.classList.contains('capture'),width:innerWidth,scroll:document.documentElement.scrollWidth,canvas:document.getElementById('screen').getBoundingClientRect().toJSON()})`,returnByValue:true});
    const state=JSON.parse(measured.result.value);
    if(!state.ready||state.scroll>width||state.canvas.right>width+.1)throw Error(name+': invalid viewport '+JSON.stringify(state));
    if(state.page!==Number(new URLSearchParams(hash).get('page')) || state.capture!==hash.includes('capture=1'))
      throw Error(name+': wrong page state '+JSON.stringify(state));
    const shot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(path.join(output,'verified-'+name+'.png'),Buffer.from(shot.data,'base64'));
    console.log(name+': '+width+'x'+height+' no overflow');
  }
  if(errors.length)throw Error(errors.join('\n'));
  await call('Browser.close').catch(()=>{});
  console.log('Browser screenshots and runtime checks passed');
})().catch(e=>{console.error(e);process.exitCode=1}).finally(()=>{socket?.close();child.kill();});
