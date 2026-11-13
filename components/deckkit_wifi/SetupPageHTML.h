// SetupPage's page: everything inline, since a phone on the setup network has no internet.
// Adapted from ESPDeck's setup page (Wi-Fi only: no device name, pairing or factory reset).
// The product's name comes from /status.
#pragma once

namespace SetupPageHTML {
	inline constexpr char kPage[] = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>Setup</title>
<style>
:root{--bg:#f2f2f7;--card:#fff;--text:#1c1c1e;--muted:#6e6e73;--line:#d8d8dd;--field:#f5f5f8;--accent:#0a7cff;--ok:#2fb350;--busy:#ff9f0a;--bad:#ff3b30}
@media (prefers-color-scheme:dark){:root{--bg:#000;--card:#1c1c1e;--text:#f5f5f7;--muted:#98989f;--line:#3a3a3c;--field:#2c2c2e;--accent:#3d9bff}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;-webkit-text-size-adjust:100%}
main{max-width:460px;margin:0 auto;padding:28px 16px 48px}
h1{font-size:28px;letter-spacing:-.02em;margin:0}
.sub{color:var(--muted);margin:2px 0 20px}
.card{background:var(--card);border-radius:16px;padding:18px;margin-bottom:16px;box-shadow:0 1px 2px rgba(0,0,0,.06)}
.status{display:flex;gap:12px}
.dot{width:12px;height:12px;border-radius:50%;margin-top:6px;flex:none;background:var(--muted)}
.dot.ok{background:var(--ok)}.dot.bad{background:var(--bad)}
.dot.busy{background:var(--busy);animation:pulse .9s ease-in-out infinite alternate}
@keyframes pulse{to{opacity:.3}}
.title{font-weight:600}
label{display:block;font-size:13px;font-weight:600;color:var(--muted);margin:16px 0 6px}
form>label:first-child{margin-top:0}
input,select{width:100%;min-width:0;font:inherit;color:inherit;background:var(--field);border:1px solid var(--line);border-radius:10px;padding:11px 12px;-webkit-appearance:none;appearance:none}
select{background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 18px) 50%,calc(100% - 13px) 50%;background-size:5px 5px;background-repeat:no-repeat;padding-right:32px}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:-1px}
.row{display:flex;gap:8px}.row>:first-child{flex:1}
button{font:inherit;font-weight:600;border:0;border-radius:12px;padding:13px 16px;width:100%;cursor:pointer}
.primary{background:var(--accent);color:#fff;margin-top:20px}
.secondary{background:var(--field);color:var(--accent);border:1px solid var(--line)}
.small{width:auto;padding:0 14px;font-size:15px;white-space:nowrap}
button:disabled{opacity:.5;cursor:default}
.hint{font-size:14px;color:var(--muted);margin:4px 0 0}
.error{color:var(--bad);margin-top:12px}
.hidden{display:none}
</style></head><body><main>
<h1 id="product">Setup</h1>
<p class="sub" id="devname">&nbsp;</p>
<div class="card status"><div class="dot" id="dot"></div><div><div class="title" id="stext">Checking&hellip;</div><div class="hint" id="sdetail"></div></div></div>
<form class="card" id="form" autocomplete="off">
<label for="net">Wi-Fi network</label>
<div class="row"><select id="net"></select><button type="button" class="secondary small" id="rescan">Rescan</button></div>
<div id="manualbox" class="hidden"><label for="ssid">Network name</label><input id="ssid" maxlength="32" autocapitalize="none" autocorrect="off" spellcheck="false"></div>
<div id="passbox"><label for="pass">Password</label>
<div class="row"><input id="pass" type="password" maxlength="64" autocapitalize="none" autocorrect="off" spellcheck="false"><button type="button" class="secondary small" id="show">Show</button></div></div>
<p class="hint error hidden" id="formerr"></p>
<button class="primary" id="save" type="submit">Save &amp; Connect</button>
</form>
<div class="card hidden" id="exitcard"><button class="secondary" id="exit" type="button">Exit Setup</button><p class="hint">Keeps the current network and returns the deck to normal use.</p></div>
<p class="hint" id="fw" style="text-align:center"></p>
</main>
<script>
const $=id=>document.getElementById(id),OTHER='\u0001other',sleep=ms=>new Promise(r=>setTimeout(r,ms));
let st={},nets=[],saveError='';
function bars(r){const n=r>-60?4:r>-70?3:r>-80?2:1;return '▂▄▆█'.slice(0,n)+'▁'.repeat(4-n)}
function opt(v,t){const o=document.createElement('option');o.value=v;o.textContent=t;$('net').appendChild(o)}
function fillNets(){
 const sel=$('net'),prev=sel.value;sel.innerHTML='';
 nets.forEach(n=>opt(n.ssid,bars(n.rssi)+'  '+n.ssid+(n.secure?'':'  (open)')));
 opt(OTHER,'Other network…');
 if([...sel.options].some(o=>o.value===prev))sel.value=prev;
 update();
}
function update(){
 const v=$('net').value,n=nets.find(n=>n.ssid===v);
 $('manualbox').classList.toggle('hidden',v!==OTHER);
 $('passbox').classList.toggle('hidden',!!(n&&!n.secure));
}
function err(t){$('formerr').textContent=t||'';$('formerr').classList.toggle('hidden',!t)}
async function scan(refresh){
 $('rescan').disabled=true;
 try{for(let i=0;i<30;i++){
  const r=await(await fetch('/scan'+(refresh&&!i?'?refresh=1':''))).json();
  nets=r.networks||[];fillNets();if(!r.scanning)break;await sleep(1000);
 }}catch(e){}
 $('rescan').disabled=false;
}
function render(){
 document.title=st.product+' Setup';$('product').textContent=st.product+' Setup';$('devname').textContent=st.name;
 let c='',t,d='';
 if(st.leaving){c='ok';t='Connected to '+st.ssid;d=st.product+' is at '+st.ip+' and is leaving setup mode. You can close this page.'}
 else if(st.connected){c='ok';t='Connected to '+st.ssid;d='IP address '+st.ip}
 else if(st.connecting){c='busy';t='Connecting to '+st.ssid+'…'}
 else if(st.configured){c=st.error?'bad':'busy';t='Not connected to '+st.ssid;d=st.error||'Still trying…'}
 else{t='Not set up yet';d='Choose a Wi-Fi network below.'}
 $('dot').className='dot '+c;$('stext').textContent=t;$('sdetail').textContent=d;
 $('exitcard').classList.toggle('hidden',!st.canExit||st.leaving);
 $('fw').textContent='Firmware '+st.firmware;
 if(st.saveError!==saveError){saveError=st.saveError;err(saveError)}
}
async function poll(){try{st=await(await fetch('/status')).json();render()}catch(e){}setTimeout(poll,1500)}
$('net').onchange=update;
$('rescan').onclick=()=>scan(true);
$('show').onclick=()=>{const p=$('pass'),s=p.type==='password';p.type=s?'text':'password';$('show').textContent=s?'Hide':'Show'};
$('form').onsubmit=async e=>{
 e.preventDefault();err('');
 const v=$('net').value,ssid=v===OTHER?$('ssid').value:v,pass=$('passbox').classList.contains('hidden')?'':$('pass').value;
 if(!ssid)return err('Choose or enter a network.');
 if(pass&&pass.length<8)return err('Wi-Fi passwords have at least 8 characters.');
 $('save').disabled=true;
 try{
  const r=await(await fetch('/save',{method:'POST',body:new URLSearchParams({ssid,password:pass})})).json();
  if(r.ok)$('pass').value='';else err(r.error||'Couldn’t save.');
 }catch(e){err('Couldn’t reach the device.')}
 $('save').disabled=false;
};
$('exit').onclick=async()=>{
 $('exit').disabled=true;
 try{const r=await(await fetch('/exit',{method:'POST'})).json();
  if(r.ok){$('stext').textContent='Leaving setup mode';$('sdetail').textContent='You can close this page.';$('exitcard').classList.add('hidden');return}
 }catch(e){}
 $('exit').disabled=false;
};
poll();scan(false);
</script></body></html>
)HTML";
}
