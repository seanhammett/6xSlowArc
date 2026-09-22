#include "WebUi.h"

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config.h"
#include "MotorController.h"        // motorClampHz — one rule for a typed rate

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
  a{color:#9ad}
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
  /* Fixed width so the changing digits never reflow the row — and the pair's
     whole width comes from these two captions, so they are kept as narrow as
     "M 100%" allows. Six arcs must fit a laptop or tablet without scrolling. */
  .cap{font-size:.7rem;margin-top:.4rem;text-align:center;font-variant-numeric:tabular-nums;white-space:nowrap;color:#bbb;width:2.8rem}
  /* The Hz box sits under BOTH sliders rather than inside the motor caption, so
     a four-digit rate doesn't widen the motor column and every column with it. */
  .hzrow{font-size:.65rem;color:#bbb;margin-top:.3rem;white-space:nowrap}
  /* Typed step rate. Spinners are dropped: they cost width and 1 Hz nudges are
     useless. Qualified with [type=number] so it outranks the generic number-input
     rule further down — unqualified, .hzin loses on specificity and the box comes
     out at 4.5rem, which is what made the six columns overflow the screen. */
  input[type=number].hzin{width:2.6rem;font-size:.7rem;padding:.1rem .2rem;text-align:right;
        background:#15171c;border:1px solid #444;border-radius:4px;color:#e8e8e8;
        font-variant-numeric:tabular-nums;-moz-appearance:textfield}
  .hzin::-webkit-outer-spin-button,.hzin::-webkit-inner-spin-button{-webkit-appearance:none;margin:0}
  .pwr{margin-top:.4rem;font-size:.7rem;padding:.15rem .4rem;border-radius:6px;cursor:pointer;background:#1e2128;border:1px solid #445;width:3rem}
  .pwr.on{color:#3c6;border-color:#3c6}
  .pwr.off{color:#e54;border-color:#e54}
  .seg{display:flex;gap:.4rem;flex-wrap:wrap;align-items:center;margin-top:.5rem}
  .seg button.on{border-color:#9ad;color:#fff;background:#34405a}
  #playbtn{min-width:5.5rem}
  #playbtn.run{border-color:#3c6;color:#3c6}
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
  #seqcv,#pvcv,#edcv{width:100%;height:132px;display:block;margin-top:.5rem}
  /* Folder tabs: the active tab and its page share one outline. The tab sits 1px
     down over the page's top border and paints it out in the page colour, so the
     line runs unbroken round tab and page; an inactive tab sits lower and darker,
     behind the page's top edge. */
  .tabs{display:flex;align-items:flex-end;gap:.3rem;margin:.6rem 0 0;position:relative;z-index:1}
  .tab{background:#15171c;border:1px solid #2c313b;border-bottom:none;border-radius:8px 8px 0 0;color:#889;
       padding:.4rem 1rem;margin-top:.25rem;cursor:pointer;font-size:.9rem;position:relative}
  .tab:hover{color:#bbb}
  .tab.on{background:#1a1d23;color:#e8e8e8;border-color:#3d4453;margin-top:0;padding-bottom:.5rem;top:1px}
  .page{background:#1a1d23;border:1px solid #3d4453;border-radius:8px;padding:.1rem .6rem .4rem}
  #tabbed[data-tab="0"] .page{border-top-left-radius:0}   /* the first tab meets the page's corner */
  .page .card{background:#22262e}
  .page .pair{background:#2a2f39}
  /* Override card: the page's controls beside the front panel's, same height. */
  .ovgrid{display:grid;grid-template-columns:1fr 1fr;gap:.6rem;margin-top:.5rem}
  .ovbox{background:#1a1d23;border:1px solid #333a46;border-radius:8px;padding:.5rem .7rem;position:relative}
  .ovbox h3{font-size:.8rem;font-weight:600;color:#9ad;margin:0 0 .1rem}
  .ovbox .seg{margin-top:.35rem}
  .ovbox .note{font-size:.75rem;color:#889;margin-top:.4rem}
  /* The front panel as it sits: lit segment = the switch's position. Display only. */
  .ind{display:inline-block;padding:.25rem .6rem;border:1px solid #333a46;border-radius:6px;color:#667;font-size:.85rem}
  .ind.on{border-color:#9ad;color:#fff;background:#34405a}
  #phys.over{border-color:#e88}
  #phys.over .physin{opacity:.35}
  #physov{display:none;position:absolute;inset:0;align-items:center;justify-content:center;flex-direction:column;
          text-align:center;padding:.5rem;pointer-events:none}
  #phys.over #physov{display:flex}
  #physov b{color:#f99;font-size:.95rem;background:rgba(21,23,28,.85);padding:.2rem .6rem;border-radius:6px}
  #physov span{font-size:.72rem;color:#ccc;margin-top:.3rem;background:rgba(21,23,28,.85);padding:.1rem .4rem;border-radius:4px}
  /* Performance: the sequence and the audio that plays with it, in one lit box. */
  .perf{border:1px solid #4a6a9a;background:#232a36;box-shadow:0 0 0 3px rgba(74,144,217,.12)}
  .perf>.phead{font-size:1rem;color:#cfe0ff}
  .perf .ovbox{margin-top:.6rem}
  #loopsel{margin-top:.5rem}
  #loopsel .lbl{font-size:.85rem;color:#bbb}
  #loopnote{font-size:.8rem;color:#9ad}
  .lenwarn{background:#fb4;color:#15171c;border-radius:8px;padding:.6rem .8rem;margin:.6rem 0 0;font-size:1rem;font-weight:700;line-height:1.35}
  .lenwarn small{display:block;font-size:.8rem;font-weight:500;margin-top:.15rem}
  /* Confirm dialog, drawn as one of the page's own cards. */
  #cfm{position:fixed;inset:0;z-index:20;background:rgba(10,11,14,.7);display:none;align-items:center;justify-content:center;padding:1rem}
  #cfm.show{display:flex}
  #cfm .dlg{background:#1e2128;border:1px solid #3d4453;border-radius:10px;padding:1rem 1.1rem;max-width:22rem;width:100%;box-shadow:0 10px 30px rgba(0,0,0,.5)}
  #cfm .dlg b{display:block;font-size:1.05rem;margin-bottom:.4rem}
  #cfm .dlg p{font-size:.9rem;color:#bbb;margin:0 0 1rem}
  #cfm .btns{display:flex;gap:.5rem;justify-content:flex-end}
  button.danger{border-color:#e54;color:#fff;background:#6a2a26}
  .wifiask{flex-basis:100%;border-top:1px solid #333a46;padding-top:.45rem;color:#fb4}
  select{background:#15171c;border:1px solid #444;border-radius:6px;color:#e8e8e8;padding:.4rem .6rem}
  input[type=number]{background:#15171c;border:1px solid #444;border-radius:6px;color:#e8e8e8;padding:.25rem .4rem;width:4.5rem}
  .step{display:flex;gap:.4rem;align-items:center;margin:.25rem 0;font-size:.85rem;flex-wrap:wrap}
  .step label{display:flex;align-items:center;gap:.15rem;color:#bbb}
  #pvname,#selmsg,#edmsg,#edlen,#edmore{font-size:.85rem;color:#bbb}
  /* The step list scrolls inside the card; rows are added as it is scrolled. */
  #edwrap{max-height:24rem;overflow-y:auto}
  .step input[type=number]{width:5.5rem}
  .step input[type=number].rp{width:4rem}
  .step .idx{color:#667;width:2.2rem;text-align:right;font-variant-numeric:tabular-nums}
  .step .tm{color:#889;width:3.6rem;font-variant-numeric:tabular-nums}
  #ederr{font-size:.85rem;color:#fb4;white-space:pre-line}
  #autrack,#austat{font-size:.85rem;color:#bbb;margin-top:.3rem}
  .chk{display:flex;align-items:center;gap:.4rem;cursor:pointer}
  /* Audio is armed but the browser has not had the click it needs to make sound. */
  .banner{position:sticky;top:0;z-index:5;background:#fb4;color:#15171c;font-weight:600;
          padding:.5rem 1rem;border-radius:8px;margin-bottom:.5rem;cursor:pointer}
  /* Unreachable box: the controls are showing the last known values, which are
     not the box's values any more. Dim them and refuse input rather than let
     someone drag a slider that goes nowhere. */
  .stale{opacity:.4;pointer-events:none;filter:grayscale(.6)}
  #offmsg{color:#e54}
  /* Anything too narrow for the full-size rack (which needs ~675px) gets the
     compact one, which needs ~385px — so there is no width in between where the
     six columns have to be scrolled sideways. */
  @media (max-width:680px){
    body{padding:.5rem}
    .page{padding:.1rem .3rem .3rem}
    .page .card{padding:.75rem .55rem}   /* gives back the width the page's frame costs the rack */
    .ovgrid{gap:.4rem}
    .ovbox{padding:.45rem .5rem}
    .rack{gap:.15rem}
    .pair{padding:.4rem .1rem}
    .sls{gap:.1rem}
    .cap{width:1.6rem;font-size:.6rem}   /* below this the Hz row sets the width */
    .cap .k{display:block}          /* M / B onto their own line */
    .cap .pc{display:none}          /* the % is inferable; the width is not */
    .hzrow{font-size:.55rem}
    input[type=number].hzin{width:2rem;font-size:.6rem}
    .sl input[type=range]{width:1.1rem;height:230px}
    .pwr{width:2.6rem;font-size:.6rem;padding:.1rem .2rem}
  }
</style></head><body>
<div class="banner" id="aubanner" style="display:none">🔈 Audio is armed but locked — click anywhere on this page to enable it</div>
<h1>Slow Arc Controller</h1>
<div class="status" id="status">connecting…</div>
<div id="tabbed" data-tab="0">
<div class="tabs">
 <button class="tab on" onclick="tab(0)">Control</button>
 <button class="tab" onclick="tab(1)">Sequences</button>
</div>
<div id="tab0" class="page">
<div class="card" id="playcard">
 <b>Override physical controls</b>
 <div class="ovgrid">
  <div class="ovbox">
   <h3>This page</h3>
   <div class="seg">
    <button id="mdG" onclick="setMode('Gallery')">Gallery</button>
    <button id="mdP" onclick="setMode('Performance')">Performance</button>
   </div>
   <div class="seg"><button id="playbtn" onclick="playStop()">▶ Play</button></div>
   <div class="note">Highlighted: the box's mode. Play switches to Performance.</div>
  </div>
  <div class="ovbox" id="phys">
   <div class="physin">
    <h3>Front panel</h3>
    <div class="seg"><span class="ind" id="swG">Gallery</span><span class="ind" id="swP">Performance</span></div>
    <div class="note">Mode switch position</div>
    <div class="seg" style="font-size:.85rem">Sequence button: <span id="btnago">—</span></div>
   </div>
   <div id="physov"><b>overridden by this page</b><span id="physwhy"></span></div>
  </div>
 </div>
</div>
<div class="card perf" id="perfcard">
 <b class="phead">Performance</b>
 <div class="lenwarn" id="lenwarn" style="display:none"></div>
<div class="ovbox" id="seqcard" style="display:none">
 <b>Sequence</b> <span id="seqinfo"></span> <span id="seqtime"></span>
 <canvas id="seqcv" height="132"></canvas>
 <div class="seg" id="loopsel"><span class="lbl">After this run:</span>
  <button id="lpY" onclick="setLoop(1)">↻ Loop the performance</button>
  <button id="lpN" onclick="setLoop(0)">■ Stop after this run</button>
  <span id="loopnote"></span>
 </div>
</div>
<div class="ovbox" id="audiocard">
 <b>Audio</b> — plays a track from this computer in sync with the sequence
 <div class="wrow"><label class="chk"><input type="checkbox" id="auon" onchange="auToggle()"> Play audio on this browser</label></div>
 <div id="aubody" style="display:none">
  <div class="wrow">
   <input type="file" id="aufile" accept="audio/*,.m4a,.flac,.wav,.aif,.aiff,.mp3" onchange="auPick()" hidden>
   <button onclick="document.getElementById('aufile').click()">Choose track…</button>
   <button onclick="auForget()">Forget track</button>
   <label title="Positive plays the audio earlier, to make up for output latency">offset <input type="number" id="auoff" step="10" value="0" onchange="auOffset()"> ms</label>
  </div>
  <div class="wrow"><label class="chk" title="Nudges the sequence's clock by a few ms at a time to match the audio. Turn on in one browser only."><input type="checkbox" id="aufollow" onchange="auFollow()"> When necessary, apply time correction to Slow Arcs during playback</label></div>
  <div id="autrack"></div>
  <div id="austat"></div>
 </div>
</div>
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
</div>
<div id="tab1" class="page" style="display:none">
<div class="card">
 <b>Active sequence</b> — plays on the next Play or button push
 <div class="wrow"><select id="seqsel" onchange="selSeq()"></select><span id="selmsg"></span></div>
</div>
<div class="card">
 <b>Preview</b> <span id="pvname"></span>
 <canvas id="pvcv" height="132"></canvas>
</div>
<div class="card">
 <b>Create / edit</b> <span id="edmsg"></span>
 <div class="wrow">
  <input type="text" id="edname" placeholder="sequence name" maxlength="23">
  <label title="Where the piece loops. Blank = the last step plus its ramp">loop <input type="number" id="edloop" min="0" step="0.1" placeholder="auto" oninput="edDraw()"> s</label>
  <label title="Ramp given to added steps, and to CSV rows with a blank ramp">new-step ramp <input type="number" id="edramp" value="4" min="0" max="600" step="0.1"> s</label>
  <button onclick="edLoad()">Load selected</button>
 </div>
 <div class="wrow">
  <input type="file" id="edcsv" accept=".csv,text/csv,text/plain" onchange="edImport()" hidden>
  <button onclick="document.getElementById('edcsv').click()">Import CSV…</button>
  <button onclick="edExport()">Export CSV</button>
  <span id="edlen"></span>
 </div>
 <div id="ederr"></div>
 <div id="edwrap"><div id="edsteps"></div><div id="edmore"></div></div>
 <div class="wrow"><button id="edaddb" onclick="edAdd()">+ add step</button><button onclick="edSave()">Save</button></div>
 <canvas id="edcv" height="132"></canvas>
</div>
</div>
</div>
<div id="cfm" role="dialog" aria-modal="true" aria-labelledby="cfmt">
 <div class="dlg"><b id="cfmt"></b><p id="cfmm"></p>
  <div class="btns"><button id="cfmno">Cancel</button><button id="cfmok" class="danger"></button></div></div>
</div>
<script>
const N=6,MAXHZ=8000,MINHZ=60; // mirror MOTOR_MAX/MIN_SPEED_HZ in config.h
// Brightness slider is 0..100 for ease of use; the model/NVS store 0..255, so
// convert at the boundary (send/poll).
const to255=p=>Math.round(p*255/100), to100=v=>Math.round(v*100/255);
// Speed is Hz end to end — the box IS the set-point and the slider is a coarse
// way to move it. 0% = stopped, 1% = MINHZ, 100% = MAXHZ.
const pctToHz=p=>p<=0?0:Math.round(MINHZ+(MAXHZ-MINHZ)*(p-1)/99);
const hzToPct=h=>h<=0?0:Math.max(1,Math.min(100,Math.round(1+(h-MINHZ)*99/(MAXHZ-MINHZ))));
// Mirror motorClampHz(): anything between a stop and the floor becomes the floor.
const clampHz=h=>!isFinite(h)||h<=0?0:Math.max(MINHZ,Math.min(MAXHZ,Math.round(h)));
// A fetch that gives up. Without this a poll to a box whose power has been cut
// sits in the OS connect timeout for a minute or more, and the page happily goes
// on showing the last frame as if it were live.
function fetchT(url,init={},ms=4000){
  const c=new AbortController(),t=setTimeout(()=>c.abort(),ms);
  return fetch(url,{...init,signal:c.signal,cache:'no-store'}).finally(()=>clearTimeout(t));
}
const post=u=>fetchT(u,{method:'POST'}).catch(()=>{});   // fire-and-forget writes
function build(){
  const rack=document.getElementById('rack');
  for(let i=0;i<N;i++){
    rack.insertAdjacentHTML('beforeend',
      `<div class="pair"><div class="arclbl">Arc ${i+1}</div><div class="sls">`+
       `<div class="sl"><input type="range" min="0" max="100" value="0" id="speed${i}">`+
         `<div class="cap"><span class="k">M</span> <output>0</output><span class="pc">%</span></div></div>`+
       `<div class="sl"><input type="range" min="0" max="100" value="0" id="brightness${i}">`+
         `<div class="cap"><span class="k">B</span> <output>0</output><span class="pc">%</span></div></div>`+
       `</div>`+
       `<div class="hzrow"><input type="number" class="hzin" id="hz${i}" min="0" max="${MAXHZ}" step="1" value="0"> Hz</div>`+
      `<button class="pwr on" id="pwr${i}" onclick="togglePwr(${i})">on</button></div>`);
  }
  for(let i=0;i<N;i++){
    const br=document.getElementById('brightness'+i);
    br.addEventListener('input',()=>cap('brightness',i));
    br.addEventListener('change',()=>send(i));
    const sp=document.getElementById('speed'+i);
    sp.addEventListener('input',()=>slid(i));        // drag: box follows the slider
    sp.addEventListener('change',()=>send(i));
    const hb=document.getElementById('hz'+i);
    hb.addEventListener('change',()=>typed(i));      // fires on Enter or blur
    hb.addEventListener('keydown',e=>{if(e.key=='Enter')hb.blur()});
  }
}
function cap(kind,i){
  const sl=document.getElementById(kind+i);
  sl.parentElement.querySelector('output').textContent=sl.value;
}
function slid(i){                                    // slider moved
  cap('speed',i);
  document.getElementById('hz'+i).value=pctToHz(+document.getElementById('speed'+i).value);
}
function typed(i){                                   // exact rate entered
  const hb=document.getElementById('hz'+i),h=clampHz(parseInt(hb.value,10));
  hb.value=h;                                        // show what the box will do
  document.getElementById('speed'+i).value=hzToPct(h);
  cap('speed',i);
  send(i);
}
function send(i){
  const b=to255(+document.getElementById('brightness'+i).value);
  const s=clampHz(parseInt(document.getElementById('hz'+i).value,10));
  post(`/api/set?ch=${i}&brightness=${b}&speed=${s}`);
}
function setPwr(i,on){
  const b=document.getElementById('pwr'+i);
  b.className='pwr '+(on?'on':'off');
  b.textContent=on?'on':'off';
}
function togglePwr(i){
  const on=!document.getElementById('pwr'+i).classList.contains('on');
  setPwr(i,on);                                  // optimistic; poll re-syncs
  post(`/api/arc?ch=${i}&on=${on?1:0}`);
}
// Mode + playback. Re-poll straight after so the card confirms at once rather
// than on the next 2 s tick; the poll is what the buttons show, never a guess.
let curRun=false;
function paintPlay(mode,running,ovr,sw,btnAgo){
  curRun=running;
  document.getElementById('mdG').classList.toggle('on',mode==='Gallery');
  document.getElementById('mdP').classList.toggle('on',mode==='Performance');
  const pb=document.getElementById('playbtn');
  pb.textContent=running?'■ Stop':'▶ Play';
  pb.classList.toggle('run',running);
  // Front panel: the switch's own position, which the box ignores while overridden.
  sw=sw||(ovr?(mode==='Gallery'?'Performance':'Gallery'):mode);
  document.getElementById('swG').classList.toggle('on',sw==='Gallery');
  document.getElementById('swP').classList.toggle('on',sw==='Performance');
  document.getElementById('btnago').textContent=btnAgo==null?'—'
    :btnAgo<0?'not pushed since power-up':`last pushed ${ago(btnAgo)} ago`;
  document.getElementById('phys').classList.toggle('over',ovr);
  document.getElementById('physwhy').textContent=ovr
    ?`The box is in ${mode} until the switch is next flipped`:'';
}
function ago(s){return s<60?s+' s':s<3600?Math.floor(s/60)+' min':Math.floor(s/3600)+' h'}
// "Are you sure?" in the page's own style. Resolves true on the red button;
// Cancel, Escape or a click outside the box resolve false.
function ask(title,msg,ok){
  const box=document.getElementById('cfm'),y=document.getElementById('cfmok'),n=document.getElementById('cfmno');
  document.getElementById('cfmt').textContent=title;
  document.getElementById('cfmm').textContent=msg;
  y.textContent=ok;box.classList.add('show');n.focus();
  return new Promise(res=>{
    const done=v=>{box.classList.remove('show');y.onclick=n.onclick=box.onclick=null;
                   document.removeEventListener('keydown',key,true);res(v)};
    const key=e=>{if(e.key==='Escape'){e.preventDefault();done(false)}};
    y.onclick=()=>done(true);n.onclick=()=>done(false);
    box.onclick=e=>{if(e.target===box)done(false)};
    document.addEventListener('keydown',key,true);
  });
}
const askStop=msg=>ask('Stop the performance?',msg,'Stop performance');
async function setMode(m){
  // Leaving Performance stops a running piece, so it gets the same warning as Stop.
  if(m==='Gallery'&&curRun&&curMode==='Performance'&&!await askStop(
    'The sequence is running. Switching to Gallery stops it and returns the arcs to their set-points.'))return;
  await post(`/api/mode?m=${m}`);poll();
}
async function playStop(){
  if(curRun){
    if(!await askStop('The sequence is running. Stopping it leaves the arcs holding wherever they are.'))return;
    await post('/api/run?on=0');poll();return;
  }
  // Starting from this page: start the audio inside the click (a user gesture,
  // so the browser allows it) instead of waiting for the next poll to see it.
  // (Play implies Performance on the box, so no mode check here.)
  if(!curRun&&AU.on&&AU.dur&&AU.primed){
    auStartAt(Math.max(0,AU.offset)/1000);AU.adoptUntil=performance.now()+2500;
  }
  await post('/api/run?on=1');poll();
}
async function scan(){
  const nets=document.getElementById('nets');nets.textContent='scanning…';
  for(let t=0;t<15;t++){
    try{
      const r=await fetchT('/api/scan');
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
  post(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`);
  document.getElementById('wmsg').textContent=
    `joining "${s}"… watch the status line — it shows the box's address and its .local name once joined. If it fails, the SlowArc-Setup AP stays up — rejoin it and retry.`;
}
// --- Sequence timeline (Performance mode) ---------------------------------
// Frame values are scales of the set-points; the plot shows WHEN each arc moves.
// The playhead position comes from seq_t each poll and free-runs between polls.
// seqName is the piece the timeline below IS; seqQueued is one selected while
// a piece was playing, which by design only takes over at the next press.
const MAXSTEPS=1000;              // mirror SEQ_MAX_STEPS in config.h
let seq=null,seqKey=0,tlCache=null,seqT=0,seqAt=0,seqRun=false,seqLive=false,seqName='',seqQueued='';
let seqLen=0,seqRunId=0,curMode='',seqLoop=true;
// Loop, or stop at the end of the pass in progress. Optimistic; the poll confirms.
function paintLoop(){
  document.getElementById('lpY').classList.toggle('on',seqLoop);
  document.getElementById('lpN').classList.toggle('on',!seqLoop);
}
async function setLoop(on){seqLoop=!!on;paintLoop();await post('/api/seq/loop?on='+on);poll();}
const stepsIn=a=>a.map(([t,m,r])=>({t,m,r}));   // API [[t_ms,mask,ramp_ms],...] -> [{t,m,r}]
async function loadSeq(){
  try{
    const d=await (await fetchT('/api/sequence')).json();
    d.steps=stepsIn(d.steps||[]);seq=d;seqKey++;drawSeq();
  }catch(e){}
}
function fmt(ms){const s=Math.floor(ms/1000);return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')}
// m:ss.s — step times are 0.1 s resolution.
function fmt1(ms){const d=Math.round(ms/100),s=Math.floor(d/10);return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')+'.'+(d%10)}
// Mirror of the firmware's frame rule (SeqFrame.h): each step ramps in over its
// own ramp, reaching its state at t; the piece starts on the last step's state,
// holds it after the last step, and loops at len. Same-time steps: the later
// one wins (state and ramp). All times ms.
function expand(len,steps){
  if(!steps.length||!len)return null;
  const st=[];
  for(const s of [...steps].sort((a,b)=>a.t-b.t)){if(st.length&&st[st.length-1].t===s.t)st.pop();st.push(s)}
  const mLast=st[st.length-1].m,pts=[{t:0,m:mLast}];
  let prevM=mLast,prevT=0;
  for(const s of st){
    pts.push({t:Math.max(prevT,s.t-s.r),m:prevM});
    pts.push({t:s.t,m:s.m});prevM=s.m;prevT=s.t;
  }
  pts.push({t:len,m:mLast});
  return {len,pts};
}
function paintMask(ctx,W,H,len,steps){
  ctx.clearRect(0,0,W,H);
  const ex=expand(len,steps);if(!ex||!ex.len)return;
  const laneH=H/6,x=t=>t/ex.len*W;
  for(let ch=0;ch<6;ch++){
    const y0=(ch+1)*laneH-2,hMax=laneH-6,yOf=p=>y0-((p.m>>ch)&1)*hMax;
    // bulb profile: filled warm yellow
    ctx.beginPath();ctx.moveTo(0,y0);
    ex.pts.forEach(p=>ctx.lineTo(x(p.t),yOf(p)));
    ctx.lineTo(W,y0);ctx.closePath();
    ctx.fillStyle='rgba(245,197,24,.4)';ctx.fill();
    // motor profile: blue line (bulb + motor move together)
    ctx.beginPath();
    ex.pts.forEach((p,i)=>{i?ctx.lineTo(x(p.t),yOf(p)):ctx.moveTo(x(p.t),yOf(p))});
    ctx.strokeStyle='#4a90d9';ctx.lineWidth=1.5;ctx.stroke();
    // lane divider + arc number
    ctx.strokeStyle='#333';ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(0,ch*laneH+.5);ctx.lineTo(W,ch*laneH+.5);ctx.stroke();
    ctx.fillStyle='#889';ctx.font='9px system-ui';ctx.fillText(ch+1,3,ch*laneH+10);
  }
}
function drawMask(id,len,steps){
  const cv=document.getElementById(id);
  if(cv.width!==cv.clientWidth)cv.width=cv.clientWidth;
  paintMask(cv.getContext('2d'),cv.width,cv.height,len,steps);
}
// The Control timeline is painted once per piece/size into an offscreen canvas;
// the 250 ms tick only redraws the playhead over it. (1000 steps x 6 lanes is
// too much to repaint four times a second.)
function tlLayer(W,H){
  const k=seqKey+'|'+W+'|'+H;
  if(!tlCache||tlCache.k!==k){
    const c=document.createElement('canvas');c.width=W;c.height=H;
    paintMask(c.getContext('2d'),W,H,seq.len,seq.steps);
    tlCache={k,c};
  }
  return tlCache.c;
}
function drawSeq(){
  if(!seq||!seq.len)return;
  const cv=document.getElementById('seqcv');
  if(cv.width!==cv.clientWidth)cv.width=cv.clientWidth;   // match CSS pixel width
  const ctx=cv.getContext('2d'),W=cv.width,H=cv.height,x=t=>t/seq.len*W;
  if(!W||!H)return;              // hidden (other tab / Gallery): nothing to draw into
  ctx.clearRect(0,0,W,H);
  ctx.drawImage(tlLayer(W,H),0,0);
  // Name what is drawn. The timeline is the piece the box actually holds, so
  // if a selection is only queued this says so rather than looking ignored.
  const nm=seqName?`“${seqName}”`:'';
  const q=seqQueued?` · “${seqQueued}” starts on the next press`:'';
  if(!seqLive){
    // Device unreachable: freeze the playhead at the last known position.
    if(seqRun){
      const t=seqT%seq.len;
      ctx.strokeStyle='#889';ctx.lineWidth=1.5;
      ctx.beginPath();ctx.moveTo(x(t),0);ctx.lineTo(x(t),H);ctx.stroke();
    }
    document.getElementById('seqinfo').textContent=`— ${nm} · device offline`;
    document.getElementById('seqtime').textContent='';
  }else if(seqRun){
    // Playing once, the playhead parks at the end rather than wrapping while the
    // box's stop is still on its way to the next poll.
    const e=seqT+(performance.now()-seqAt),t=seqLoop?e%seq.len:Math.min(e,seq.len);
    ctx.strokeStyle='#e54';ctx.lineWidth=1.5;
    ctx.beginPath();ctx.moveTo(x(t),0);ctx.lineTo(x(t),H);ctx.stroke();
    document.getElementById('seqinfo').textContent=`— ${nm} · running${q}`;
    document.getElementById('seqtime').textContent=`${fmt(t)} / ${fmt(seq.len)}`;
    document.getElementById('loopnote').textContent=seqLoop?'':`— stops in ${fmt(Math.max(0,seq.len-t))}`;
  }else{
    document.getElementById('loopnote').textContent='';
    document.getElementById('seqinfo').textContent=
      `— ${nm} · stopped (press the sequence button)${q}`;
    document.getElementById('seqtime').textContent=`loop ${fmt(seq.len)}`;
  }
}
setInterval(()=>{if(seq&&document.getElementById('seqcard').style.display!=='none')drawSeq()},250);
// --- Sequences tab: select / preview / create -------------------------------
function tab(n){
  document.getElementById('tab0').style.display=n?'none':'';
  document.getElementById('tab1').style.display=n?'':'none';
  document.querySelectorAll('.tab').forEach((b,i)=>b.classList.toggle('on',i===n));
  document.getElementById('tabbed').dataset.tab=n;
  if(n){loadSeqList();edRender();}
  else poll();      // coming back to Control: show the current piece, not a 2 s-old one
}
function esc(s){return s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';')}
async function loadSeqList(){
  try{
    const d=await (await fetchT('/api/seqs')).json();
    document.getElementById('seqsel').innerHTML=d.seqs.map(s=>
      `<option value="${s.i}"${s.i==d.active?' selected':''}>${esc(s.name)}</option>`).join('');
    loadPreview();
  }catch(e){}
}
async function loadPreview(){
  const i=document.getElementById('seqsel').value;
  try{
    const d=await (await fetchT('/api/seq?i='+i)).json(),st=stepsIn(d.steps);
    document.getElementById('pvname').textContent=
      `— ${d.name} · ${st.length} steps · loop ${fmt1(d.len)}`;
    drawMask('pvcv',d.len,st);
  }catch(e){}
}
async function selSeq(){
  await post('/api/seq/select?i='+document.getElementById('seqsel').value);
  const m=document.getElementById('selmsg');
  // Say which of the two things happened: it is live now, or it is armed for the
  // next press. Guessing wrong here is what makes the Control tab look stale.
  m.textContent=seqRun?'saved — starts on the next press of the sequence button'
                      :'saved — this is now the active sequence';
  setTimeout(()=>m.textContent='',6000);
  loadPreview();seq=null;   // control-tab timeline refetches on its next poll
  poll();                   // …and refresh the strip now rather than in 2 s
}
// --- Creator ---
// Steps are held in ms ({t,m,r}: time, arcs, ramp into that state); inputs show
// seconds to 0.1 s. Rows are built in batches as the list is scrolled — 1000
// rows x 6 checkboxes all at once is sluggish, especially on a phone.
let ed={steps:[{t:4000,m:1,r:4000}]},edShown=0;
const ED_BATCH=100;
const snap=s=>Math.max(0,Math.round((+s||0)*10))*100;         // seconds -> ms, 0.1 s
const snapR=s=>Math.min(600000,snap(s));                       // a ramp: 0..600 s
function edDefRamp(){return snapR(document.getElementById('edramp').value)}
// The step that closes the piece: latest in time (the later one of a tie).
function edLast(){let l=null;for(const s of ed.steps)if(!l||s.t>=l.t)l=s;return l}
// Loop length: the loop box, or when it's blank the last step plus its ramp.
function edAutoLoop(){const l=edLast();return l?l.t+l.r:0}
function edLoopMs(){const v=document.getElementById('edloop').value.trim();return v===''?edAutoLoop():snap(v)}
function edRow(i){
  const s=ed.steps[i],row=document.createElement('div');row.className='step';
  row.innerHTML=`<span class="idx">${i+1}</span>t <input type="number" class="tt" min="0" step="0.1" value="${s.t/1000}"> s `+
    `<span class="tm">${fmt1(s.t)}</span> arcs `+
    [0,1,2,3,4,5].map(ch=>`<label><input type="checkbox"${(s.m>>ch)&1?' checked':''}>${ch+1}</label>`).join('')+
    ` ramp <input type="number" class="rp" min="0" max="600" step="0.1" value="${s.r/1000}"> s`+
    ` <button>✕</button>`;
  const t=row.querySelector('.tt'),tm=row.querySelector('.tm'),rp=row.querySelector('.rp');
  t.oninput=()=>{s.t=snap(t.value);tm.textContent=fmt1(s.t);edDraw()};
  rp.oninput=()=>{s.r=snapR(rp.value);edDraw()};
  row.querySelectorAll('input[type=checkbox]').forEach((cb,ch)=>
    cb.onchange=()=>{s.m=cb.checked?s.m|(1<<ch):s.m&~(1<<ch);edDraw()});
  row.querySelector('button').onclick=()=>{ed.steps.splice(i,1);edRender(edShown)};
  return row;
}
function edMore(n=ED_BATCH){
  const box=document.getElementById('edsteps'),end=Math.min(ed.steps.length,edShown+n);
  const frag=document.createDocumentFragment();
  for(let i=edShown;i<end;i++)frag.append(edRow(i));
  box.append(frag);edShown=end;
  document.getElementById('edmore').textContent=edShown<ed.steps.length
    ?`showing ${edShown} of ${ed.steps.length} — scroll for more`:'';
}
function edRender(keep){
  document.getElementById('edsteps').innerHTML='';edShown=0;
  edMore(Math.max(ED_BATCH,keep||0));
  edDraw();
}
document.getElementById('edwrap').addEventListener('scroll',e=>{
  const w=e.target;
  if(edShown<ed.steps.length&&w.scrollTop+w.clientHeight>=w.scrollHeight-200)edMore();
});
function edInfo(){
  const n=ed.steps.length,l=edLast(),len=edLoopMs();
  document.getElementById('edloop').placeholder=`auto ${(edAutoLoop()/1000).toFixed(1)}`;
  let t=`${n} / ${MAXSTEPS} steps · loop ${fmt1(len)} (${(len/1000).toFixed(1)} s)`;
  if(l&&len<l.t)t+=' — ends before the last step';
  // Beside the loaded audio track, so the two can be made to match exactly.
  if(AU.dur){
    const tr=Math.round(AU.dur*10)*100;
    t+=` · track ${fmt1(tr)} (${(tr/1000).toFixed(1)} s)`+(Math.abs(tr-len)>1000?' — lengths differ':' ✓');
  }
  document.getElementById('edlen').textContent=t;
  document.getElementById('edaddb').disabled=n>=MAXSTEPS;
}
function edDraw(){drawMask('edcv',edLoopMs(),ed.steps);edInfo()}
function edAdd(){
  const n=ed.steps.length;if(n>=MAXSTEPS)return;
  const last=ed.steps[n-1];
  ed.steps.push({t:last?last.t+60000:4000,m:last?last.m:1,r:edDefRamp()});
  if(edShown===n){document.getElementById('edsteps').append(edRow(n));edShown++;edDraw();}
  else edRender(n+1);
  const w=document.getElementById('edwrap');w.scrollTop=w.scrollHeight;
}
async function edLoad(){
  const i=document.getElementById('seqsel').value;
  try{
    const d=await (await fetchT('/api/seq?i='+i)).json();
    ed={steps:stepsIn(d.steps)};
    document.getElementById('edname').value=d.name;
    document.getElementById('edloop').value=d.len/1000;
    document.getElementById('ederr').textContent='';
    edRender();
  }catch(e){}
}
async function edSave(){
  const name=document.getElementById('edname').value.trim();
  const loop=edLoopMs(),l=edLast();
  const m=document.getElementById('edmsg');
  if(!name||!ed.steps.length){m.textContent='— need a name and at least one step';return}
  if(ed.steps.length>MAXSTEPS){m.textContent=`— too many steps (max ${MAXSTEPS})`;return}
  if(!loop||loop<l.t){m.textContent='— the loop must end at or after the last step';return}
  // Stable sort: same-time steps keep their order (the later one wins on playback).
  const body=[...ed.steps].sort((a,b)=>a.t-b.t).map(s=>`${s.t}:${s.m}:${s.r}`).join(',');
  let r;
  try{
    r=await fetchT(`/api/seq/save?name=${encodeURIComponent(name)}&loop_ms=${loop}`,
                   {method:'POST',body,headers:{'Content-Type':'text/plain'}},10000);
  }catch(e){m.textContent='— save failed: no reply from the box';return}
  m.textContent=r.ok?'— saved (same name overwrites)':'— save failed: '+await r.text();
  if(r.ok){loadSeqList();seq=null;poll();}
}
// --- CSV import / export ------------------------------------------------------
// One row per step: time, the six arcs (1/0, x, on/off; blank = off) or a single
// mask column (0..63), then the ramp into that state in seconds. Times are
// seconds (12.5, "2,392.0") or m:ss.s. A header row is optional but names the
// columns (a column headed "ramp" is the ramp). An END row — a time plus the
// word END — sets the loop length; without one the loop closes at the last
// step plus its ramp. A blank ramp cell takes the editor's new-step ramp.
// Older files with no ramp column: every step gets the ramp END - last step.
// Parsed here in the browser; the box only ever sees the step list on Save.
const ON=/^(1|x|on|yes|y|true)$/i,OFF=/^(0|off|no|n|false|-|)$/i,ISEND=/^end$/i;
// "2,392.0" -> "2392.0": spreadsheets add thousands separators to big numbers.
const unsep=s=>/^\d{1,3}(,\d{3})+(\.\d+)?$/.test(s)?s.replace(/,/g,''):s;
function parseTime(s){
  s=unsep((s||'').trim());if(!s)return NaN;
  const p=s.split(':');if(p.length>3)return NaN;
  let v=0;
  for(const x of p){if(!/^\d+(\.\d+)?$/.test(x.trim()))return NaN;v=v*60+parseFloat(x)}
  return v;
}
function splitCsv(line,delim){
  const out=[];let cur='',q=false;
  for(let i=0;i<line.length;i++){
    const c=line[i];
    if(q){if(c=='"'){if(line[i+1]=='"'){cur+='"';i++}else q=false}else cur+=c}
    else if(c=='"')q=true;
    else if(c==delim){out.push(cur);cur=''}
    else cur+=c;
  }
  out.push(cur);
  return out.map(x=>x.trim());
}
function parseCsv(text,defRamp){
  const lines=text.replace(/^﻿/,'').split(/\r\n|\n|\r/);
  const first=lines.find(l=>l.trim())||'';
  // Some spreadsheet locales export ; or tab instead of commas.
  const delim=[',',';','\t'].reduce((a,d)=>first.split(d).length>first.split(a).length?d:a,',');
  const rows=[];
  lines.forEach((l,i)=>{if(l.trim()&&!l.trim().startsWith('#'))rows.push({n:i+1,c:splitCsv(l,delim)})});
  const errs=[],steps=[];let end=null,maskMode=null,rampCol=-1;
  if(rows.length&&isNaN(parseTime(rows[0].c[0]))&&!rows[0].c.some(x=>ISEND.test(x))){
    const h=rows.shift();                                        // header row
    rampCol=h.c.findIndex(x=>/ramp/i.test(x));
    maskMode=h.c.length>=2&&/mask/i.test(h.c[1]);
  }else{
    // No header: judge by width. time,mask[,ramp] is at most 3 wide with a
    // number in column 2; otherwise time + six arcs [+ ramp].
    const data=rows.filter(r=>!r.c.some(x=>ISEND.test(x)));
    const width=r=>{let w=r.c.length;while(w&&r.c[w-1]==='')w--;return w};
    const maxW=Math.max(0,...data.map(width));
    maskMode=data.length>0&&maxW<=3&&data.every(r=>/^\d+$/.test(r.c[1]||''));
    if(maskMode&&maxW===3)rampCol=2;
    if(!maskMode&&maxW>=8)rampCol=7;
  }
  for(const r of rows){
    const c=r.c,ei=c.findIndex(x=>ISEND.test(x));
    if(ei>=0){
      const tc=c.find((x,i)=>i!==ei&&!isNaN(parseTime(x)));
      if(tc===undefined){errs.push(`line ${r.n}: END row needs a time`);continue}
      if(end!==null){errs.push(`line ${r.n}: second END row`);continue}
      end=snap(parseTime(tc));continue;
    }
    if(end!==null){errs.push(`line ${r.n}: step after the END row`);continue}
    const ts=parseTime(c[0]);
    if(isNaN(ts)){errs.push(`line ${r.n}: bad time "${c[0]}"`);continue}
    let m=0,bad=false;
    if(maskMode){
      const v=c[1]||'';
      if(!/^\d+$/.test(v)||+v>63){errs.push(`line ${r.n}: mask "${v}" must be 0–63`);continue}
      m=+v;
    }else{
      for(let ch=0;ch<6;ch++){
        const v=c[ch+1]||'';
        if(ON.test(v))m|=1<<ch;
        else if(!OFF.test(v)){errs.push(`line ${r.n}: arc ${ch+1} value "${v}" (use 1/0, x, on/off)`);bad=true;break}
      }
      if(bad)continue;
    }
    let rp=defRamp;
    if(rampCol>=0&&(c[rampCol]||'')!==''){
      const v=parseTime(c[rampCol]);
      if(isNaN(v)||v>600){errs.push(`line ${r.n}: ramp "${c[rampCol]}" must be 0–600 s`);continue}
      rp=snap(v);
    }
    steps.push({t:snap(ts),m,r:rp});
  }
  if(!steps.length&&!errs.length)errs.push('no steps found');
  if(steps.length>MAXSTEPS)errs.push(`${steps.length} steps — the maximum is ${MAXSTEPS}`);
  steps.sort((a,b)=>a.t-b.t);
  let legacyRamp=null;
  if(end!==null&&steps.length){
    const lastT=steps[steps.length-1].t;
    if(end<lastT)errs.push(`END at ${fmt1(end)} is before the last step (${fmt1(lastT)})`);
    else if(rampCol<0){
      // Older layout: END - last step was the one ramp for the whole piece.
      legacyRamp=end-lastT;
      if(legacyRamp>600000)
        errs.push(`END at ${fmt1(end)} gives a ramp of ${(legacyRamp/1000).toFixed(1)} s — add a ramp column, or keep END within 600 s of the last step`);
      else steps.forEach(s=>s.r=legacyRamp);
    }
  }
  return {steps,loop:end,legacyRamp,errs};
}
async function edImport(){
  const inp=document.getElementById('edcsv'),f=inp.files[0];inp.value='';
  if(!f)return;
  const er=document.getElementById('ederr'),m=document.getElementById('edmsg');
  const {steps,loop,legacyRamp,errs}=parseCsv(await f.text(),edDefRamp());
  if(errs.length){
    er.textContent=`Not imported — ${errs.length} problem${errs.length>1?'s':''}:\n`+
      errs.slice(0,12).join('\n')+(errs.length>12?`\n…and ${errs.length-12} more`:'');
    return;
  }
  er.textContent='';
  ed={steps};
  const nm=document.getElementById('edname');
  if(!nm.value.trim())nm.value=f.name.replace(/\.[^.]*$/,'').slice(0,23);
  document.getElementById('edloop').value=loop!==null?loop/1000:'';
  let note=`— imported ${steps.length} steps from ${f.name}`;
  if(loop!==null)note+=` · loop ${fmt1(loop)} from the END row`;
  if(legacyRamp!==null)note+=` · no ramp column, so every step ramps ${legacyRamp/1000} s (END − last step)`;
  m.textContent=note+' · not saved yet';
  edRender();
}
function edExport(){
  const st=[...ed.steps].sort((a,b)=>a.t-b.t);if(!st.length)return;
  const rows=['time,arc1,arc2,arc3,arc4,arc5,arc6,ramp'];
  for(const s of st)rows.push([(s.t/1000).toFixed(1),...[0,1,2,3,4,5].map(ch=>(s.m>>ch)&1),(s.r/1000).toFixed(1)].join(','));
  rows.push(`${(edLoopMs()/1000).toFixed(1)},END`);
  const name=(document.getElementById('edname').value.trim()||'sequence').replace(/[^\w\- ]+/g,'_');
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([rows.join('\n')+'\n'],{type:'text/csv'}));
  a.download=name+'.csv';document.body.append(a);a.click();
  setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove()},1000);
}
// --- Audio -------------------------------------------------------------------
// The track is a file on THIS computer, never sent to the box. The audio is the
// master and is never touched while it plays — no seeks, no rate changes, both of
// which are audible. It is only started (when the sequence starts, or at a wrap
// when the track's length differs from the loop's) and stopped. Instead, with
// "keep the lights in step" on, the page measures the box against the audio each
// poll and nudges the box's sequence clock (POST /api/seq/nudge): the lights
// shifting a few ms is invisible. Off, the two free-run from a common start.
// Opt-in per browser, so phones used as remotes stay silent. The chosen file is
// kept in IndexedDB so a reload doesn't need it picked again.
const AU={on:false,follow:true,el:null,url:'',name:'',size:0,dur:0,saved:false,note:'',
  primed:false,playing:false,lastRun:-1,lastLoop:-1,adoptUntil:0,settle:0,err:null,
  samples:[],stopTok:0,fadeT:0,offset:0};
// Nudge only past AU_DEADBAND ms of error, by at most AU_MAXNUDGE ms, judged on
// the best of the last AU_WINDOW polls.
const AU_DEADBAND=20,AU_MAXNUDGE=250,AU_WINDOW=4;
let seqRtt=0;                       // the last poll's round trip, ms
function lsGet(k,d){try{const v=localStorage.getItem(k);return v===null?d:v}catch(e){return d}}
function lsSet(k,v){try{localStorage.setItem(k,v)}catch(e){}}
function idb(){
  return new Promise((res,rej)=>{
    if(!window.indexedDB)return rej(new Error('no IndexedDB'));
    const r=indexedDB.open('slowarc',1);
    r.onupgradeneeded=()=>r.result.createObjectStore('track');
    r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);
  });
}
async function idbDo(mode,fn){
  const db=await idb();
  return new Promise((res,rej)=>{
    const tx=db.transaction('track',mode),q=fn(tx.objectStore('track'));
    tx.oncomplete=()=>{db.close();res(q&&q.result)};
    tx.onerror=tx.onabort=()=>{db.close();rej(tx.error)};
  });
}
function auEl(){
  if(AU.el)return AU.el;
  const el=AU.el=new Audio();el.preload='auto';
  el.addEventListener('loadedmetadata',()=>{AU.dur=isFinite(el.duration)?el.duration:0;auRender();edInfo();});
  el.addEventListener('ended',()=>{AU.playing=false});
  return el;
}
function auLocked(){return AU.on&&!!AU.url&&!AU.primed}
// Browsers only allow sound after a user gesture. Called from any click/key while
// locked: play+pause the element (muted) inside the gesture, after which the page
// may start it on its own.
async function auUnlock(){
  if(!AU.on)return;
  const el=auEl();
  if(!AU.primed&&el.src&&!AU.playing){
    el.muted=true;
    try{await el.play();if(!AU.playing)el.pause();AU.primed=true}catch(e){}
    el.muted=false;
  }
  auRender();auSync();
}
['pointerdown','keydown'].forEach(ev=>document.addEventListener(ev,()=>{if(auLocked())auUnlock()},true));
function auSetTrack(blob,name,size){
  const el=auEl();
  if(AU.url)URL.revokeObjectURL(AU.url);
  auStopNow();
  AU.url=URL.createObjectURL(blob);AU.name=name;AU.size=size;AU.dur=0;AU.primed=false;
  el.src=AU.url;el.load();
  auRender();edInfo();
}
async function auPick(){
  const inp=document.getElementById('aufile'),f=inp.files[0];inp.value='';
  if(!f)return;
  auSetTrack(f,f.name,f.size);AU.saved=false;AU.note='saving a copy in this browser…';auRender();
  try{
    await idbDo('readwrite',st=>st.put({name:f.name,size:f.size,lastModified:f.lastModified,blob:f},'cur'));
    AU.saved=true;AU.note='';
    // Ask the browser not to evict the copy when disk space runs low.
    if(navigator.storage&&navigator.storage.persist)navigator.storage.persist().catch(()=>{});
  }catch(e){AU.saved=false;AU.note='not saved — re-pick after reload'}
  auRender();
}
async function auRestore(){
  try{
    const r=await idbDo('readonly',st=>st.get('cur'));
    if(r&&r.blob&&!AU.url){auSetTrack(r.blob,r.name,r.size);AU.saved=true}
  }catch(e){}
  auRender();
}
async function auForget(){
  auStopNow();
  if(AU.url)URL.revokeObjectURL(AU.url);
  AU.url='';AU.name='';AU.size=0;AU.dur=0;AU.saved=false;AU.note='';
  if(AU.el){AU.el.removeAttribute('src');AU.el.load()}
  try{await idbDo('readwrite',st=>st.delete('cur'))}catch(e){}
  auRender();edInfo();
}
function auToggle(){
  AU.on=document.getElementById('auon').checked;lsSet('au_on',AU.on?'1':'0');
  document.getElementById('aubody').style.display=AU.on?'':'none';
  if(AU.on){if(AU.url)auUnlock();else auRestore().then(auUnlock)}   // the tick is a gesture
  else auStopNow();
  auRender();
}
function auOffset(){
  const i=document.getElementById('auoff');
  AU.offset=Math.max(-2000,Math.min(2000,Math.round(+i.value||0)));
  i.value=AU.offset;lsSet('au_off',AU.offset);AU.settle=0;AU.samples=[];
}
function auFollow(){
  AU.follow=document.getElementById('aufollow').checked;lsSet('au_follow',AU.follow?'1':'0');
  AU.samples=[];auRender();
}
// A track the same length as the loop (within 1 s) loops itself, gaplessly;
// any other is restarted by auSync at each wrap.
const auLoops=()=>!!seqLen&&Math.abs(AU.dur*1000-seqLen)<=1000;
function auStartAt(s){
  const el=auEl();AU.stopTok++;clearInterval(AU.fadeT);el.volume=1;
  el.loop=auLoops()&&seqLoop;
  try{el.currentTime=s}catch(e){}
  AU.settle=performance.now()+1500;AU.err=null;AU.samples=[];AU.lastLoop=-1;AU.playing=true;
  const p=el.play();
  if(p&&p.catch)p.catch(()=>{AU.playing=false;AU.primed=false;auRender()});
}
// Fade out over 1.5 s on the element's own volume, then pause.
function auFadeStop(){
  AU.playing=false;
  const el=AU.el,tok=++AU.stopTok;if(!el)return;
  const t0=performance.now(),v0=el.volume;
  clearInterval(AU.fadeT);
  AU.fadeT=setInterval(()=>{
    if(tok!==AU.stopTok){clearInterval(AU.fadeT);return}
    const k=(performance.now()-t0)/1500;
    if(k>=1){clearInterval(AU.fadeT);el.pause();el.volume=1}
    else el.volume=v0*(1-k)*(1-k);
  },30);
}
function auStopNow(){
  AU.playing=false;AU.stopTok++;clearInterval(AU.fadeT);
  if(AU.el){AU.el.pause();AU.el.volume=1}
}
// Box position in ms, extrapolated since the last poll (not wrapped).
const boxPos=()=>seqRun?seqT+(performance.now()-seqAt):0;
// Seconds into the track for this moment of the loop, or null when there is no
// audio here (a track shorter than the loop is silent until the wrap).
function auTarget(){
  if(!seqLen||!AU.dur)return null;
  let p=(boxPos()+AU.offset)%seqLen;if(p<0)p+=seqLen;
  return p/1000<AU.dur?p/1000:null;
}
// Called after every successful poll: start or stop the audio, then measure the
// box against it.
function auSync(){
  if(!AU.on||!AU.url||!AU.dur){auRender();return}
  const pageStart=performance.now()<AU.adoptUntil;   // our own Play, not yet seen by a poll
  if(!seqRun||curMode!=='Performance'){
    if(!pageStart){if(AU.playing)auFadeStop();AU.lastRun=seqRunId}
    auRender();return;
  }
  if(auLocked()){auRender();return}
  let restarted=seqRunId!==AU.lastRun;
  if(restarted&&pageStart&&AU.playing){restarted=false;AU.adoptUntil=0}   // already started it
  // A gapless self-looping track must stop looping once the box is set to stop
  // after this run (changing .loop doesn't touch what is playing).
  if(AU.playing)AU.el.loop=auLoops()&&seqLoop;
  const loopN=Math.floor((boxPos()+AU.offset)/seqLen);
  const crossed=AU.lastLoop>=0&&loopN!==AU.lastLoop;
  const wrapped=crossed&&!AU.el.loop;
  AU.lastRun=seqRunId;AU.lastLoop=loopN;
  // Playing once: the box stops at this wrap, so let the music end with it
  // instead of starting it over for the moment until the next poll says so.
  if(!seqLoop&&((crossed&&!restarted)||AU.doneRun===seqRunId)){
    AU.doneRun=seqRunId;if(AU.playing)auFadeStop();auRender();return;
  }
  const tgt=auTarget();
  if(tgt===null){if(AU.playing)auFadeStop();auRender();return}
  if(!AU.playing||restarted||wrapped){auStartAt(tgt);AU.lastLoop=loopN;auRender();return}
  auMeasure();auRender();
}
// Audio minus box, ms (+ = audio ahead of the lights). A poll's box position is
// only as good as its round trip is symmetric, so keep the last few and trust the
// one with the shortest round trip. Following, a nudge moves the box by that
// error; the window then refills before the next one.
function auMeasure(){
  if(performance.now()<AU.settle)return;   // let a start or a nudge land first
  let e=AU.el.currentTime*1000-(((boxPos()+AU.offset)%seqLen)+seqLen)%seqLen;
  if(e>seqLen/2)e-=seqLen;else if(e<-seqLen/2)e+=seqLen;   // either side of a wrap
  AU.samples.push({e,rtt:seqRtt});
  if(AU.samples.length>AU_WINDOW)AU.samples.shift();
  const best=AU.samples.reduce((a,b)=>b.rtt<a.rtt?b:a);
  AU.err=best.e/1000;
  if(!AU.follow||AU.samples.length<AU_WINDOW||Math.abs(best.e)<AU_DEADBAND)return;
  const ms=Math.round(Math.max(-AU_MAXNUDGE,Math.min(AU_MAXNUDGE,best.e)));
  post(`/api/seq/nudge?ms=${ms}&run=${seqRunId}`);
  AU.samples=[];AU.settle=performance.now()+1000;
}
function auRender(){
  document.getElementById('aubanner').style.display=auLocked()?'':'none';
  // Lengths differ: loud, across the top of the Performance box. Only the
  // earlier of the two plays through; the audio restarts at each loop.
  const lw=document.getElementById('lenwarn'),
        diff=AU.on&&AU.dur&&seqLen&&Math.abs(AU.dur*1000-seqLen)>1000;
  lw.style.display=diff?'':'none';
  if(diff)lw.innerHTML=`⚠ Track and sequence lengths differ`+
    `<small>track ${fmt1(AU.dur*1000)} · sequence loop ${fmt1(seqLen)} — `+
    (AU.dur*1000<seqLen?'the audio stops early and is silent until the sequence loops':'the audio is cut off each time the sequence loops')+
    `. Match them on the Sequences tab.</small>`;
  if(!AU.on)return;
  document.getElementById('autrack').textContent=AU.url
    ?`${AU.name} · ${(AU.size/1048576).toFixed(1)} MB · ${AU.dur?fmt1(AU.dur*1000):'reading…'}${AU.saved?' · saved in this browser':''}`
    :'no track chosen';
  let s;
  if(!AU.url)s='choose the track file on this computer';
  else if(auLocked())s='locked — click anywhere on the page to enable audio';
  else if(AU.playing)s='playing'+(AU.err!==null?` (${AU.err>=0?'+':'−'}${Math.round(Math.abs(AU.err)*1000)} ms)`:'')
    +(AU.follow?' · time correction on':' · free-running');
  else s='armed — plays when the sequence runs';
  if(document.hidden&&AU.url&&!AU.playing)s+=' · tab in the background: keep it in front so a start is caught promptly';
  if(AU.note)s+=' · '+AU.note;
  document.getElementById('austat').textContent=s;
}
document.addEventListener('visibilitychange',auRender);
setInterval(()=>{if(AU.on)auRender()},1000);
// --- Liveness ---------------------------------------------------------------
// The page must never keep claiming "healthy" for a box that has been switched
// off. Every poll is timed out, and the strip goes red once we have gone long
// enough with no reply — the reading below is then last-known, not current.
let polling=false,lastOkMs=0,offline=false;
const OFFLINE_MS=5000;
function renderOffline(){
  const age=lastOkMs?Math.round((Date.now()-lastOkMs)/1000):0;
  document.getElementById('status').innerHTML=
    `<div><span class="dot bad"></span><b id="offmsg">OFFLINE</b></div>`+
    `<div>${lastOkMs?`no reply for ${age} s`:'no reply from the box'} — `+
      `it may be powered down, or your device has left the network</div>`+
    `<div>The readings below are the last ones received, not live.</div>`;
}
function goOffline(){
  offline=true;seqLive=false;                    // freeze the timeline, don't extrapolate
  document.getElementById('rack').classList.add('stale');
  document.getElementById('playcard').classList.add('stale');
  renderOffline();
  if(seq)drawSeq();
}
function wifiLine(d){
  const ss=d.ssid?`<b>${esc(d.ssid)}</b>`:'';
  const ip=d.ip?` (${d.ip})`:'';
  if(d.wifi==='station')return `connected to ${ss}${ip}`;
  if(d.wifi==='connecting')return `joining ${ss}…`;
  if(d.wifi==='ap')return `own network ${ss}${ip}`;
  return `<b>${d.wifi}</b>`;
}
function wifiOpen(){
  tab(0);
  const w=document.getElementById('wifi');w.open=true;w.scrollIntoView({behavior:'smooth'});
}
async function poll(){
  if(polling)return;              // a hung request must not stack up behind itself
  polling=true;
  try{
    const t0=performance.now();
    const d=await (await fetchT('/api/state')).json();
    const t1=performance.now();
    lastOkMs=Date.now();
    offline=false;seqLive=true;
    document.getElementById('rack').classList.remove('stale');
    document.getElementById('playcard').classList.remove('stale');
    const hl=d.fault?`<span class="dot bad"></span>FAULT ${d.fault}`
       :`<span class="dot ok"></span>healthy`;
    // Name the active piece here too: it is the one place visible in both modes,
    // so a change made on the Sequences tab is confirmed without leaving Control.
    const nm=d.seq_name?`<b>“${esc(d.seq_name)}”</b> `:'';
    const q=d.seq_queued?` — “${esc(d.seq_queued)}” next`:'';
    document.getElementById('status').innerHTML=
      `<div>${hl}</div>`+
      `<div>Mode: <b>${d.mode}</b></div>`+
      `<div>Sequence: ${nm}${d.running?'running':'stopped'}${q}</div>`+
      `<div>WiFi: ${wifiLine(d)}</div>`+
      (d.host?`<div>Address: <b><a href="http://${d.host}/">http://${d.host}</a></b></div>`:'')+
      `<div>Uptime: <b>${d.uptime}s</b></div>`+
      (d.wifi==='ap'?`<div class="wifiask"><span class="dot warn"></span>This box is serving its own WiFi `+
        `(<b>${esc(d.ssid||'SlowArc-Setup')}</b>). Connect it to a local WiFi network in `+
        `<a href="#wifi" onclick="wifiOpen();return false">WiFi setup</a> so it can be reached from the house network.</div>`:'');
    document.getElementById('wsum').textContent=
      `${d.wifi}${d.ssid?' · '+d.ssid:''}${d.ip?' · '+d.ip:''}${d.host?' · '+d.host:''}`;
    paintPlay(d.mode,d.running,!!d.override,d.switch,d.btn_ago);
    const sc=document.getElementById('seqcard');
    sc.style.display=(d.mode==='Performance')?'':'none';
    // seq_t was sampled mid-request: credit half the round trip.
    seqRun=d.running;seqT=d.seq_t+(d.running?(t1-t0)/2:0);seqAt=t1;seqRtt=t1-t0;
    seqLen=d.seq_len||0;seqRunId=d.run_id||0;curMode=d.mode;
    seqLoop=d.seq_loop!==false;paintLoop();
    // Refetch the timeline whenever the loaded piece changes — including when
    // another phone changed it, which no local seq=null would have caught.
    const changed=seqName!==d.seq_name;
    seqName=d.seq_name||'';seqQueued=d.seq_queued||'';
    if(d.mode==='Performance'&&(!seq||changed))loadSeq();
    for(let i=0;i<N;i++){
      setPwr(i,!!d.on[i]);
      const br=document.getElementById('brightness'+i);
      if(document.activeElement!==br)br.value=to100(d.brightness[i]);
      cap('brightness',i);
      // Speed: don't fight the operator — leave both controls alone while either
      // the slider or the Hz box has focus, otherwise take the firmware's Hz.
      const sp=document.getElementById('speed'+i),hb=document.getElementById('hz'+i);
      if(document.activeElement!==sp&&document.activeElement!==hb){
        sp.value=hzToPct(d.speed[i]);
        hb.value=d.speed[i];
        cap('speed',i);
      }
    }
    auSync();
  }catch(e){
    // One dropped poll is a blip; sustained silence is a box that is not there.
    if(Date.now()-lastOkMs>OFFLINE_MS)goOffline();
  }finally{polling=false}
}
// Poll faster while this browser plays audio: a start from the front-panel
// button is then heard within ~0.5 s rather than 2 s.
function pollLoop(){poll().finally(()=>setTimeout(pollLoop,AU.on?500:2000))}
AU.offset=+lsGet('au_off',0)||0;document.getElementById('auoff').value=AU.offset;
AU.follow=lsGet('au_follow','1')==='1';document.getElementById('aufollow').checked=AU.follow;
if(lsGet('au_on','0')==='1'){
  AU.on=true;document.getElementById('auon').checked=true;
  document.getElementById('aubody').style.display='';
  auRestore();
}
build(); pollLoop();
setInterval(()=>{if(offline)renderOffline()},1000);   // keep the age counting up
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

// Largest /api/seq/save body accepted: 1000 worst-case steps of
// "2400000:63:600000," are ~18 KB.
constexpr size_t kSeqBodyMax = 24576;

// Snap a time to the sequence resolution (0.1 s).
uint32_t snapMs(long ms) {
  if (ms < 0) ms = 0;
  return (uint32_t)((ms + (long)SEQ_TIME_RES_MS / 2) / (long)SEQ_TIME_RES_MS) * SEQ_TIME_RES_MS;
}

// Parse a save request into d (zeroed by the caller). Returns an error message
// for the 400 reply, or nullptr on success. Steps are "t_ms:mask:ramp_ms"
// triples, comma-separated; times and ramps snap to 0.1 s, steps sort by time,
// and the loop must end at or after the last step.
const char* parseSeqSave(const String& name, long loopMs, const char* body, SeqDef& d) {
  strlcpy(d.name, name.c_str(), sizeof(d.name));
  if (d.name[0] == '\0') return "missing name";

  const char* c = body;
  uint16_t n = 0;
  while (*c) {
    while (*c == ' ' || *c == '\n' || *c == '\r') ++c;
    if (!*c) break;
    if (n >= SEQ_MAX_STEPS) return "too many steps (max 1000)";
    char* e;
    long t = strtol(c, &e, 10);
    if (e == c || *e != ':') return "bad step (want t_ms:mask:ramp_ms)";
    const char* mStart = e + 1;
    long m = strtol(mStart, &e, 10);
    if (e == mStart || m < 0 || m > 63) return "bad step mask (0..63)";
    if (*e != ':') return "bad step (want t_ms:mask:ramp_ms)";
    const char* rStart = e + 1;
    long r = strtol(rStart, &e, 10);
    if (e == rStart || r < 0 || r > (long)SEQ_RAMP_MAX_MS) return "bad step ramp (0..600000 ms)";
    d.steps[n].timeMs = snapMs(t);
    d.steps[n].mask   = (uint8_t)m;
    d.steps[n].rampDs = (uint16_t)(snapMs(r) / 100);
    ++n;
    if (*e == ',') { c = e + 1; continue; }
    while (*e == ' ' || *e == '\n' || *e == '\r') ++e;
    if (*e) return "bad step separator (want commas)";
    break;
  }
  if (n == 0) return "no steps";
  d.stepCount = n;
  uint32_t lastT = 0;
  for (uint16_t a = 0; a < n; ++a) if (d.steps[a].timeMs > lastT) lastT = d.steps[a].timeMs;
  d.loopMs = snapMs(loopMs);
  if (d.loopMs == 0 || d.loopMs < lastT) return "loop must end at or after the last step";
  // Keep steps sorted by time regardless of entry order (stable, so same-time
  // steps keep their order and the later one still wins on playback). The page
  // sends them sorted, so this is ~linear in practice.
  for (uint16_t a = 1; a < n; ++a) {
    SeqStep key = d.steps[a];
    int b = a - 1;
    while (b >= 0 && d.steps[b].timeMs > key.timeMs) { d.steps[b + 1] = d.steps[b]; --b; }
    d.steps[b + 1] = key;
  }
  return nullptr;
}
}  // namespace

void WebUi::begin(ChannelModel* model, ConfigStore* store, SequenceStore* seqStore,
                  bool* arcEnabled,
                  std::function<String()> stateJson,
                  std::function<void(Print&)> sequenceJson,
                  std::function<void(const String&, const String&)> onWifiCredentials,
                  std::function<void(uint8_t)> onSequenceActivated,
                  std::function<void(Mode)> onMode,
                  std::function<void(bool)> onRun,
                  std::function<void(int32_t, uint32_t)> onNudge,
                  std::function<void(bool)> onLoop) {
  model_        = model;
  store_        = store;
  seqStore_     = seqStore;
  arcEnabled_   = arcEnabled;
  stateJson_    = stateJson;
  sequenceJson_ = sequenceJson;
  wifiCreds_    = onWifiCredentials;
  seqActivated_ = onSequenceActivated;
  onMode_       = onMode;
  onRun_        = onRun;
  onNudge_      = onNudge;
  onLoop_       = onLoop;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", kIndexHtml);
  });

  server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "application/json", stateJson_ ? stateJson_() : "{}");
  });

  server.on("/api/sequence", HTTP_GET, [this](AsyncWebServerRequest* req) {
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    if (sequenceJson_) sequenceJson_(*r); else r->print("{}");
    req->send(r);
  });

  // GET /api/seqs — stored sequence list + the active slot. Names only: each is
  // read from its file header, not the whole ~8 KB sequence.
  server.on("/api/seqs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String j = "{\"active\":";
    j += seqStore_->activeIndex();
    j += ",\"seqs\":[";
    char nm[sizeof(SeqDef::name)];
    bool first = true;
    for (uint8_t i = 0; i < SEQ_SLOTS; ++i) {
      if (!seqStore_->nameOf(i, nm)) continue;
      if (!first) j += ',';
      first = false;
      j += "{\"i\":"; j += i;
      j += ",\"name\":\""; j += jsonEscape(nm); j += "\"}";
    }
    j += "]}";
    req->send(200, "application/json", j);
  });

  // GET /api/seq?i=N — one stored definition, streamed from its file.
  // Times, ramps and loop in ms; mask bit i = arc i+1.
  server.on("/api/seq", HTTP_GET, [this](AsyncWebServerRequest* req) {
    int i = req->hasParam("i") ? req->getParam("i")->value().toInt() : -1;
    if (i < 0 || i >= SEQ_SLOTS || !seqStore_->exists((uint8_t)i)) {
      req->send(404, "text/plain", "no such sequence");
      return;
    }
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    if (!seqStore_->streamJson((uint8_t)i, *r)) {
      delete r;
      req->send(500, "text/plain", "sequence unreadable");
      return;
    }
    req->send(r);
  });

  // POST /api/seq/select?i=N — becomes the active sequence.
  server.on("/api/seq/select", HTTP_POST, [this](AsyncWebServerRequest* req) {
    int i = req->hasParam("i") ? req->getParam("i")->value().toInt() : -1;
    if (i < 0 || i >= SEQ_SLOTS || !seqStore_->exists((uint8_t)i)) {
      req->send(404, "text/plain", "no such sequence");
      return;
    }
    seqStore_->setActive((uint8_t)i);
    if (seqActivated_) seqActivated_((uint8_t)i);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/seq/nudge?ms=N&run=R — shift the running sequence's clock by N ms
  // (+ = forward) so the lights follow the audio a browser is playing. Main
  // drops it unless R is still the current run, so a nudge measured just before
  // a restart can't land on the new run.
  server.on("/api/seq/nudge", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("ms") || !req->hasParam("run")) {
      req->send(400, "text/plain", "missing ms/run");
      return;
    }
    long ms = req->getParam("ms")->value().toInt();
    if (ms < -5000 || ms > 5000) { req->send(400, "text/plain", "bad ms"); return; }
    if (onNudge_) onNudge_((int32_t)ms, (uint32_t)req->getParam("run")->value().toInt());
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/seq/loop?on=0|1 — loop the piece, or stop at the end of this pass.
  server.on("/api/seq/loop", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("on")) { req->send(400, "text/plain", "missing on"); return; }
    if (onLoop_) onLoop_(req->getParam("on")->value().toInt() != 0);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/seq/save?name=..&loop_ms=..   body: t_ms:mask:ramp_ms,...
  // The steps ride in the body (text/plain): 1000 of them are ~12 KB, far past
  // what belongs in a URL. The body is gathered into _tempObject (freed by the
  // server with the request) and parsed into a heap SeqDef — never the stack.
  server.on("/api/seq/save", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!req->hasParam("name") || !req->hasParam("loop_ms")) {
        req->send(400, "text/plain", "missing name/loop_ms");
        return;
      }
      const char* body = (const char*)req->_tempObject;
      if (!body) {
        req->send(400, "text/plain", "missing steps (or body over 24 KB)");
        return;
      }
      SeqDef* d = (SeqDef*)calloc(1, sizeof(SeqDef));
      if (!d) { req->send(503, "text/plain", "out of memory"); return; }
      const char* err = parseSeqSave(req->getParam("name")->value(),
                                     req->getParam("loop_ms")->value().toInt(), body, *d);
      if (err) { free(d); req->send(400, "text/plain", err); return; }

      int slot = seqStore_->saveByName(*d);
      free(d);
      if (slot < 0) {
        req->send(507, "text/plain", "sequence slots full (or storage write failed)");
        return;
      }
      // Saving over the active sequence re-arms the engine with the new version.
      if ((uint8_t)slot == seqStore_->activeIndex() && seqActivated_) seqActivated_((uint8_t)slot);
      String j = "{\"ok\":true,\"slot\":";
      j += slot;
      j += '}';
      req->send(200, "application/json", j);
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (total == 0 || total > kSeqBodyMax) return;     // no buffer -> 400 above
      if (index == 0) {
        req->_tempObject = calloc(total + 1, 1);   // zeroed: always NUL-terminated
        if (!req->_tempObject) return;
      }
      if (!req->_tempObject || index + len > total) return;
      memcpy((uint8_t*)req->_tempObject + index, data, len);
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
    if (req->hasParam("speed")) {              // step rate in Hz (0 = stopped)
      model_->speedHz[ch] = motorClampHz(req->getParam("speed")->value().toInt());
      changed = true;
    }
    if (changed) store_->markDirty();   // debounced commit happens in main loop
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/mode?m=Gallery|Performance — the web half of "last change wins"
  // with the front-panel switch, so a box out of reach can still change mode.
  server.on("/api/mode", HTTP_POST, [this](AsyncWebServerRequest* req) {
    String m = req->hasParam("m") ? req->getParam("m")->value() : String();
    if (m != "Gallery" && m != "Performance") {
      req->send(400, "text/plain", "m must be Gallery or Performance");
      return;
    }
    if (onMode_) onMode_(m == "Performance" ? Mode::Performance : Mode::Gallery);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/run?on=0|1 — stop / play the active sequence, as the button does.
  server.on("/api/run", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("on")) { req->send(400, "text/plain", "missing on"); return; }
    if (onRun_) onRun_(req->getParam("on")->value().toInt() != 0);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/arc?ch=N&on=0|1 — runtime per-arc kill switch. Deliberately does
  // NOT touch the model or NVS: purely a live tool, resets to all-on at boot.
  server.on("/api/arc", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("ch") || !req->hasParam("on")) {
      req->send(400, "text/plain", "missing ch/on");
      return;
    }
    int ch = req->getParam("ch")->value().toInt();
    if (ch < 0 || ch >= NUM_CHANNELS) { req->send(400, "text/plain", "bad ch"); return; }
    arcEnabled_[ch] = req->getParam("on")->value().toInt() != 0;
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
