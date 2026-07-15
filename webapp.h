// Auto-generated from webapp/index.html — served at /app.
// Regenerate after editing the SPA (see webapp/README.md).
#pragma once
// PROGMEM: keeps this blob in flash instead of RAM (required on ESP8266,
// harmless/no-op on ESP32). Served via send_P, which reads it correctly.
const char WEBAPP_HTML[] PROGMEM = R"WEBAPP(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Three Oak Woods — E-Paper</title>
<link rel="manifest" href="manifest.webmanifest">
<meta name="theme-color" content="#2C654B">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="E-Paper">
<link rel="apple-touch-icon" href="apple-touch-icon.png">
<link rel="icon" href="icon-192.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Nunito:wght@400;700;800&display=swap" rel="stylesheet">
<style>
  :root{--green:#2C654B;--cream:#F9E7DF;--amber:#C8852A;--bark:#2B3A33;--parchment:#F5F1E6}
  *{box-sizing:border-box}
  body{font-family:'Nunito',system-ui,sans-serif;color:var(--bark);background:var(--parchment);margin:0;padding:0 16px 48px}
  .wrap{max-width:520px;margin:0 auto}
  header{display:flex;align-items:center;gap:14px;padding:22px 0 6px}
  header img{width:52px;height:52px}
  header .sub{color:var(--green);font-weight:700;font-size:.78em;letter-spacing:.05em;text-transform:uppercase}
  header h1{font-size:1.25em;font-weight:800;margin:0}
  .row{display:flex;gap:10px;flex-wrap:wrap}
  .row>*{flex:1}
  .card{background:#fff;border:1px solid #e6ddca;border-radius:12px;padding:14px 16px;margin:0 0 14px}
  .card h2{margin:0 0 10px;font-size:1.02em;color:var(--green)}
  label{font-weight:700;font-size:.85em;display:block;margin:0 0 4px}
  input,textarea,button{font-family:inherit;font-size:1.02em;padding:10px;width:100%;border-radius:8px;border:1px solid #cfc6b3;margin:4px 0}
  textarea{resize:vertical}
  button{border:0;background:var(--green);color:var(--cream);font-weight:800;cursor:pointer}
  button:hover{filter:brightness(1.08)}
  button.amber{background:var(--amber)}
  button.ghost{background:#fff;color:var(--bark);border:1px solid #cfc6b3;font-weight:700}
  .pill{display:inline-block;background:var(--cream);border-left:4px solid var(--amber);border-radius:8px;padding:8px 12px;font-weight:700}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  .stat{background:var(--parchment);border-radius:8px;padding:10px}
  .stat .k{font-size:.72em;text-transform:uppercase;letter-spacing:.04em;color:var(--green);font-weight:700}
  .stat .v{font-size:1.15em;font-weight:800;margin-top:2px}
  .muted{color:#7a786e;font-size:.85em}
  .dtbl{width:100%;border-collapse:collapse;font-size:.92em}
  .dtbl td{padding:4px 0;border-bottom:1px solid #efe9da}
  .dtbl td:last-child{text-align:right;font-weight:700}
  .dgroup{font-weight:700;text-transform:uppercase;font-size:.72em;letter-spacing:.04em;color:var(--green);margin:12px 0 2px}
  .dgroup:first-child{margin-top:2px}
  footer{text-align:center;color:var(--green);font-size:.8em;margin-top:6px;opacity:.85}
  .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#bbb;margin-right:6px;vertical-align:1px}
  .dot.on{background:var(--green)}
</style>
</head>
<body><div class="wrap">

  <header>
    <img id="logo" alt="" hidden>
    <div><div class="sub">Three Oak Woods</div><h1>E-Paper Control</h1></div>
  </header>

  <div class="card">
    <label>Device address</label>
    <div class="row" style="align-items:center">
      <input id="host" placeholder="http://192.168.12.50" style="flex:3">
      <button id="save" class="ghost" style="flex:1">Connect</button>
    </div>
    <div class="muted"><span id="dot" class="dot"></span><span id="conn">not connected</span></div>
  </div>

  <div class="card">
    <h2>Now showing</h2>
    <div class="pill" id="status">…</div>
    <div class="grid" style="margin-top:12px">
      <div class="stat"><div class="k">Battery</div><div class="v" id="batt">—</div></div>
      <div class="stat"><div class="k">Auto-cycle</div><div class="v" id="cyc">—</div></div>
    </div>
    <div class="row" style="margin-top:12px">
      <button class="ghost" data-act="/next">Next screen →</button>
      <button id="cycbtn" data-act="/cycle">Auto-cycle</button>
    </div>
  </div>

  <div class="card">
    <h2>Forecast <span class="muted" id="wxsrc" style="font-weight:400"></span></h2>
    <div id="wxbody" class="muted">—</div>
  </div>

  <div class="card">
    <h2>Backyard station <span class="muted" id="stnage" style="font-weight:400"></span></h2>
    <div id="stnbody" class="muted">—</div>
    <button class="ghost" id="stnAllToggle" style="margin-top:10px" hidden>Show all data ▾</button>
    <div id="stnAllBody" hidden style="margin-top:6px"></div>
  </div>

  <div class="card">
    <h2>Screens</h2>
    <div class="row">
      <button data-act="/clock">Clock</button>
      <button data-act="/station">Station</button>
    </div>
    <div class="row" style="margin-top:6px">
      <input id="zip" inputmode="numeric" maxlength="5" placeholder="ZIP" style="flex:1">
      <button class="amber" id="wxbtn" style="flex:2">Show weather</button>
    </div>
  </div>

  <div class="card">
    <h2>Message</h2>
    <textarea id="msg" rows="6" maxlength="400" placeholder="Type a note… (scrolls if over 4 lines)"></textarea>
    <button id="setbtn">Update display</button>
  </div>

  <div class="row">
    <button class="ghost" data-act="/clear">Clear</button>
    <button class="ghost" data-act="/refresh">Clean (de-ghost)</button>
  </div>

  <button id="install" hidden style="margin-top:10px">Install app</button>

  <p style="text-align:center;margin:14px 0 0">
    <a id="classic" href="/" style="color:var(--green);font-weight:700;text-decoration:none">&larr; Classic control page</a>
  </p>
  <footer>Three Oak Woods · e-paper web app</footer>
</div>

<script>
const $ = s => document.querySelector(s);
// Default to wherever we're served from (device serves this at /app), else a guess.
let host = localStorage.getItem('epaperHost')
  || (location.protocol.startsWith('http') ? location.origin : 'http://192.168.12.50');
$('#host').value = host;

function setHost(){
  host = $('#host').value.trim().replace(/\/$/,'');
  if(host && !/^https?:\/\//.test(host)) host = 'http://'+host;
  localStorage.setItem('epaperHost', host);
  $('#host').value = host;
  $('#logo').src = host+'/logo.svg'; $('#logo').hidden = false;
  $('#classic').href = host+'/';
  poll();
}
$('#save').onclick = setHost;

async function act(path){
  try { await fetch(host+path, {redirect:'manual', mode:'cors'}); } catch(e){}
  setTimeout(poll, 600);     // give the device a moment to redraw
}
document.querySelectorAll('[data-act]').forEach(b => b.onclick = () => act(b.dataset.act));
$('#wxbtn').onclick = () => { const z=$('#zip').value.trim(); if(z) act('/weather?zip='+encodeURIComponent(z)); };
$('#setbtn').onclick = () => act('/set?msg='+encodeURIComponent($('#msg').value));

const ICON = c => ({ '01':'☀️','02':'🌤️','03':'⛅','04':'☁️','09':'🌧️',
  '10':'🌦️','11':'⛈️','13':'❄️','50':'🌫️' }[(c||'').slice(0,2)] || '🌡️');
const COMPASS = d => (d==null||d<0) ? '' :
  ['N','NNE','NE','ENE','E','ESE','SE','SSE','S','SSW','SW','WSW','W','WNW','NW','NNW'][Math.round(d/22.5)%16];
const stat = (k,v) => `<div class="stat"><div class="k">${k}</div><div class="v">${v}</div></div>`;

function renderWeather(w){
  if(!w || !w.valid){
    $('#wxsrc').textContent = '';
    $('#wxbody').innerHTML = '<span class="muted">No weather yet — enter a ZIP below and tap Show weather.</span>';
    return;
  }
  $('#wxsrc').textContent = '· OpenWeatherMap';
  $('#wxbody').innerHTML =
    `<div style="display:flex;align-items:center;gap:14px">
       <div style="font-size:2.8em;line-height:1">${ICON(w.icon)}</div>
       <div><div style="font-size:2em;font-weight:800">${w.temp}°F</div>
         <div style="font-weight:700">${w.city||('ZIP '+w.zip)}</div>
         <div class="muted" style="text-transform:capitalize">${w.cond||''}</div></div>
     </div>
     <div class="grid" style="margin-top:12px;grid-template-columns:1fr 1fr 1fr">
       ${stat('Feels', w.feels+'°')}${stat('Humidity', w.humidity+'%')}${stat('Wind', w.wind+' mph')}
     </div>`;
}

function renderStation(s){
  if(!s || !s.received){
    $('#stnage').textContent = '';
    $('#stnbody').innerHTML = '<span class="muted">No push yet — point your Ambient console at this device.</span>';
    $('#stnAllToggle').hidden = true;
    $('#stnAllBody').hidden = true;
    return;
  }
  $('#stnage').textContent = '· live';
  const cells = [];
  if(s.tempf!=null)    cells.push(stat('Temp', Math.round(s.tempf)+'°F'));
  if(s.humidity!=null) cells.push(stat('Humidity', Math.round(s.humidity)+'%'));
  if(s.windmph!=null)  cells.push(stat('Wind', Math.round(s.windmph)+' '+COMPASS(s.winddir)));
  if(s.gustmph!=null)  cells.push(stat('Gust', Math.round(s.gustmph)+' mph'));
  if(s.dailyrain!=null)cells.push(stat('Rain today', (+s.dailyrain).toFixed(2)+'"'));
  if(s.baromin!=null)  cells.push(stat('Pressure', (+s.baromin).toFixed(2)+' inHg'));
  $('#stnbody').innerHTML = `<div class="grid" style="grid-template-columns:1fr 1fr 1fr">${cells.join('')}</div>`;
  $('#stnAllToggle').hidden = false;
  renderStationAll(s);
}

// [jsonKey, label, unit, decimals, isCompassDir]
const STN_ALL_FIELDS = [
  ['Outdoor', [
    ['tempf','Temp','°F',1], ['humidity','Humidity','%',0],
    ['windmph','Wind','mph',1], ['windmph10m','Wind (10-min avg)','mph',1],
    ['winddir','Wind direction','',0,true], ['winddir10m','Wind dir (10-min avg)','',0,true],
    ['gustmph','Gust','mph',1], ['maxdailygust','Max gust today','mph',1],
    ['uv','UV index','',0], ['solarradiation','Solar radiation','W/m²',0],
  ]],
  ['Rain', [
    ['hourlyrain','Last hour','"',2], ['eventrain','This event','"',2],
    ['dailyrain','Today','"',2], ['weeklyrain','This week','"',2],
    ['monthlyrain','This month','"',2], ['yearlyrain','This year','"',2],
  ]],
  ['Pressure', [
    ['baromin','Relative','inHg',2], ['baromabsin','Absolute','inHg',2],
  ]],
  ['Indoor', [
    ['tempinf','Temp','°F',1], ['humidityin','Humidity','%',0],
  ]],
  ['Sensors', [
    ['battout','Outdoor battery (raw)','',0], ['battin','Indoor battery (raw)','',0],
    ['batt_lightning','Lightning battery (raw)','',0], ['lightning_day','Lightning strikes today','',0],
  ]],
];

function renderStationAll(s){
  let html = '';
  for (const [group, fields] of STN_ALL_FIELDS) {
    const rows = fields.filter(([k]) => s[k] != null);
    if (!rows.length) continue;
    html += `<div class="dgroup">${group}</div><table class="dtbl">`;
    for (const [k, label, unit, dec, isDir] of rows) {
      const raw = s[k];
      const v = isDir ? (raw + '° ' + COMPASS(raw))
                       : ((typeof raw === 'number' ? raw.toFixed(dec) : raw) + (unit ? ' '+unit : ''));
      html += `<tr><td>${label}</td><td>${v}</td></tr>`;
    }
    html += '</table>';
  }
  const meta = [];
  if (s.stationtype) meta.push('Station: '+s.stationtype);
  if (s.dateutc) meta.push('Last observation (UTC): '+s.dateutc);
  if (meta.length) html += `<div class="muted" style="margin-top:8px">${meta.join(' · ')}</div>`;
  $('#stnAllBody').innerHTML = html || '<span class="muted">No extended fields reported.</span>';
}

let stnAllOpen = false;
$('#stnAllToggle').onclick = () => {
  stnAllOpen = !stnAllOpen;
  $('#stnAllBody').hidden = !stnAllOpen;
  $('#stnAllToggle').textContent = stnAllOpen ? 'Hide all data ▴' : 'Show all data ▾';
};

async function poll(){
  try {
    const r = await fetch(host+'/status.json', {mode:'cors'});
    const d = await r.json();
    $('#dot').classList.add('on'); $('#conn').textContent = 'connected · '+(d.ip||host);
    $('#status').textContent = ({clock:'Clock (date & time)',text:d.text||'Text',
      weather:'Weather — '+(d.weather?.summary||d.weather?.zip||''),
      station:'Station — '+(d.station?.summary||'')})[d.mode] || d.mode;
    $('#batt').textContent = d.battValid
      ? (d.battPct+'%  ·  '+(+d.battVolts).toFixed(2)+' V')
      : ('no cell · GPIO34 '+(d.battRawMv ?? '?')+' mV');
    $('#cyc').innerHTML = '<span class="dot '+(d.autoCycle?'on':'')+'"></span>'+(d.autoCycle?'On':'Off');
    $('#cycbtn').textContent = d.autoCycle ? 'Stop cycling' : 'Auto-cycle';
    renderWeather(d.weather);
    renderStation(d.station);
    if(d.weather?.zip && !$('#zip').value) $('#zip').value = d.weather.zip;
    // Show the device's current message — but don't clobber what the user is typing.
    if(document.activeElement !== $('#msg') && d.text != null) $('#msg').value = d.text;
  } catch(e){
    $('#dot').classList.remove('on'); $('#conn').textContent = 'not connected — check the address';
  }
}

if(host){ $('#logo').src = host+'/logo.svg'; $('#logo').hidden = false; $('#classic').href = host+'/'; }
poll();
setInterval(poll, 5000);

// ---- PWA: service worker (secure contexts only) + install button ----
if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('sw.js').catch(()=>{});
}
let deferredPrompt = null;
window.addEventListener('beforeinstallprompt', e => {
  e.preventDefault(); deferredPrompt = e; $('#install').hidden = false;
});
$('#install').onclick = async () => {
  if (!deferredPrompt) return;
  deferredPrompt.prompt(); await deferredPrompt.userChoice;
  deferredPrompt = null; $('#install').hidden = true;
};
window.addEventListener('appinstalled', () => { $('#install').hidden = true; });
</script>
</body>
</html>

)WEBAPP";
