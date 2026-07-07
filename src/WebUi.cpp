#include "WebUi.h"

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

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
  body{font-family:system-ui,sans-serif;background:#15171c;color:#e8e8e8;margin:0;padding:1rem;max-width:820px;margin:auto}
  h1{font-size:1.25rem}
  .status{display:flex;flex-wrap:wrap;gap:.5rem 1rem;background:#1e2128;border-radius:8px;padding:.75rem 1rem;margin:.5rem 0 1rem}
  .status div{font-size:.85rem}
  .status b{color:#9ad}
  .card{background:#1e2128;border-radius:8px;padding:.75rem 1rem;margin:.6rem 0}
  output{font-weight:bold;color:#9ad}
  .scroll{overflow-x:auto}
  .rack{display:flex;gap:.4rem;width:max-content;margin:.6rem auto 0;padding:.2rem}
  .pair{display:flex;flex-direction:column;align-items:center;background:#262a33;border-radius:8px;padding:.5rem .25rem;flex:0 0 auto}
  .arclbl{font-size:.8rem;color:#9ad;margin-bottom:.4rem;white-space:nowrap}
  .sls{display:flex;gap:.2rem}
  .sl{display:flex;flex-direction:column;align-items:center}
  /* accent-color paints the filled region below the knob (and the knob): motor speed blue. */
  .sl input[type=range]{writing-mode:vertical-lr;direction:rtl;-webkit-appearance:slider-vertical;width:1.4rem;height:300px;margin:0;accent-color:#4a90d9}
  /* Bulb brightness fills warm yellow instead. */
  .sl input[type=range][id^=brightness]{accent-color:#f5c518}
  /* Fixed width so the value/Hz digits changing (0..100, 0..4000) never reflow the row. */
  .cap{font-size:.7rem;margin-top:.4rem;text-align:center;font-variant-numeric:tabular-nums;white-space:nowrap;color:#bbb;width:3rem}
  .dot{display:inline-block;width:.6rem;height:.6rem;border-radius:50%;margin-right:.3rem;vertical-align:middle}
  .ok{background:#3c6}.warn{background:#fb4}.bad{background:#e54}
  summary{cursor:pointer}
  .wrow{display:flex;gap:.5rem;margin:.6rem 0;flex-wrap:wrap;align-items:center}
  input[type=text],input[type=password]{background:#15171c;border:1px solid #444;border-radius:6px;color:#e8e8e8;padding:.4rem .6rem}
  button{background:#2b3140;border:1px solid #445;border-radius:6px;color:#e8e8e8;padding:.4rem .8rem;cursor:pointer}
  #nets{flex-basis:100%;font-size:.85rem}
  .net{padding:.3rem .5rem;border-radius:6px;cursor:pointer;font-variant-numeric:tabular-nums}
  .net:hover{background:#2b3140}
  #wmsg{font-size:.85rem;color:#bbb}
  #seqinfo{font-size:.85rem;color:#bbb}
  /* Fixed width so the ticking digits never shift the header text. */
  #seqtime{display:inline-block;width:6.5rem;font-size:.85rem;color:#9ad;font-variant-numeric:tabular-nums;white-space:nowrap}
  #seqcv{width:100%;height:132px;display:block;margin-top:.5rem}
</style></head><body>
<h1>Slow Arc Controller</h1>
<div class="status" id="status">connecting…</div>
<div class="card" id="seqcard" style="display:none">
 <b>Sequence</b> <span id="seqinfo"></span> <span id="seqtime"></span>
 <canvas id="seqcv" height="132"></canvas>
</div>
<div class="card">
 <b>Set-points</b> — motor speed (M) &amp; bulb brightness (B), 0..100
 <div class="scroll"><div class="rack" id="rack"></div></div>
</div>
<details class="card" id="wifi">
 <summary><b>WiFi setup</b> — <span id="wsum">…</span></summary>
 <div class="wrow"><button onclick="scan()">Scan networks</button><div id="nets"></div></div>
 <div class="wrow">
  <input type="text" id="wssid" placeholder="SSID" autocapitalize="off" autocorrect="off">
  <input type="password" id="wpass" placeholder="password">
  <button onclick="wsave()">Join</button>
 </div>
 <div id="wmsg"></div>
</details>
<script>
const N=6,MAXHZ=4000,MINHZ=60; // mirror MOTOR_MAX/MIN_SPEED_HZ in config.h
// Sliders are 0..100 for ease of use; the model/NVS store 0..255, so convert
// at the boundary (send/poll). Hz is computed from the 0..255 value the firmware
// actually maps (MotorController: 0->stop, else MIN+(MAX-MIN)*(v-1)/254).
const to255=p=>Math.round(p*255/100), to100=v=>Math.round(v*100/255);
function hz(p){const v=to255(p);return v>0?Math.round(MINHZ+(MAXHZ-MINHZ)*(v-1)/254):0}
function build(){
  const rack=document.getElementById('rack');
  for(let i=0;i<N;i++){
    rack.insertAdjacentHTML('beforeend',
      `<div class="pair"><div class="arclbl">Arc ${i+1}</div><div class="sls">`+
       `<div class="sl"><input type="range" min="0" max="100" value="0" id="speed${i}">`+
         `<div class="cap">M <output>0</output>%<br><span class="hz">0</span> Hz</div></div>`+
       `<div class="sl"><input type="range" min="0" max="100" value="0" id="brightness${i}">`+
         `<div class="cap">B <output>0</output>%</div></div>`+
      `</div></div>`);
  }
  for(let i=0;i<N;i++)for(const kind of ['speed','brightness']){
    const sl=document.getElementById(kind+i);
    sl.addEventListener('input',()=>cap(kind,i));
    sl.addEventListener('change',()=>send(i));
  }
}
function cap(kind,i){
  const sl=document.getElementById(kind+i),c=sl.parentElement;
  c.querySelector('output').textContent=sl.value;
  if(kind=='speed')c.querySelector('.hz').textContent=hz(+sl.value);
}
function send(i){
  const b=to255(+document.getElementById('brightness'+i).value);
  const s=to255(+document.getElementById('speed'+i).value);
  fetch(`/api/set?ch=${i}&brightness=${b}&speed=${s}`,{method:'POST'});
}
async function scan(){
  const nets=document.getElementById('nets');nets.textContent='scanning…';
  for(let t=0;t<15;t++){
    try{
      const r=await fetch('/api/scan');
      if(r.status==200){
        const l=await r.json();nets.textContent=l.length?'':'no networks found';
        l.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
          if(!n.ssid)return;                       // skip hidden networks
          const d=document.createElement('div');d.className='net';
          d.textContent=`${n.ssid}  (${n.rssi} dBm)${n.open?'':' \u{1F512}'}`;
          d.onclick=()=>{document.getElementById('wssid').value=n.ssid;
                         document.getElementById('wpass').focus();};
          nets.append(d);});
        return;
      }
    }catch(e){}
    await new Promise(x=>setTimeout(x,1000));
  }
  nets.textContent='scan timed out';
}
function wsave(){
  const s=document.getElementById('wssid').value,p=document.getElementById('wpass').value;
  if(!s)return;
  fetch(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`,{method:'POST'});
  document.getElementById('wmsg').textContent=
    `joining "${s}"… watch the status line. If it fails, the SlowArc-Setup AP stays up — rejoin it and retry.`;
}
// --- Sequence timeline (Performance mode) ---------------------------------
// Cue values are scales of the set-points; the plot shows WHEN each arc moves.
// The playhead position comes from seq_t each poll and free-runs between polls.
let seq=null,seqT=0,seqAt=0,seqRun=false;
async function loadSeq(){
  try{seq=await (await fetch('/api/sequence')).json();drawSeq();}catch(e){}
}
function fmt(ms){const s=Math.floor(ms/1000);return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')}
function drawSeq(){
  if(!seq||!seq.len)return;
  const cv=document.getElementById('seqcv');
  if(cv.width!==cv.clientWidth)cv.width=cv.clientWidth;   // match CSS pixel width
  const ctx=cv.getContext('2d'),W=cv.width,H=cv.height,laneH=H/6,x=t=>t/seq.len*W;
  ctx.clearRect(0,0,W,H);
  for(let ch=0;ch<6;ch++){
    const y0=(ch+1)*laneH-2,hMax=laneH-6;
    // bulb profile: filled warm yellow
    ctx.beginPath();ctx.moveTo(x(seq.cues[0].t),y0);
    seq.cues.forEach(c=>ctx.lineTo(x(c.t),y0-c.b[ch]/255*hMax));
    ctx.lineTo(W,y0);ctx.closePath();
    ctx.fillStyle='rgba(245,197,24,.4)';ctx.fill();
    // motor profile: blue line
    ctx.beginPath();
    seq.cues.forEach((c,i)=>{const px=x(c.t),py=y0-c.s[ch]/255*hMax;i?ctx.lineTo(px,py):ctx.moveTo(px,py)});
    ctx.strokeStyle='#4a90d9';ctx.lineWidth=1.5;ctx.stroke();
    // lane divider + arc number
    ctx.strokeStyle='#333';ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(0,ch*laneH+.5);ctx.lineTo(W,ch*laneH+.5);ctx.stroke();
    ctx.fillStyle='#889';ctx.font='9px system-ui';ctx.fillText(ch+1,3,ch*laneH+10);
  }
  if(seqRun){
    const t=(seqT+(performance.now()-seqAt))%seq.len;
    ctx.strokeStyle='#e54';ctx.lineWidth=1.5;
    ctx.beginPath();ctx.moveTo(x(t),0);ctx.lineTo(x(t),H);ctx.stroke();
    document.getElementById('seqinfo').textContent='— running';
    document.getElementById('seqtime').textContent=`${fmt(t)} / ${fmt(seq.len)}`;
  }else{
    document.getElementById('seqinfo').textContent='— stopped (press the sequence button)';
    document.getElementById('seqtime').textContent=`loop ${fmt(seq.len)}`;
  }
}
setInterval(()=>{if(seq&&document.getElementById('seqcard').style.display!=='none')drawSeq()},250);
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
    document.getElementById('wsum').textContent=
      `${d.wifi}${d.ssid?' · '+d.ssid:''}${d.ip?' · '+d.ip:''}`;
    const sc=document.getElementById('seqcard');
    sc.style.display=(d.mode==='Performance')?'':'none';
    seqRun=d.running;seqT=d.seq_t;seqAt=performance.now();
    if(d.mode==='Performance'&&!seq)loadSeq();
    for(let i=0;i<N;i++){
      for(const kind of ['brightness','speed']){
        const sl=document.getElementById(kind+i);
        if(document.activeElement!==sl)sl.value=to100(d[kind][i]);
        cap(kind,i);
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

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}
}  // namespace

void WebUi::begin(ChannelModel* model, ConfigStore* store,
                  std::function<String()> stateJson,
                  std::function<String()> sequenceJson,
                  std::function<void(const String&, const String&)> onWifiCredentials) {
  model_        = model;
  store_        = store;
  stateJson_    = stateJson;
  sequenceJson_ = sequenceJson;
  wifiCreds_    = onWifiCredentials;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", kIndexHtml);
  });

  server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "application/json", stateJson_ ? stateJson_() : "{}");
  });

  server.on("/api/sequence", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "application/json", sequenceJson_ ? sequenceJson_() : "{}");
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

  // GET /api/scan — asynchronous WiFi scan. 202 while running, 200 + list done.
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {                 // no scan yet — start one
      // Scanning needs the STA interface; alongside an active AP that's AP+STA.
      if (!(WiFi.getMode() & WIFI_MODE_STA)) WiFi.mode(WIFI_AP_STA);
      WiFi.scanNetworks(/*async=*/true);
      req->send(202, "application/json", "{\"scanning\":true}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {
      req->send(202, "application/json", "{\"scanning\":true}");
      return;
    }
    String j = "[";
    for (int i = 0; i < n; ++i) {
      if (i) j += ',';
      j += "{\"ssid\":\"";
      j += jsonEscape(WiFi.SSID(i));
      j += "\",\"rssi\":";
      j += WiFi.RSSI(i);
      j += ",\"open\":";
      j += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
      j += '}';
    }
    j += ']';
    WiFi.scanDelete();                           // free results for the next scan
    req->send(200, "application/json", j);
  });

  // POST /api/wifi?ssid=..&pass=.. — persist credentials and try to join.
  server.on("/api/wifi", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid")) { req->send(400, "text/plain", "missing ssid"); return; }
    String ssid = req->getParam("ssid")->value();
    String pass = req->hasParam("pass") ? req->getParam("pass")->value() : String();
    if (wifiCreds_) wifiCreds_(ssid, pass);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    // Captive portal: while the SoftAP is up, bounce unknown URLs (the OS
    // connectivity probes land here) to the UI so joining the AP pops the page.
    if (WiFi.getMode() & WIFI_MODE_AP) {
      req->redirect("http://" + WiFi.softAPIP().toString() + "/");
      return;
    }
    req->send(404, "text/plain", "not found");
  });

  server.begin();
}
