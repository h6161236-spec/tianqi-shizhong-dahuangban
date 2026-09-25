/* Headless smoke test: extract the inline script from ui.html, stub the DOM +
   canvas 2D API, then render every page many times and fail loudly on any
   exception. Catches runtime errors (bad identifiers, wrong call order) that a
   syntax check cannot see.
   Usage: node tools/smoke.js */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'ui.html'), 'utf8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.error('no <script> block found'); process.exit(1); }
let js = m[1];
// Observe semantic text calls while the actual renderer paints hardware pixels.
js = js.replace("const txt = (s, x, y, c, f = FONT.CN12, align = 'left') => {",
  "const txt = (s, x, y, c, f = FONT.CN12, align = 'left') => { globalThis.__drawnText.push([s,x,y,c,f,align]);");

/* ---- stubs ---- */
const noop = () => {};
const drawnText = [];
const ctxStub = () => {
  const c = {
    canvas: { width: 320, height: 240 },
    save: noop, restore: noop, beginPath: noop, closePath: noop, moveTo: noop,
    lineTo: noop, arc: noop, arcTo: noop, ellipse: noop, bezierCurveTo: noop,
    quadraticCurveTo: noop, rect: noop, fill: noop, stroke: noop, clip: noop,
    fillRect: noop, strokeRect: noop, clearRect: noop,
    fillText: (...args) => drawnText.push(args),
    strokeText: noop, translate: noop, scale: noop, rotate: noop, setTransform: noop,
    drawImage: noop, createLinearGradient: () => ({ addColorStop: noop }),
    measureText: s => ({ width: String(s).length * 6 }),
  };
  return c;
};
const elStub = (id) => {
  const el = {
    id, children: [], dataset: {}, style: {}, checked: true, value: '0',
    textContent: '', innerHTML: '', selectedOptions: [{ textContent: '苏州' }],
    className: '', width: 320, height: 240,
    classList: { add: noop, remove: noop, toggle: noop, contains: () => false },
    addEventListener: noop, removeEventListener: noop, setAttribute: noop,
    getAttribute: () => null, appendChild(c) { this.children.push(c); return c; },
    closest: () => null, getBoundingClientRect: () => ({ left: 0, top: 0, width: 960, height: 720 }),
    getContext: () => ctxStub(),
  };
  return el;
};
const registry = new Map();
const getEl = id => {
  if (!registry.has(id)) registry.set(id, elStub(id));
  return registry.get(id);
};

let rafCb = null;
const sandbox = {
  __drawnText: drawnText,
  console,
  document: {
    getElementById: getEl,
    createElement: tag => elStub(tag),
    addEventListener: noop,
  },
  window: {},
  location: { hash: '' },
  navigator: { userAgent: 'node' },
  performance: { now: () => Date.now() },
  requestAnimationFrame: cb => { rafCb = cb; return 1 },
  cancelAnimationFrame: noop,
  addEventListener: noop,
  setTimeout, clearTimeout, setInterval, clearInterval,
  fetch: () => Promise.reject(new Error('offline in smoke test')),
  Image: function () { this.complete = false; this.naturalWidth = 0; },
  devicePixelRatio: 1,
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;

/* expose internals the harness needs to drive: append an epilogue */
js += `
;globalThis.__T = {
  drawFrame, S, PAGES, RENDER, doAct, goVert, setPage, openModal,
  setNet: typeof setNet === 'function' ? setNet : null,
  setBt: typeof setBt === 'function' ? setBt : null,
  tick: dt => { sceneClock += dt; tickSpec(dt); tickNetwork(dt);
                S.pkFrame = Math.floor(sceneClock * PK_SPD / 22 * 4) % 4;
                if (S.page === 0) {
                  for (const t of TUFTS) { t.x -= PK_SPD * dt; if (t.x < -8) { t.x += 336; t.h = 2 + Math.round(rng()*2); } }
                }
                if (S.pkJump !== null) { S.pkJump += dt / .55; if (S.pkJump >= 1) S.pkJump = null; } },
  HITS: () => HITS, hitAt, txtMarquee, pinCity, shownWeather, weather: () => WX,
  dayLabel, validForecast, apiRefresh, netBadge, keyboardRows, keyboardLimit, solarToLunar,
  connectDone, wrapLyrics, txtW, song,
};
`;

const ctx = vm.createContext(sandbox);
let fails = 0;
const fail = (what, e) => { fails++; console.error(`FAIL ${what}\n  ${e && e.stack ? e.stack.split('\n').slice(0,4).join('\n  ') : e}`); };

try {
  vm.runInContext(fs.readFileSync(path.join(root, 'fonts.js'), 'utf8'), ctx, { filename:'fonts.js' });
  vm.runInContext(js, ctx, { filename: 'ui.html:script' });
}
catch (e) { fail('module top-level', e); process.exit(1); }

const T = sandbox.__T;
if (!T) { console.error('epilogue did not run'); process.exit(1); }

/* ---- 1. every page renders, across many animation steps ---- */
for (let p = 0; p < T.PAGES.length; p++) {
  T.S.page = p; T.S.slide = 0;
  for (let i = 0; i < 90; i++) {
    try { T.tick(1/60); T.drawFrame(); }
    catch (e) { fail(`page ${p} (${T.PAGES[p].id}) frame ${i}`, e); break; }
  }
}

/* ---- 2. music page in all its variants ---- */
const variants = [];
for (const style of [0, 1, 2, 3])
  for (const playing of [true, false])
    for (const bt of ['con', 'adv', 'off'])
      variants.push({ style, playing, bt });
for (const v of variants) {
  T.S.page = 1; T.S.vsel[1] = v.style;
  T.S.playing = v.playing; T.S.btState = v.bt;
  try { T.tick(1/60); T.drawFrame(); }
  catch (e) { fail(`music variant ${JSON.stringify(v)}`, e); }
}
T.S.btState = 'con'; T.S.playing = true;
try {
  T.S.mediaReady = false;
  drawnText.length = 0;
  T.S.page = 1; T.drawFrame();
  if (!drawnText.some(args => args[0] === '等待手机媒体信息'))
    throw new Error('connected state without metadata showed demo song');
} catch (e) { fail('BLE waiting state', e); }
T.S.mediaReady = true;

/* ---- 2b. long lyrics pause at the start, then move left ---- */
try {
  const longLyric = '这是一句明显超过右侧显示区宽度的长歌词，后半句也必须滚动出来';
  drawnText.length = 0;
  T.txtMarquee(longLyric, 162, 112, 148, '#fff', '12px sans-serif', 1.0);
  const heldX = drawnText[drawnText.length - 1][1];
  drawnText.length = 0;
  T.txtMarquee(longLyric, 162, 112, 148, '#fff', '12px sans-serif', 2.0);
  const movingX = drawnText[0][1];
  if (!(movingX < heldX)) throw new Error(`expected ${movingX} < ${heldX}`);
} catch (e) { fail('long lyric marquee', e); }

try {
  T.S.page=1; T.S.vsel[1]=3; T.S.btState='con'; T.S.mediaReady=true;
  const lyric='这是一句超过两行的歌词 Mixed ASCII 123，所有内容都需要显示，翻页后仍然能够看到最后一句。'.repeat(2);
  const lines=T.wrapLyrics(lyric);
  if (lines.join('')!==lyric || lines.some(line=>T.txtW(line,'CN16')>280))
    throw new Error('lyrics were lost or overflowed while wrapping');
  if (lines.length<=2) throw new Error('fixture must need multiple pages');
  T.S.lyricOverride=lyric; drawnText.length=0; T.drawFrame();
  if (!drawnText.some(t=>t[0]===lines[0] && t[2]===84) || !drawnText.some(t=>t[0]===lines[1] && t[2]===106))
    throw new Error('first two lyric lines not visible');
  T.tick(3.61); drawnText.length=0; T.drawFrame();
  if (!drawnText.some(t=>t[0]===lines[2])) throw new Error('long lyrics did not advance to next page');
  T.S.lyricOverride='新的一句'; drawnText.length=0; T.drawFrame();
  if (!drawnText.some(t=>t[0]==='新的一句' && t[2]===84)) throw new Error('new line did not restart pagination');
  T.S.lyricOverride=''; drawnText.length=0; T.drawFrame();
  if (!drawnText.some(t=>t[0]==='等待歌词')) throw new Error('empty lyric lacks waiting state');
  delete T.S.lyricOverride;
  const current=T.song.lrc[Math.floor(T.S.trackPos/6)%T.song.lrc.length];
  for (const style of [0,1,2,3]) {
    T.S.vsel[1]=style; drawnText.length=0; T.drawFrame();
    if (T.song.lrc.some(l=>l!==current && drawnText.some(t=>t[0]===l)))
      throw new Error('preview fabricated future BLE lyrics');
  }
  T.S.vsel[1]=3; T.goVert(1);
  if(T.S.vsel[1]!==0)throw new Error('four music styles do not wrap');
  T.goVert(-1);
  if(T.S.vsel[1]!==3)throw new Error('new style not reachable by reverse swipe');
  T.S.toast=null;
} catch (e) { fail('hardware lyric pagination and current-line parity',e); }

/* ---- 3. every hotspot action fires without throwing ---- */
const ids = new Set();
for (let p = 0; p < T.PAGES.length; p++) {
  T.S.page = p; T.S.modal = null;
  T.drawFrame();
  for (const h of T.HITS()) ids.add(h.id);
}
for (const kind of ['wifi', 'bt', 'reset', 'reboot', 'city']) {
  T.S.page = 4; T.openModal(kind); T.drawFrame();
  for (const h of T.HITS()) ids.add(h.id);
}
T.S.modal = null;
for (const id of ids) {
  try {
    T.S.page = 1; T.S.modal = null;
    T.doAct(id);
    T.drawFrame();
  } catch (e) { fail(`doAct('${id}')`, e); }
}
T.S.modal = null;

/* ---- 4. vertical gestures on every page ---- */
for (let p = 0; p < T.PAGES.length; p++) {
  T.S.page = p; T.S.modal = null;
  for (const d of [1, -1, 1, 1, -1]) {
    try { T.goVert(d); T.drawFrame(); }
    catch (e) { fail(`goVert(${d}) on page ${p}`, e); }
  }
}

/* ---- 5. network states x weather codes x hours on the clock page ---- */
T.S.page = 0;
for (const net of ['online', 'offline', 'unconfig']) {
  if (T.setNet) { try { T.setNet(net); } catch (e) { fail(`setNet(${net})`, e); } }
  else T.S.net = net;
  for (const ovr of ['auto','clear','mostly','partly','overcast','fog','drizzle','rain','heavyrain','freezing','snow','heavysnow','thunder']) {
    T.S.wxOvr = ovr;
    for (const tod of ['auto','dawn','day','dusk','night']) {
      T.S.todOvr = tod;
      try { T.tick(1/60); T.drawFrame(); }
      catch (e) { fail(`clock net=${net} wx=${ovr} tod=${tod}`, e); }
    }
  }
}
T.S.wxOvr = 'auto'; T.S.todOvr = 'auto'; T.S.net = 'online';

/* ---- 5b. city picker: auto / pin / province / pinyin ---- */
try {
  T.S.page = 4; T.S.cityMode = 'auto';
  T.openModal('city'); T.drawFrame();
  T.goVert(1); T.goVert(-1); T.drawFrame();
  T.doAct('c.it.0');
  T.openModal('city'); T.doAct('c.it.1');
  T.openModal('city'); T.doAct('c.search'); T.drawFrame();
  T.S.modal.pass = 'sz'; T.doAct('k.ok'); T.drawFrame();
  T.doAct('c.it.0');
  T.openModal('city');
  const nProv = 1 + 20; /* auto + 20 hot → first province */
  T.doAct('c.it.' + nProv); T.drawFrame();
  T.doAct('c.back'); T.doAct('c.cancel');
} catch (e) { fail('city picker', e); }
T.S.modal = null;

/* ---- 5c. selected place, dates, API validation, and WiFi controls ---- */
try {
  const priorCity = { ...T.S.city }, priorMode = T.S.cityMode;
  const priorNet = T.S.net, priorRefreshFailed = T.S.refreshFailed;
  const priorSnapshot = T.weather();
  T.pinCity({ name: '杭州', lat: 30.25, lon: 120.17 });
  if (T.shownWeather() !== null || T.weather() !== priorSnapshot || T.weather().city === '杭州')
    throw new Error('city switch reused another place\'s weather');
  T.S.net = 'offline';
  if (T.netBadge().text !== '离线-暂无数据')
    throw new Error('empty weather state lost offline reason');
  T.S.net = 'online'; T.S.refreshFailed = true;
  if (T.netBadge().text !== '刷新失败-暂无数据')
    throw new Error('empty weather state lost refresh failure reason');
  T.S.refreshFailed = false;
  drawnText.length = 0;
  T.S.page = 2; T.drawFrame();
  if (!drawnText.some(args => args[0] === '暂无天气数据'))
    throw new Error('missing empty weather state');
  T.S.city = priorCity; T.S.cityMode = priorMode;
  if (T.weather().demo) {
    T.S.city = { name: T.weather().city, lat: T.weather().lat, lon: T.weather().lon };
    T.S.net = 'online'; T.S.refreshFailed = true;
    if (T.netBadge().text !== '刷新失败-演示数据')
      throw new Error('failed demo refresh mislabeled as cached data');
  }
  T.S.city = priorCity;
  T.S.net = priorNet; T.S.refreshFailed = priorRefreshFailed;

  const ref = new Date('2026-09-24T23:59:00');
  if (T.dayLabel('2026-09-25', ref) !== '明天' ||
      T.dayLabel('2026-09-25', new Date('2026-09-25T00:01:00')) !== '今天')
    throw new Error('day label did not cross midnight');
  const dailyDates = Array.from({ length: 7 }, (_, i) => {
    const d = new Date(2026, 8, 24 + i);
    return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
  });
  const good = {
    current: { time: '2026-09-24T12:00', weather_code: 2, temperature_2m: 25, apparent_temperature: 26,
      relative_humidity_2m: 60, wind_speed_10m: 8 },
    daily: { time: dailyDates, weather_code: Array(7).fill(2),
      temperature_2m_max: Array(7).fill(30), temperature_2m_min: Array(7).fill(20) },
    hourly: { time: ['2026-09-24T12:00'], weather_code: [2], temperature_2m: [25] },
  };
  if (!T.validForecast(good) || T.validForecast({ ...good, daily: { ...good.daily, temperature_2m_min: [] } }))
    throw new Error('API field validation failed');

  const chars = new Set();
  for (const mode of [0, 1, 2]) for (const shift of [false, true])
    for (const row of T.keyboardRows({ kbMode: mode, kbShift: shift }))
      for (const ch of row) chars.add(ch);
  chars.add(' ');
  for (let i = 32; i <= 126; i++) if (!chars.has(String.fromCharCode(i)))
    throw new Error(`missing ASCII key ${String.fromCharCode(i)}`);
  if (T.keyboardLimit({ kbWhy: 0 }) !== 64) throw new Error('WiFi password limit');

  T.openModal('wifi'); T.drawFrame();
  if (T.hitAt(190, 200)?.id !== 'x.w.refresh' || T.hitAt(270, 200)?.id !== 'x.w.cancel')
    throw new Error('WiFi buttons are not reachable by touch');
  T.doAct(T.hitAt(120, 200).id); T.drawFrame();
  if (!T.HITS().some(h => h.id === 'w.net.6')) throw new Error('second scan page unavailable');
  T.doAct(T.hitAt(190, 200).id);
  if (T.S.modal.scanState !== 'running') throw new Error('rescan did not start');
  T.doAct(T.hitAt(270, 200).id);
  if (T.S.modal !== null) throw new Error('WiFi cancel did not close');
  const savedSsid = T.S.wifiSsid;
  T.S.modal = { kind: 'connecting', ssid: 'Wrong-Network', pass: 'wrong', connT: 0 };
  T.connectDone();
  if (T.S.wifiSsid !== savedSsid || T.S.modal?.kind !== 'kb')
    throw new Error('wrong password changed saved network');
} catch (e) { fail('weather and WiFi behavior', e); }
T.S.modal = null;

try {
  for (const [y, m, d, month] of [
    [2048, 11, 6, 10], [2048, 12, 5, 11], [2049, 1, 4, 12],
  ]) {
    const lunar = T.solarToLunar(y, m, d);
    if (!lunar || lunar.y !== 2048 || lunar.m !== month || lunar.d !== 1)
      throw new Error(`${y}-${m}-${d} should start lunar month ${month}`);
  }
} catch (e) { fail('2048 HKO lunar months', e); }

/* ---- 6. long run: pikachu travel wrap-around, dust pool, spectrum decay ---- */
T.S.page = 0; T.S.pkJump = 0;
for (let i = 0; i < 3000; i++) {
  try { T.tick(1/60); T.drawFrame(); }
  catch (e) { fail(`long run frame ${i}`, e); break; }
}

(async () => {
  try {
    const before = T.weather();
    T.S.refetchBusy = false;
    sandbox.fetch = async () => ({ ok: true, json: async () => ({ current: {} }) });
    if (await T.apiRefresh() !== false || T.weather() !== before || !T.S.refreshFailed)
      throw new Error('incomplete API response replaced snapshot');
  } catch (e) { fail('API failure preserves snapshot', e); }
  if (fails) { console.error(`\n${fails} failure(s)`); process.exit(1); }
  console.log('smoke OK: all pages, variants, actions, gestures and a 50s run passed');
})().catch(e => { console.error(e); process.exit(1); });
