#include "WebUi.h"

#include <ESPAsyncWebServer.h>

#include "config.h"

namespace {
AsyncWebServer server(80);

// Single-page UI. Vanilla JS, no external assets, so it works on the SoftAP with
// no internet. Polls /api/state for live status and POSTs slider changes.
const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Slow Arc Controller</title>
<style>
  :root{color-scheme:dark}
  body{font-family:system-ui,sans-serif;background:#15171c;color:#e8e8e8;margin:0;padding:1rem;max-width:680px;margin:auto}
  h1{font-size:1.25rem}
  .status{display:flex;flex-wrap:wrap;gap:.5rem 1rem;background:#1e2128;border-radius:8px;padding:.75rem 1rem;margin:.5rem 0 1rem}
  .status div{font-size:.85rem}
  .status b{color:#9ad}
  .ch{background:#1e2128;border-radius:8px;padding:.75rem 1rem;margin:.6rem 0}
  .ch h2{font-size:.95rem;margin:.1rem 0 .6rem}
  .row{display:flex;align-items:center;gap:.6rem;margin:.35rem 0}
  .row label{width:5.5rem;font-size:.85rem;color:#bbb}
  .row input[type=range]{flex:1}
  .row output{width:2.5rem;text-align:right;font-variant-numeric:tabular-nums}
  .dot{display:inline-block;width:.6rem;height:.6rem;border-radius:50%;margin-right:.3rem;vertical-align:middle}
  .ok{background:#3c6}.warn{background:#fb4}.bad{background:#e54}
</style></head><body>
<h1>Slow Arc Controller</h1>
<div class="status" id="status">connecting…</div>
<div id="channels"></div>
<script>
const N=6; let pending={};
function el(t,a={},...k){const e=document.createElement(t);for(const[p,v]of Object.entries(a))e[p]=v;k.forEach(c=>e.append(c));return e;}
function build(){
  const root=document.getElementById('channels');
  for(let i=0;i<N;i++){
    const card=el('div',{className:'ch'},el('h2',{textContent:'Slow Arc '+(i+1)}));
    for(const kind of ['brightness','speed']){
      const out=el('output',{textContent:'0'});
      const slider=el('input',{type:'range',min:0,max:255,value:0,id:kind+i});
      slider.addEventListener('input',()=>{out.textContent=slider.value;});
      slider.addEventListener('change',()=>send(i));
      card.append(el('div',{className:'row'},el('label',{textContent:kind}),slider,out));
    }
    root.append(card);
  }
}
function send(i){
  const b=document.getElementById('brightness'+i).value;
  const s=document.getElementById('speed'+i).value;
  fetch(`/api/set?ch=${i}&brightness=${b}&speed=${s}`,{method:'POST'});
}
async function poll(){
  try{
    const r=await fetch('/api/state'); const d=await r.json();
    const hl=d.fault?`<span class="dot bad"></span>FAULT ${d.fault}`
       :`<span class="dot ok"></span>healthy`;
    document.getElementById('status').innerHTML=
      `<div>${hl}</div>`+
      `<div>Mode: <b>${d.mode}</b></div>`+
      `<div>Sequence: <b>${d.running?'running':'stopped'}</b></div>`+
      `<div>WiFi: <b>${d.wifi}</b> ${d.ip?('('+d.ip+')'):''}</div>`+
      `<div>Uptime: <b>${d.uptime}s</b></div>`;
    for(let i=0;i<N;i++){
      for(const kind of ['brightness','speed']){
        const sl=document.getElementById(kind+i);
        if(document.activeElement!==sl){ sl.value=d[kind][i]; sl.nextElementSibling; }
        sl.parentElement.querySelector('output').textContent=sl.value;
      }
    }
  }catch(e){document.getElementById('status').textContent='offline';}
}
build(); poll(); setInterval(poll,2000);
</script></body></html>
)HTML";

uint8_t clamp255(long v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}
}  // namespace

void WebUi::begin(ChannelModel* model, ConfigStore* store,
                  std::function<String()> stateJson) {
  model_     = model;
  store_     = store;
  stateJson_ = stateJson;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", kIndexHtml);
  });

  server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "application/json", stateJson_ ? stateJson_() : "{}");
  });

  // POST /api/set?ch=N&brightness=V&speed=V  (either value optional)
  server.on("/api/set", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("ch")) { req->send(400, "text/plain", "missing ch"); return; }
    int ch = req->getParam("ch")->value().toInt();
    if (ch < 0 || ch >= NUM_CHANNELS) { req->send(400, "text/plain", "bad ch"); return; }

    bool changed = false;
    if (req->hasParam("brightness")) {
      model_->brightness[ch] = clamp255(req->getParam("brightness")->value().toInt());
      changed = true;
    }
    if (req->hasParam("speed")) {
      model_->speed[ch] = clamp255(req->getParam("speed")->value().toInt());
      changed = true;
    }
    if (changed) store_->markDirty();   // debounced commit happens in main loop
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "not found");
  });

  server.begin();
}
