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

/* ---- stubs ---- */
const noop = () => {};
const ctxStub = () => {
  const c = {
    canvas: { width: 320, height: 240 },
    save: noop, restore: noop, beginPath: noop, closePath: noop, moveTo: noop,
    lineTo: noop, arc: noop, arcTo: noop, ellipse: noop, bezierCurveTo: noop,
    quadraticCurveTo: noop, rect: noop, fill: noop, stroke: noop, clip: noop,
    fillRect: noop, strokeRect: noop, clearRect: noop, fillText: noop,
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
                for (const t of TUFTS) { t.x -= PK_SPD * dt; if (t.x < -8) { t.x += 336; t.h = 2 + Math.round(rng()*2); } }
                if (S.pkJump !== null) { S.pkJump += dt / .55; if (S.pkJump >= 1) S.pkJump = null; } },
  HITS: () => HITS,
};
`;

const ctx = vm.createContext(sandbox);
let fails = 0;
const fail = (what, e) => { fails++; console.error(`FAIL ${what}\n  ${e && e.stack ? e.stack.split('\n').slice(0,4).join('\n  ') : e}`); };

try { vm.runInContext(js, ctx, { filename: 'ui.html:script' }); }
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
for (const style of [0, 1, 2])
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

/* ---- 3. every hotspot action fires without throwing ---- */
const ids = new Set();
for (let p = 0; p < T.PAGES.length; p++) {
  T.S.page = p; T.S.modal = null;
  T.drawFrame();
  for (const h of T.HITS()) ids.add(h.id);
}
for (const kind of ['wifi', 'bt', 'reset', 'reboot']) {
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

/* ---- 6. long run: pikachu travel wrap-around, dust pool, spectrum decay ---- */
T.S.page = 0; T.S.pkJump = 0;
for (let i = 0; i < 3000; i++) {
  try { T.tick(1/60); T.drawFrame(); }
  catch (e) { fail(`long run frame ${i}`, e); break; }
}

if (fails) { console.error(`\n${fails} failure(s)`); process.exit(1); }
console.log('smoke OK: all pages, variants, actions, gestures and a 50s run passed');
