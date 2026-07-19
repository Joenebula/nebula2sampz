// Headless check for design/ui-spec.html.
//
//   node design/check.js
//
// The editor cannot be seen from here, so this is how a change gets verified: extract the
// page script, parse it, run it against a stubbed DOM, and assert about the markup it
// produces. It has already caught a template literal closed early - which silently dropped
// the whole waveform - before that reached the browser.
//
// It runs the script through `new Function` rather than `eval`. Plain eval inherits the
// enclosing scope, and the page declares `function load()`, which collides with a binding
// Node already has in a module scope. Three separate ad-hoc harnesses tripped over that (or
// over a global that shadowed the page's own `page` variable) and reported false results
// before this was written down properly. A checker that lies is worse than no checker.
const fs = require('fs');
const path = require('path');

const HTML = path.join(__dirname, 'ui-spec.html');
const src = fs.readFileSync(HTML, 'utf8');
const m = src.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.error('no <script> found in ui-spec.html'); process.exit(1); }
const js = m[1];

const els = {};
const stub = () => ({ style:{}, innerHTML:'', value:'100', textContent:'', className:'',
                      dataset:{}, showModal(){}, close(){} });
const doc = { getElementById: id => els[id] || (els[id] = stub()),
              querySelectorAll: () => [], querySelector: () => null };

let go;
try {
  go = new Function('document','localStorage','navigator','confirm',
        js + "\n;return p=>{page=p;render();return document.getElementById('page').innerHTML;};")(
        doc, { getItem:()=>null, setItem:()=>{} }, {}, ()=>false);
} catch (e) { console.error('SCRIPT FAILED TO RUN:', e.message); process.exit(1); }

let bad = 0;
const check = (ok, what) => { console.log((ok?'  ok  ':'  FAIL') + '  ' + what); if (!ok) bad++; };

const out = {};
for (const p of ['sample','grid','morph','rack']) {
  const h = out[p] = go(p);
  check(h.length > 1000, `${p} renders (${h.length} chars)`);
  check(!h.includes('undefined'), `${p} has no "undefined" in its markup`);
  check(!h.includes('NaN'), `${p} has no "NaN" in its markup`);
}

// Things a screenshot caught once and should not have to catch again.
const s = out.sample;
check(s.indexOf('class="shl"') >= 0 && s.indexOf('LOAD SAMPLE') > s.indexOf('class="shl"')
      && s.indexOf('LOAD SAMPLE') < s.indexOf('class="ctl"'),
      'LOAD SAMPLE sits in the non-wrapping left group, not after the controls');
check(s.includes('id="wav"'), 'the waveform survives (a template literal once ate it)');

const r = out.rack;
check(r.indexOf('align-items:center;gap:14px') >= 0
      && r.indexOf('LEVEL') > r.indexOf('align-items:center;gap:14px')
      && r.indexOf('LEVEL') < r.indexOf('BEAT OUT'),
      'Main Out meter and LEVEL share one row');

console.log(bad ? `\n${bad} FAILED` : '\nall checks passed');
process.exit(bad ? 1 : 0);
