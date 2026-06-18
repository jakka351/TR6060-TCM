// =============================================================================
//  index_html.h  -  single-page web dashboard (served from flash)
// =============================================================================
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLDOC(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FG-TCM-MITM</title>
<style>
  :root{
    --bg:#0d1117; --panel:#161b22; --panel2:#1c2330; --line:#2a3340;
    --txt:#e6edf3; --mut:#8b949e; --acc:#3fb950; --warn:#d29922; --bad:#f85149;
    --blue:#58a6ff; --veh:#a371f7; --tcm:#3fb950;
  }
  *{box-sizing:border-box}
  body{margin:0;font-family:Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--txt);font-size:14px}
  header{background:#010409;border-bottom:1px solid var(--line);padding:10px 16px;display:flex;align-items:center;gap:14px;flex-wrap:wrap}
  header h1{font-size:17px;margin:0;letter-spacing:.5px}
  .pill{font-size:11px;padding:3px 9px;border-radius:20px;border:1px solid var(--line);color:var(--mut)}
  .pill.ok{color:var(--acc);border-color:var(--acc)}
  .pill.bad{color:var(--bad);border-color:var(--bad)}
  .safety{margin-left:auto;font-size:12px;font-weight:700;color:#010409;background:var(--acc);padding:5px 12px;border-radius:6px}
  .safety.tx{background:var(--warn)}
  nav{display:flex;gap:2px;background:var(--panel);border-bottom:1px solid var(--line);padding:0 8px;flex-wrap:wrap}
  nav button{background:none;border:none;color:var(--mut);padding:11px 16px;cursor:pointer;font-size:14px;border-bottom:2px solid transparent}
  nav button.active{color:var(--txt);border-bottom-color:var(--blue)}
  main{padding:16px;max-width:1200px;margin:0 auto}
  .tab{display:none}.tab.active{display:block}
  .grid{display:grid;gap:12px}
  .g4{grid-template-columns:repeat(4,1fr)}.g3{grid-template-columns:repeat(3,1fr)}.g2{grid-template-columns:repeat(2,1fr)}
  @media(max-width:760px){.g4,.g3,.g2{grid-template-columns:repeat(2,1fr)}}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:14px}
  .card h3{margin:0 0 10px;font-size:12px;text-transform:uppercase;letter-spacing:1px;color:var(--mut);font-weight:600}
  .metric{display:flex;flex-direction:column;gap:3px}
  .metric .v{font-size:26px;font-weight:700;font-variant-numeric:tabular-nums}
  .metric .l{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px}
  .metric .u{font-size:13px;color:var(--mut);font-weight:400}
  .big{font-size:40px}
  table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
  th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);font-size:13px}
  th{color:var(--mut);font-weight:600;font-size:11px;text-transform:uppercase;position:sticky;top:0;background:var(--panel)}
  td.mono,.mono{font-family:Consolas,Menlo,monospace}
  .tag{font-size:10px;padding:2px 6px;border-radius:4px;font-weight:700}
  .tag.veh{background:#2a1f4d;color:var(--veh)}.tag.tcm{background:#102b1a;color:var(--tcm)}
  .tag.fwd{background:#0b2d4a;color:var(--blue)} .tag.blk{background:#3a1416;color:var(--bad)}
  input,select{background:var(--panel2);border:1px solid var(--line);color:var(--txt);border-radius:6px;padding:7px 9px;font-size:13px}
  input.mono{font-family:Consolas,monospace}
  label{display:block;font-size:12px;color:var(--mut);margin-bottom:4px}
  .row{display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap;margin-bottom:10px}
  button.act{background:var(--blue);border:none;color:#010409;font-weight:700;padding:9px 16px;border-radius:6px;cursor:pointer}
  button.act.warn{background:var(--warn)} button.act.danger{background:var(--bad);color:#fff}
  button.sm{padding:5px 10px;font-size:12px}
  .note{font-size:12px;color:var(--mut);line-height:1.5;background:var(--panel2);border:1px solid var(--line);border-left:3px solid var(--blue);padding:10px 12px;border-radius:6px;margin:10px 0}
  .note.warn{border-left-color:var(--warn)} .note.bad{border-left-color:var(--bad)}
  .led{display:inline-block;width:9px;height:9px;border-radius:50%;background:#30363d;margin-right:6px;vertical-align:middle}
  .led.on{background:var(--acc);box-shadow:0 0 6px var(--acc)} .led.alarm{background:var(--bad);box-shadow:0 0 6px var(--bad)}
  .scroll{max-height:62vh;overflow:auto;border:1px solid var(--line);border-radius:8px}
  .toast{position:fixed;bottom:18px;right:18px;background:var(--panel);border:1px solid var(--acc);color:var(--txt);padding:11px 16px;border-radius:8px;opacity:0;transition:.3s;pointer-events:none}
  .toast.show{opacity:1}
</style>
</head>
<body>
<header>
  <h1>&#9881; FG-TCM-MITM</h1>
  <span class="pill" id="wsState">connecting&hellip;</span>
  <span class="pill" id="fwState"></span>
  <span class="safety" id="safetyBanner">VEHICLE BUS &mdash; LISTEN-ONLY / SILENT</span>
</header>
<nav>
  <button class="active" data-tab="dash">Dashboard</button>
  <button data-tab="sniff">Live Sniffer</button>
  <button data-tab="fwd">Forwarding</button>
  <button data-tab="inject">Inject (TCM)</button>
  <button data-tab="set">Settings</button>
</nav>
<main>

<!-- ===================== DASHBOARD ===================== -->
<section class="tab active" id="dash">
  <div class="grid g4">
    <div class="card metric"><span class="l">Selected (J-gate)</span><span class="v" id="gSel">&mdash;</span></div>
    <div class="card metric"><span class="l">Actual Gear</span><span class="v big" id="gAct">&mdash;</span></div>
    <div class="card metric"><span class="l">Target Gear</span><span class="v" id="gTgt">&mdash;</span></div>
    <div class="card metric"><span class="l">Shift Map</span><span class="v" id="sMap">&mdash;</span></div>
  </div>
  <div class="grid g4" style="margin-top:12px">
    <div class="card metric"><span class="l">Oil Temp</span><span class="v" id="oil">&mdash;<span class="u"> &deg;C</span></span></div>
    <div class="card metric"><span class="l">Input Speed</span><span class="v" id="inRpm">&mdash;<span class="u"> rpm</span></span></div>
    <div class="card metric"><span class="l">Output Speed</span><span class="v" id="outRpm">&mdash;<span class="u"> rpm</span></span></div>
    <div class="card metric"><span class="l">TC Slip</span><span class="v" id="slip">&mdash;</span></div>
  </div>
  <div class="grid g2" style="margin-top:12px">
    <div class="card">
      <h3>TCM Status</h3>
      <div id="tcmFlags"></div>
      <div class="note" id="tcmSeen">Waiting for TCM frames (0x0C9 / 0x3E9)&hellip;</div>
    </div>
    <div class="card">
      <h3>Simulated Vehicle &rarr; TCM</h3>
      <div class="grid g2">
        <div class="metric"><span class="l">Engine</span><span class="v" id="vRpm">&mdash;<span class="u"> rpm</span></span></div>
        <div class="metric"><span class="l">Vehicle Speed</span><span class="v" id="vKph">&mdash;<span class="u"> km/h</span></span></div>
        <div class="metric"><span class="l">Pedal</span><span class="v" id="vPedal">&mdash;<span class="u"> %</span></span></div>
        <div class="metric"><span class="l">Throttle</span><span class="v" id="vThr">&mdash;<span class="u"> %</span></span></div>
      </div>
      <div style="margin-top:10px" id="simFlags"></div>
    </div>
  </div>
  <div class="grid g3" style="margin-top:12px">
    <div class="card"><h3 id="vehBusTitle">Vehicle Bus (TWAI, passive)</h3>
      <div>RX <b id="vRx">0</b> &nbsp; TX <b id="vTx">0</b> &nbsp; <span class="mut" id="vFps">0</span> fps &nbsp; err <span id="vErr">0</span></div>
      <div class="note" id="vehBusNote">No transmit path exists for this bus.</div></div>
    <div class="card"><h3>TCM Bus (MCP2515, active)</h3>
      <div>RX <b id="tRx">0</b> &nbsp; TX <b id="tTx">0</b> &nbsp; <span class="mut" id="tFps">0</span> fps &nbsp; err <span id="tErr">0</span></div></div>
    <div class="card"><h3>Gateway</h3>
      <div>Forwarded <b id="cFwd">0</b></div><div>Overridden <b id="cOvr">0</b></div><div>Blocked <b id="cBlk">0</b></div>
      <div id="diagLine" style="display:none;margin-top:6px;border-top:1px solid var(--line);padding-top:6px">
        Diag &rarr;TCM <b id="cdT">0</b> &nbsp; &rarr;VEH <b id="cdV">0</b></div></div>
  </div>
</section>

<!-- ===================== SNIFFER ===================== -->
<section class="tab" id="sniff">
  <div class="row">
    <div><label>Bus filter</label>
      <select id="fBus"><option value="">All</option><option value="0">Vehicle</option><option value="1">TCM</option></select></div>
    <div><label>ID contains (hex)</label><input class="mono" id="fId" placeholder="e.g. 3E9"></div>
    <div><label>&nbsp;</label><button class="act sm" id="freeze">Freeze</button></div>
    <span class="pill" id="snCount">0 IDs</span>
  </div>
  <div class="scroll"><table>
    <thead><tr><th>Bus</th><th>ID</th><th>Name</th><th>DLC</th><th>Data</th><th>Period</th><th>Count</th><th>Fwd</th></tr></thead>
    <tbody id="snBody"></tbody></table></div>
</section>

<!-- ===================== FORWARDING ===================== -->
<section class="tab" id="fwd">
  <div class="note">Frames flow <b>Vehicle &rarr; TCM only</b>. Rules choose what reaches the TCM:
    <b>PASS</b> forward unchanged &middot; <b>BLOCK</b> drop &middot; <b>OVERRIDE</b> rewrite then forward
    (0x640 is rewritten to an automatic/TCM-controlled driveline) &middot; <b>GENERATE</b> do not forward; synthesise locally.
    Nothing here can ever transmit toward the vehicle.</div>
  <div class="scroll"><table>
    <thead><tr><th>ID (hex)</th><th>Name</th><th>Action</th><th>Period ms</th><th></th></tr></thead>
    <tbody id="ruleBody"></tbody></table></div>
  <div class="row" style="margin-top:12px">
    <div><label>Add ID (hex)</label><input class="mono" id="nId" placeholder="0x640" style="width:110px"></div>
    <div><label>Name</label><input id="nName" placeholder="label" style="width:160px"></div>
    <div><label>Action</label><select id="nAct"><option value="0">PASS</option><option value="1">BLOCK</option><option value="2">OVERRIDE</option><option value="3">GENERATE</option></select></div>
    <div><label>Period</label><input id="nPer" type="number" value="0" style="width:90px"></div>
    <button class="act sm" id="addRule">Add</button>
  </div>
  <div class="row"><button class="act" id="saveRules">Save Rules</button>
    <button class="act warn" id="resetRules">Reset to Defaults</button></div>
</section>

<!-- ===================== INJECT ===================== -->
<section class="tab" id="inject">
  <div class="note warn">This transmits a single frame on the <b>TCM bus</b> only. The vehicle bus is
    hardware listen-only and has no transmit path &mdash; it can never be injected onto.</div>
  <div class="card" style="max-width:560px">
    <div class="row">
      <div><label>CAN ID (hex)</label><input class="mono" id="iId" value="0x640" style="width:120px"></div>
      <div><label>DLC</label><input id="iLen" type="number" min="0" max="8" value="8" style="width:70px"></div>
    </div>
    <label>Data bytes (hex)</label>
    <div class="row" id="iBytes"></div>
    <button class="act" id="sendInj">Send to TCM bus</button>
  </div>
</section>

<!-- ===================== SETTINGS ===================== -->
<section class="tab" id="set">
  <div class="grid g2">
    <div class="card"><h3>Simulated Vehicle</h3>
      <div class="row"><label><input type="checkbox" id="sEn"> Master enable (transmit on TCM bus)</label></div>
      <div class="row"><label><input type="checkbox" id="sAuto"> Force automatic driveline config (override 0x640)</label></div>
      <div class="row"><label><input type="checkbox" id="sGen"> Bench idle simulation when no live car</label></div>
      <div class="row">
        <div><label>Forward gears</label><input id="sGears" type="number" min="1" max="9" style="width:80px"></div>
        <div><label>Trans config</label><select id="sTC"><option value="0">Barra_E265</option><option value="1">Copperhead</option></select></div>
        <div><label>Axle ratio</label><input id="sAxle" type="number" step="0.001" style="width:100px"></div>
      </div>
    </div>
    <div class="card"><h3>CAN Bitrates</h3>
      <div class="row">
        <div><label>Vehicle bus</label><select id="sVb"></select></div>
        <div><label>TCM bus</label><select id="sTb"></select></div>
      </div>
      <div class="note">Changing a bitrate is saved immediately but applies after a restart.</div>
    </div>
    <div class="card"><h3>Diagnostic Bridge (OBD &harr; TCM)</h3>
      <div class="row"><label><input type="checkbox" id="dEn"> Enable OBD&nbsp;&harr;&nbsp;TCM diagnostic passthrough</label></div>
      <div class="note bad">Enabling this takes the <b>vehicle bus out of listen-only</b> and runs it in
        NORMAL mode so the TCM's response can be sent back to the tester. The node will then also
        ACK vehicle traffic. The only frame ID ever transmitted on the car is the response ID below.
        <b>Applies after a restart.</b></div>
      <div class="row">
        <div><label>Request ID (veh&rarr;TCM)</label><input class="mono" id="dReq" style="width:100px"></div>
        <div><label>Functional ID (0=off)</label><input class="mono" id="dFunc" style="width:100px"></div>
        <div><label>Response ID (TCM&rarr;veh)</label><input class="mono" id="dResp" style="width:100px"></div>
      </div>
    </div>
    <div class="card"><h3>WiFi</h3>
      <div class="row"><div><label>Mode</label><select id="wMode"><option value="0">Access Point</option><option value="1">Join network (STA)</option></select></div></div>
      <div class="row"><div><label>AP / STA SSID</label><input id="wSsid"></div>
        <div><label>Password</label><input id="wPass"></div></div>
      <div class="note">WiFi changes apply after a restart.</div>
    </div>
    <div class="card"><h3>Actions</h3>
      <div class="row"><button class="act" id="saveSet">Save Settings</button>
        <button class="act danger" id="restart">Restart Device</button></div>
      <div class="note" id="fwInfo"></div>
    </div>
  </div>
</section>
</main>
<div class="toast" id="toast"></div>

<script>
const $=s=>document.querySelector(s), $$=s=>document.querySelectorAll(s);
let frozen=false, cfg=null;
const BR=[100000,125000,250000,500000,800000,1000000];

// ---- tabs ----
$$('nav button').forEach(b=>b.onclick=()=>{
  $$('nav button').forEach(x=>x.classList.remove('active'));
  $$('.tab').forEach(x=>x.classList.remove('active'));
  b.classList.add('active'); $('#'+b.dataset.tab).classList.add('active');
});

function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2200);}
function hex(n,w=3){return n.toString(16).toUpperCase().padStart(w,'0');}
function led(on,alarm){return `<span class="led ${alarm?'alarm':(on?'on':'')}"></span>`;}

// ---- websocket live feed ----
let ws;
function connect(){
  ws=new WebSocket('ws://'+location.host+'/ws');
  ws.onopen =()=>{$('#wsState').textContent='live';$('#wsState').className='pill ok';};
  ws.onclose=()=>{$('#wsState').textContent='offline';$('#wsState').className='pill bad';setTimeout(connect,1500);};
  ws.onmessage=e=>{try{render(JSON.parse(e.data));}catch(x){}};
}
connect();

function render(d){
  const t=d.tcm||{}, v=d.veh||{}, s=d.sim||{}, b=d.bus||{};
  // gears
  $('#gSel').textContent=t.gearSel||'—';
  $('#gAct').textContent=t.gearAct||'—';
  $('#gTgt').textContent=t.gearTgt||'—';
  $('#sMap').textContent=t.map||'—';
  $('#oil').innerHTML=(t.oil??'—')+'<span class="u"> °C</span>';
  $('#inRpm').innerHTML=(t.inRpm??'—')+'<span class="u"> rpm</span>';
  $('#outRpm').innerHTML=(t.outRpm??'—')+'<span class="u"> rpm</span>';
  $('#slip').textContent=t.slip??'—';
  $('#tcmSeen').style.display=t.seen?'none':'block';
  $('#tcmFlags').innerHTML=t.seen?
    led(t.tcLock)+'TC Locked &nbsp; '+led(t.shift)+'Shifting &nbsp; '+
    led(false,t.malf)+'Malfunction &nbsp; '+led(false,t.mil)+'MIL &nbsp; '+led(false,t.gsf)+'Gear Sel Fault':'';
  // sim
  $('#vRpm').innerHTML=(s.rpm??'—')+'<span class="u"> rpm</span>';
  $('#vKph').innerHTML=(s.kph??'—')+'<span class="u"> km/h</span>';
  $('#vPedal').innerHTML=(s.pedal??'—')+'<span class="u"> %</span>';
  $('#vThr').innerHTML=(s.thr??'—')+'<span class="u"> %</span>';
  $('#simFlags').innerHTML=led(s.en)+'Sim TX &nbsp; '+led(s.auto)+'Force Auto Config &nbsp; '+led(s.bench)+'Bench Idle';
  $('#fwState').textContent=(s.en?'SIM ON':'SIM OFF');$('#fwState').className='pill '+(s.en?'ok':'');
  // bus stats
  $('#vRx').textContent=v.rx||0;$('#vTx').textContent=v.tx||0;$('#vFps').textContent=v.fps||0;$('#vErr').textContent=v.err||0;
  $('#tRx').textContent=b.rx||0;$('#tTx').textContent=b.tx||0;$('#tFps').textContent=b.fps||0;$('#tErr').textContent=b.err||0;
  $('#cFwd').textContent=d.fwd||0;$('#cOvr').textContent=d.ovr||0;$('#cBlk').textContent=d.blk||0;
  // safety banner + diagnostic bridge
  const dg=d.diag||{}, lo=(v.listenOnly!==false);
  const sb=$('#safetyBanner');
  if(lo){sb.textContent='VEHICLE BUS — LISTEN-ONLY / SILENT';sb.classList.remove('tx');
    $('#vehBusTitle').textContent='Vehicle Bus (TWAI, passive)';
    $('#vehBusNote').textContent='No transmit path exists for this bus.';}
  else{sb.textContent='VEHICLE BUS — NORMAL (diag bridge: TX 0x'+hex(dg.respId||0x7E9)+' only)';sb.classList.add('tx');
    $('#vehBusTitle').textContent='Vehicle Bus (TWAI, diag bridge active)';
    $('#vehBusNote').textContent='Transmitting on the car — limited to the diagnostic response ID.';}
  $('#diagLine').style.display=dg.bridge?'block':'none';
  $('#cdT').textContent=dg.toTcm||0;$('#cdV').textContent=dg.toVeh||0;
  if(d.frames&&!frozen) renderSniff(d.frames);
}

function renderSniff(frames){
  const fb=$('#fBus').value, fi=$('#fId').value.toUpperCase();
  frames.sort((a,b)=>a.bus-b.bus||a.id-b.id);
  let rows='', n=0;
  for(const f of frames){
    if(fb!==''&&f.bus!=fb) continue;
    if(fi&&!hex(f.id).includes(fi)) continue;
    n++;
    rows+=`<tr><td><span class="tag ${f.bus?'tcm':'veh'}">${f.bus?'TCM':'VEH'}</span></td>
      <td class="mono">0x${hex(f.id)}</td><td>${f.name||''}</td><td>${f.dlc}</td>
      <td class="mono">${f.d}</td><td>${f.per}</td><td>${f.cnt}</td>
      <td>${f.bus==0?(f.fwd?'<span class="tag fwd">&rarr;TCM</span>':'<span class="tag blk">&mdash;</span>'):(f.fwd?'<span class="tag fwd">&rarr;VEH</span>':'')}</td></tr>`;
  }
  $('#snBody').innerHTML=rows;
  $('#snCount').textContent=n+' IDs';
}
$('#freeze').onclick=function(){frozen=!frozen;this.textContent=frozen?'Resume':'Freeze';this.classList.toggle('warn',frozen);};

// ---- config load/save ----
const ACT=['PASS','BLOCK','OVERRIDE','GENERATE'];
async function loadCfg(){
  cfg=await (await fetch('/api/config')).json();
  // bitrate selects
  for(const sel of ['#sVb','#sTb']){$(sel).innerHTML=BR.map(x=>`<option value="${x}">${x/1000} kbps</option>`).join('');}
  $('#sVb').value=cfg.vehBitrate;$('#sTb').value=cfg.tcmBitrate;
  $('#sEn').checked=cfg.simEnabled;$('#sAuto').checked=cfg.forceAutoConfig;$('#sGen').checked=cfg.generateMissing;
  $('#sGears').value=cfg.forwardGearCount;$('#sTC').value=cfg.transConfig;$('#sAxle').value=cfg.axleRatio;
  $('#wMode').value=cfg.wifiMode;$('#wSsid').value=cfg.wifiMode==0?cfg.apSsid:cfg.staSsid;
  $('#wPass').value=cfg.wifiMode==0?cfg.apPass:cfg.staPass;
  $('#dEn').checked=cfg.diagBridge;
  $('#dReq').value='0x'+hex(cfg.diagReqId);$('#dFunc').value='0x'+hex(cfg.diagReqFunc);$('#dResp').value='0x'+hex(cfg.diagRespId);
  $('#fwInfo').textContent='Firmware '+cfg.fw+' v'+cfg.ver+'  —  '+cfg.ip;
  renderRules();
}
function renderRules(){
  $('#ruleBody').innerHTML=cfg.rules.map((r,i)=>`<tr>
    <td class="mono">0x${hex(r.id)}</td><td><input value="${r.name}" data-i="${i}" data-k="name" style="width:150px"></td>
    <td><select data-i="${i}" data-k="action">${ACT.map((a,k)=>`<option value="${k}" ${k==r.action?'selected':''}>${a}</option>`).join('')}</select></td>
    <td><input type="number" value="${r.periodMs}" data-i="${i}" data-k="periodMs" style="width:80px"></td>
    <td><button class="act sm danger" data-del="${i}">del</button></td></tr>`).join('');
  $$('#ruleBody [data-i]').forEach(el=>el.onchange=()=>{
    const r=cfg.rules[el.dataset.i];const k=el.dataset.k;
    r[k]=(k=='name')?el.value:parseInt(el.value)||0;});
  $$('#ruleBody [data-del]').forEach(el=>el.onclick=()=>{cfg.rules.splice(el.dataset.del,1);renderRules();});
}
$('#addRule').onclick=()=>{
  const id=parseInt($('#nId').value.replace(/^0x/i,''),16);
  if(isNaN(id)){toast('bad ID');return;}
  cfg.rules.push({id,name:$('#nName').value||('0x'+hex(id)),action:parseInt($('#nAct').value),periodMs:parseInt($('#nPer').value)||0});
  renderRules();
};
async function postCfg(extra){
  Object.assign(cfg,extra||{});
  const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
  toast(r.ok?'Saved':'Save failed');
}
$('#saveRules').onclick=()=>postCfg();
$('#resetRules').onclick=async()=>{await fetch('/api/defaults',{method:'POST'});await loadCfg();toast('Defaults restored');};
$('#saveSet').onclick=()=>{
  const m=parseInt($('#wMode').value);
  const hx=s=>parseInt(($('#'+s).value||'').replace(/^0x/i,''),16)||0;
  const e={simEnabled:$('#sEn').checked,forceAutoConfig:$('#sAuto').checked,generateMissing:$('#sGen').checked,
    forwardGearCount:parseInt($('#sGears').value),transConfig:parseInt($('#sTC').value),axleRatio:parseFloat($('#sAxle').value),
    vehBitrate:parseInt($('#sVb').value),tcmBitrate:parseInt($('#sTb').value),wifiMode:m,
    diagBridge:$('#dEn').checked,diagReqId:hx('dReq'),diagReqFunc:hx('dFunc'),diagRespId:hx('dResp')};
  if(m==0){e.apSsid=$('#wSsid').value;e.apPass=$('#wPass').value;}else{e.staSsid=$('#wSsid').value;e.staPass=$('#wPass').value;}
  postCfg(e);
};
$('#restart').onclick=async()=>{if(confirm('Restart the device?')){await fetch('/api/restart',{method:'POST'});toast('Restarting…');}};

// ---- inject ----
for(let i=0;i<8;i++){const I=document.createElement('input');I.className='mono';I.style.width='44px';I.value='00';I.id='ib'+i;$('#iBytes').appendChild(I);}
$('#sendInj').onclick=async()=>{
  const id=parseInt($('#iId').value.replace(/^0x/i,''),16);
  const len=Math.max(0,Math.min(8,parseInt($('#iLen').value)||0));
  const data=[];for(let i=0;i<len;i++)data.push(parseInt($('#ib'+i).value,16)||0);
  const r=await fetch('/api/inject',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,len,data})});
  toast(r.ok?'Sent to TCM bus':'Send failed');
};

loadCfg();
</script>
</body></html>)HTMLDOC";
