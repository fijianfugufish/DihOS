/* Host-only evaluation. This never supplies pixels or networking to DihOS.
 * Usage: node tools/browser_webapps_probe.cjs <absolute Playwright module path>
 */
const fs=require('fs'),path=require('path'),http=require('http');
const root=path.resolve(__dirname,'..'),out=path.join(root,'build/browser-engine-probe/webapps');
const pw=require(process.argv[2]||'playwright');
fs.mkdirSync(out,{recursive:true});
const allowed={'/webapps.html':['webapps.html','text/html; charset=utf-8'],'/webapps-feed.json':['webapps-feed.json','application/json; charset=utf-8']};
const server=http.createServer((req,res)=>{const file=allowed[req.url];if(!file){res.writeHead(404).end();return;}res.setHeader('Content-Type',file[1]);res.end(fs.readFileSync(path.join(root,'tools/browser_fixtures',file[0])));});
(async()=>{
 let browser;const result={host_only:true,media_playback_tested:false,status:'blocked'};
 try{
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
  const origin='http://127.0.0.1:'+server.address().port;
  browser=await pw.webkit.launch({headless:true});result.engine='Playwright WebKit '+browser.version();
  const context=await browser.newContext({viewport:{width:1000,height:900}});
  await context.route('**/*',route=>new URL(route.request().url()).origin===origin?route.continue():route.abort());
  const page=await context.newPage();const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  await page.goto(origin+'/webapps.html',{timeout:15000});
  result.fixture=await page.evaluate(()=>window.webAppCheckPromise);
  result.script_errors=errors;result.status=result.fixture.passed&&!errors.length?'passed':'failed';
  await page.screenshot({path:path.join(out,'webapps.png')});
 }catch(error){result.error=String(error);}
 finally{
  if(browser)await browser.close();
  if(server.listening)await new Promise(resolve=>server.close(resolve));
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify(result,null,2));if(result.status!=='passed')process.exitCode=1;
 }
})();
