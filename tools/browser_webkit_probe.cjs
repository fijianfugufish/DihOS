/* Separate host compatibility probe; pass the bundled Playwright module path.
 * Runtime must be installed with PLAYWRIGHT_BROWSERS_PATH pointing into build.
 */
const fs=require('fs'),path=require('path'),http=require('http');
const root=path.resolve(__dirname,'..'),out=path.join(root,'build/browser-engine-probe/webkit');fs.mkdirSync(out,{recursive:true});
const pw=require(process.argv[2]);
const server=http.createServer((req,res)=>{
 let file=req.url.startsWith('/fonts/')?path.join(root,'OS/System/Fonts/Web',path.basename(req.url)):path.join(root,'tools/browser_fixtures',path.basename(req.url));
 if(!fs.existsSync(file)){res.writeHead(404).end();return;}res.setHeader('Content-Type',file.endsWith('.ttf')?'font/ttf':'text/html; charset=utf-8');res.end(fs.readFileSync(file));
});
(async()=>{
 await new Promise(resolve=>server.listen(8767,'127.0.0.1',resolve));
 let browser;const result={engine:'Playwright WebKit 26.5 revision 2336',host_only:true,tests:{},sites:[]};
 try{
  browser=await pw.webkit.launch({headless:true});const page=await browser.newPage({viewport:{width:1000,height:900}});
  result.runtime=browser.version();let start=Date.now();await page.goto('http://127.0.0.1:8767/layout.html');result.local_navigation_ms=Date.now()-start;
  result.tests=await page.evaluate(()=>window.fixtureResults());await page.screenshot({path:path.join(out,'layout.png')});
  if(process.argv.includes('--live'))for(const [name,url] of [['wikipedia','https://en.wikipedia.org/wiki/Web_browser'],['github','https://github.com/servo/servo'],['mdn','https://developer.mozilla.org/en-US/docs/Web/CSS'],['duckduckgo','https://duckduckgo.com/?q=web+browser']]){
   const row={name,url};start=Date.now();try{await page.goto(url,{timeout:25000,waitUntil:'domcontentloaded'});await page.waitForTimeout(10000);row.dom=await page.evaluate(()=>({title:document.title,text:document.body.innerText.length,links:document.links.length,state:document.readyState}));await page.screenshot({path:path.join(out,name+'.png'),timeout:10000});}catch(e){row.error=String(e);}row.elapsed_ms=Date.now()-start;result.sites.push(row);console.log(JSON.stringify(row));
  }
  result.status='ran';
 }catch(e){result.status='blocked';result.error=String(e);}
 finally{if(browser)await browser.close();server.close();fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));}
})();
