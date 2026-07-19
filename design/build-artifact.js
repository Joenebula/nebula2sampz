// Builds design/ui-spec.artifact.html FROM design/ui-spec.html.
//
// Two things have to change for the hosted version:
//   1. it must be a fragment - no doctype/html/head/body wrapper
//   2. the Google Fonts <link> must go, because the artifact CSP blocks font CDNs and the
//      page would silently fall back to a system face. That is the exact failure this whole
//      exercise exists to avoid, so the faces are inlined as data URIs instead.
//
// Generated, never hand-edited: ui-spec.html stays the single source.
const fs = require('fs'), path = require('path');
const src = fs.readFileSync('design/ui-spec.html', 'utf8');

const FACES = [
  ['Chakra Petch', 400, 'ChakraPetch-Regular'],
  ['Chakra Petch', 600, 'ChakraPetch-SemiBold'],
  ['Chakra Petch', 700, 'ChakraPetch-Bold'],
  ['IBM Plex Mono', 500, 'IBMPlexMono-Medium'],
];
const faceCss = FACES.map(([fam, wt, file]) => {
  const b64 = fs.readFileSync(path.join('Resources/fonts', file + '.ttf')).toString('base64');
  return `@font-face{font-family:'${fam}';font-style:normal;font-weight:${wt};font-display:block;`
       + `src:url(data:font/ttf;base64,${b64}) format('truetype')}`;
}).join('\n');

let out = src
  .replace(/^[\s\S]*?<title>[\s\S]*?<\/title>/, '')          // drop everything to the title
  .replace(/<link[^>]*fonts\.(googleapis|gstatic)[^>]*>/g, '')
  .replace(/<\/head>|<body>|<\/body>|<\/html>/g, '')
  .replace('<style>', '<style>\n' + faceCss + '\n');

// The page relied on body{display:flex;height:100%}; as a fragment it needs its own shell.
out = out.replace('body{margin:0;height:100%}', '')
         .replace('html,body{margin:0;height:100%}', '')
         .replace(/^\s*body\{[^}]*\}/m, '');
out = `<div id="nb2root" style="height:86vh;min-height:560px;display:flex;flex-direction:column;`
    + `background:#05080d;color:#9fb0bf;font-family:'Chakra Petch',sans-serif;`
    + `border-radius:10px;overflow:hidden">\n${out}\n</div>`;

fs.writeFileSync('design/ui-spec.artifact.html', out);
console.log('wrote design/ui-spec.artifact.html —', Math.round(out.length/1024), 'KB');
console.log('font links removed:', !/fonts\.googleapis/.test(out));
console.log('faces inlined:', (out.match(/@font-face/g)||[]).length);
console.log('no doctype/html tag:', !/<!DOCTYPE|<html/i.test(out));
