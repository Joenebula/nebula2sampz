// Serves design/ over http so the browser pane can render the real file. The pane only
// renders files inside the working directory, and this repo is outside it - serving beats
// copying, because a copy is a second source that silently goes stale.
const http=require('http'), fs=require('fs'), path=require('path');
const ROOT=__dirname, PORT=8731;
const TYPES={'.html':'text/html;charset=utf-8','.js':'text/javascript','.css':'text/css','.ttf':'font/ttf'};
http.createServer((req,res)=>{
  const rel=decodeURIComponent(req.url.split('?')[0]);
  const file=path.join(ROOT, rel==='/'?'ui-spec.html':rel);
  if(!file.startsWith(ROOT)){res.writeHead(403).end('no');return;}
  fs.readFile(file,(e,d)=>{
    if(e){res.writeHead(404).end('not found');return;}
    res.writeHead(200,{'content-type':TYPES[path.extname(file)]||'application/octet-stream',
                       'cache-control':'no-store'}).end(d);});
}).listen(PORT,()=>console.log('serving design/ on http://localhost:'+PORT));
