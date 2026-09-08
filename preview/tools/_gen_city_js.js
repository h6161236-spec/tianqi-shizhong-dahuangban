/* one-shot: dump CITY_LIST snippet from firmware/src/cities.cpp */
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, '../../firmware/src/cities.cpp'), 'utf8');
const re = /\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*([0-9.+-]+)f\s*,\s*([0-9.+-]+)f\s*\}/g;
const rows = [];
let m;
while ((m = re.exec(src))) {
  rows.push(`${m[1]}|${m[2]}|${m[3]}|${m[4]}|${m[5]}`);
}
const hot = [];
const hotRe = /"([^"]+)"/g;
const hotBlock = src.match(/HOT_NAMES\[] = \{([\s\S]*?)\};/)[1];
let h;
while ((h = hotRe.exec(hotBlock))) hot.push(h[1]);
const provBlock = src.match(/PROV_NAMES\[] = \{([\s\S]*?)\};/)[1];
const prov = [];
const pRe = /"([^"]+)"/g;
let p;
while ((p = pRe.exec(provBlock))) prov.push(p[1]);
const out = [
  'const PROV_NAMES = ' + JSON.stringify(prov, null, 0) + ';',
  'const HOT_NAMES = ' + JSON.stringify(hot, null, 0) + ';',
  'const CITY_RAW = `',
  rows.join('\n'),
  '`;',
  'const CITY_LIST = CITY_RAW.trim().split(/\\n/).map(l => {',
  "  const [name, py, prov, lat, lon] = l.split('|');",
  '  return { name, py, prov: +prov, lat: +lat, lon: +lon };',
  '});',
].join('\n');
fs.writeFileSync(path.join(__dirname, '_city_snip.js'), out, 'utf8');
console.log('cities', rows.length, 'hot', hot.length, 'prov', prov.length);
