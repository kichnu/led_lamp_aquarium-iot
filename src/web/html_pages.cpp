#include "html_pages.h"

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermo Control</title>
<style>
  :root {
    --bg-primary:#0a0f1a; --bg-card:#111827; --bg-input:#1e293b; --border:#2d3a4f;
    --text-primary:#f1f5f9; --text-secondary:#94a3b8; --text-muted:#64748b;
    --accent-blue:#38bdf8; --accent-blue-dark:#1d4ed8; --accent-cyan:#22d3d5; --accent-green:#22c55e;
    --accent-red:#ef4444; --accent-orange:#f97316; --accent-yellow:#eab308;
    --radius:12px; --radius-sm:8px; --radius-lg:16px;
    --font-xs:.72rem; --font-sm:.8rem; --font-md:.9rem; --font-lg:1.05rem;
    --transition-fast:.15s ease;
    --shadow:0 4px 24px rgba(0,0,0,.4);
    --service-bg:rgba(249,115,22,0.10); --service-bg-hover:rgba(249,115,22,0.20);
    --service-border:rgba(249,115,22,0.35); --service-text:#f97316;
  }
  /* Bez tego padding/border liczy się PO podziale przestrzeni przez flex —
     .fan-stepper (0 własnego paddingu + 2px border) i przyciski (40px
     paddingu + 2px border) wychodziły na różne szerokości mimo flex:1 1 0. */
  *, *::before, *::after { box-sizing:border-box; }
  body { font-family: sans-serif; background:var(--bg-primary); color:var(--text-primary); margin:0; padding:0; }
  .container { max-width:800px; margin:0 auto; padding:16px; }
  h1 { color:var(--text-primary); margin:0; font-size:1.25rem; font-weight:700; letter-spacing:-0.02em; }
  h2 { color:var(--text-secondary); font-size:var(--font-md); font-weight:600; text-transform:uppercase; letter-spacing:.05em; margin:0 0 8px; }
  .card { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin:8px 0; box-shadow:var(--shadow); }
  .temp { display:flex; align-items:center; flex-wrap:wrap; column-gap:14px; row-gap:2px; font-size:3em; color:var(--text-primary); }
  /* Kolor #temp wg stanu (2026-07-25, rozszerzone 2026-08-15): heating=niebieski,
     thermal-buffer=żółty (cicha uwaga o buforze dzień/noc, target..fan_on_point),
     cooling=pomarańczowy (fan aktywny, fan_on_point..alarm_delta_high — bez tego
     ten zakres wypadał "biały", jakby nic się nie działo), alarm HIGH/TREND/
     SENSOR=czerwony, alarm LOW=wyraźny ciemny niebieski (odróżnia się od zwykłego
     heating-blue, mimo że tło/border karty i tak idzie na czerwono jak przy
     każdym alarmie). */
  #temp.temp-heating  { color:var(--accent-blue); }
  #temp.temp-buffer   { color:var(--accent-yellow); }
  #temp.temp-cooling  { color:var(--accent-orange); }
  #temp.temp-alarm    { color:var(--accent-red); }
  #temp.temp-alarm-low{ color:var(--accent-blue-dark); }
  #temp { transition:font-size .2s ease; }

  /* Nagłówek karty (wzorzec z dolewki: ikona-badge + uppercase h2 + separator) */
  .logo { display:flex; align-items:center; gap:12px; }
  .logo-icon { width:40px; height:40px; background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); border-radius:var(--radius-sm); display:flex; align-items:center; justify-content:center; flex-shrink:0; }
  .logo-icon svg { width:24px; height:24px; fill:var(--bg-primary); }
  .card-header { display:flex; align-items:center; gap:10px; margin-bottom:16px; padding-bottom:12px; border-bottom:1px solid var(--border); }
  .card-header h2 { margin:0; }
  .card-header-icon { width:28px; height:28px; border-radius:6px; display:flex; align-items:center; justify-content:center; flex-shrink:0; }
  .card-header-icon svg { width:16px; height:16px; }

  /* Pierwsza karta: System Status — wzorzec dla pozostałych kart */
  .status-main { display:flex; align-items:center; justify-content:space-between; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); padding:12px 16px; gap:12px; position:relative; }
  .status-main.status-ok { border-color:rgba(34,197,94,0.35); background:rgba(34,197,94,0.05); }
  .status-main.status-ok::after { content:''; position:absolute; top:8px; right:8px; width:6px; height:6px; background:var(--accent-green); border-radius:50%; animation:pulse 2s infinite; }
  .status-main.status-error { border-color:rgba(239,68,68,0.4); background:rgba(239,68,68,0.06); }
  .status-main.status-warn { border-color:rgba(234,179,8,0.4); background:rgba(234,179,8,0.06); }
  .status-main.status-disabled { border-color:rgba(148,163,184,0.35); background:rgba(148,163,184,0.05); }
  .status-main-body { flex:1; min-width:0; }
  .status-main-sub { display:flex; flex-wrap:wrap; align-items:center; gap:8px; margin-top:8px; font-size:var(--font-sm); color:var(--text-secondary); }
  .sub-on { color:var(--accent-green); } .sub-off { color:var(--text-muted); }
  .sub-warn { color:var(--accent-yellow); } .sub-danger { color:var(--accent-red); }
  .sub-sep { color:var(--border); }
  .status-main-wifi { display:flex; flex-direction:column; align-items:center; gap:2px; flex-shrink:0; }
  .card-header .status-main-wifi { margin-left:auto; }
  .wifi-label { font-size:.6rem; color:var(--text-muted); text-transform:uppercase; letter-spacing:.05em; }
  .wifi-dot { font-size:1.1rem; line-height:1; }
  .status-main-wifi.wifi-on .wifi-dot { color:var(--accent-green); }
  .status-main-wifi.wifi-off .wifi-dot { color:var(--text-muted); }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.5} }

  /* Karta Energy — wzorzec wielkości z .temp (System Status), ale bez
     obramowanego boxa (--text-secondary zamiast --accent-blue, brak
     .status-main). Reset+timestamp we wspólnym rodzicu (.energy-reset-group),
     żeby na mobile poprawnie wylądowały POD licznikiem (column), a na
     desktopie po prawej (row, patrz .energy-row). */
  .energy-row { display:flex; align-items:center; justify-content:space-between; gap:16px; flex-wrap:wrap; }
  .energy-total { font-size:1.5em; color:var(--text-secondary); font-weight:300; }
  .energy-total-value>span { font-size:2.5em; }
  /* Heartbeat: krótki flash przy przeskoczeniu o kolejne 0.01 kWh (patrz
     refresh() — porównanie zaokrąglonego stringa, nie samego pollingu 5s). */
  @keyframes energy-tick { 0%{ color:var(--accent-yellow); } 100%{ color:inherit; } }
  .energy-total-value.energy-tick>span { animation:energy-tick .8s ease; }

  .energy-reset-group { display:flex; flex-direction:column; align-items:flex-end; gap:8px; }
  .energy-reset-info { font-size:var(--font-sm); color:var(--text-secondary); text-align:right; }
  .energy-reset-info b { color:var(--text-primary); font-family:'Courier New',monospace; }
  @media (max-width:600px) {
    .energy-row { flex-direction:column; align-items:stretch; }
    /* Label i wartość+jednostka w osobnych spanach (.energy-total-label/-value)
       specjalnie po to, żeby wymusić podział na dwie linie TYLKO na mobile —
       na desktopie oba zostają inline w jednym wierszu (bez zmian). */
    .energy-total-label, .energy-total-value { display:block; }
    .energy-total-value>span { font-size:2em; }
    .energy-reset-group { align-items:stretch; }
    .energy-reset-group button { width:100%; }
    .energy-reset-info { text-align:center; }
  }

  .btn-service { background:var(--service-bg); border-color:var(--service-border); color:var(--service-text); }
  .btn-service:hover { background:var(--service-bg-hover); border-color:var(--service-border); }
  .badge { display:inline-block; padding:4px 10px; border-radius:12px; font-size:.85em; font-weight:600; border:1px solid transparent; }
  .idle    { background:rgba(34,197,94,0.15);  color:var(--accent-green);  border-color:rgba(34,197,94,0.3); }
  .heating { background:rgba(234,179,8,0.15);  color:var(--accent-yellow); border-color:rgba(234,179,8,0.3); }
  .cooling { background:rgba(34,211,213,0.15); color:var(--accent-cyan);   border-color:rgba(34,211,213,0.3); }
  .alarm   { background:rgba(239,68,68,0.15);  color:var(--accent-red);    border-color:rgba(239,68,68,0.3); }
  /* Panel "Algorithm Settings" — wzorzec z dolewki (settings.png): przycisk
     ⚙ Settings poza kartą, cała karta domyślnie ukryta (display:none), grid
     dużych, wyśrodkowanych wartości monospace zamiast wąskich .row. */
  .settings-toggle-btn { margin:8px 0; font-size:var(--font-xs); padding:6px 10px; border-radius:6px; border:1px solid var(--border); background:var(--bg-input); color:var(--text-muted); cursor:pointer; }
  .settings-toggle-btn:hover { border-color:var(--accent-cyan); color:var(--text-primary); }
  .settings-title { font-size:var(--font-xs); font-weight:600; color:var(--text-muted); letter-spacing:.05em; text-transform:uppercase; margin-bottom:10px; }
  .settings-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(140px,1fr)); gap:8px 14px; }
  .settings-field { font-size:var(--font-xs); color:var(--text-muted); }
  .settings-field input { display:block; width:100%; margin-top:3px; padding:10px 14px; font-family:'Courier New',monospace; font-size:1rem; text-align:center; background:var(--bg-primary); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); }
  .settings-field input:focus { outline:none; border-color:var(--accent-cyan); box-shadow:0 0 0 3px rgba(34,211,213,0.2); }
  button { display:flex; align-items:center; justify-content:center; gap:8px; padding:10px 20px; border-radius:var(--radius-sm); border:1px solid var(--border); background:var(--bg-input); color:var(--text-primary); cursor:pointer; margin:4px 4px 4px 0; font-family:inherit; font-size:.9rem; font-weight:600; transition:all var(--transition-fast); }
  button:hover { border-color:var(--accent-cyan); }
  button:disabled { opacity:.45; cursor:not-allowed; }
  button:disabled:hover { border-color:var(--border); }
  button.primary { background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); border:none; color:var(--bg-primary); }
  button.danger { background:rgba(239,68,68,0.15); border-color:rgba(239,68,68,0.3); color:var(--accent-red); }

  /* Rząd przycisków wypełniający całą dostępną szerokość (System Control) */
  .btn-row { display:flex; flex-wrap:wrap; gap:8px; }
  .btn-row > button, .btn-row > .fan-stepper { flex:1 1 0; min-width:0; margin:0; padding-left:20px; padding-right:20px;}
  @media (max-width:600px) {
    .btn-row { flex-direction:column; }
    /* flex-basis:0 na osi pionowej (column) + overflow:hidden na .fan-stepper
       zerowały jego min-content wysokość (klasyczny problem flex
       min-height:auto+overflow) — w kolumnie liczy się naturalna wysokość,
       nie równy podział, więc flex-basis wraca do auto. */
    .btn-row > button, .btn-row > .fan-stepper { flex:0 0 auto; width:100%; }
  }
  table { border-collapse: collapse; width:100%; font-size:var(--font-sm); }
  th, td { text-align:left; padding:4px 8px; border-bottom:1px solid var(--border); color:var(--text-secondary); }
  .err-text { color:var(--accent-red); font-size:var(--font-sm); }

  /* Wykresy temperatury (hourly + daily), portowane z chart_prototype/daily_alarms.html
     (2026-07-17) — jedna karta, dwa niezależne wykresy (godzinowy/dobowy), ręczne
     odświeżanie przyciskiem (bez pollingu, patrz loadCharts()). */
  .chart-sub-title { color:var(--text-secondary); font-size:var(--font-sm); font-weight:600; text-transform:uppercase; letter-spacing:.05em; margin:16px 0 10px; }
  .chart-sub-title:first-of-type { margin-top:0; }
  .legend { display:flex; gap:14px; flex-wrap:wrap; font-size:var(--font-xs); color:var(--text-muted); margin-bottom:10px; }
  .legend span.dot { display:inline-block; width:11px; height:11px; border-radius:50%; margin-right:5px; vertical-align:middle; }
  .chart-body { display:flex; align-items:stretch; gap:0; }
  .chart-nav-btn { flex-shrink:0; width:32px; border:1px solid var(--border); background:var(--bg-input); color:var(--text-secondary); cursor:pointer; font-size:1rem; border-radius:var(--radius-sm); transition:all var(--transition-fast); margin:0; padding:0; }
  .chart-nav-btn:hover { border-color:var(--accent-cyan); color:var(--text-primary); }
  .chart-nav-btn:disabled { opacity:.3; cursor:not-allowed; }
  .chart-nav-btn:disabled:hover { border-color:var(--border); color:var(--text-secondary); }
  @media (max-width:600px) { .chart-nav-btn { display:none; } }
  .chart-yaxis { flex-shrink:0; width:42px; position:relative; margin:0 6px 0 0; }
  .chart-yaxis .tick { position:absolute; left:0; transform:translateY(-50%); font-size:var(--font-xs); color:var(--text-muted); white-space:nowrap; }
  .chart-yaxis .target-tick { color:var(--accent-blue); font-weight:600; }
  .chart-scroll { flex:1; min-width:0; overflow-x:auto; overflow-y:hidden; -webkit-overflow-scrolling:touch; touch-action:pan-x; scrollbar-width:thin; scrollbar-color:var(--border) transparent; }
  .chart-scroll::-webkit-scrollbar { height:6px; }
  .chart-scroll::-webkit-scrollbar-thumb { background:var(--border); border-radius:3px; }
  .chart-inner { position:relative; }
  .plot-svg { display:block; }
  .chart-labels { display:flex; }
  .chart-label { display:flex; align-items:flex-start; justify-content:center; flex-shrink:0; }
  .chart-label span { display:inline-block; transform:rotate(-90deg); white-space:nowrap; font-size:.7rem; color:var(--text-muted); font-family:'Courier New',monospace; margin-top:8px; }
  .chart-label.now span { color:var(--accent-cyan); font-weight:700; }
  .now-marker { stroke:var(--accent-cyan); stroke-width:1; stroke-dasharray:3 3; opacity:.6; }
  .target-marker { stroke:var(--accent-blue); stroke-width:1; stroke-dasharray:5 3; opacity:.55; }

  /* Topbar + lock */
  .topbar { display:flex; align-items:center; justify-content:space-between; margin-bottom:8px; }
  .topbar-actions { display:flex; align-items:center; gap:8px; }
  .btn-back { background:var(--bg-input); border:1px solid var(--border); color:var(--text-secondary); padding:8px 16px; border-radius:var(--radius-sm); font-size:.875rem; font-weight:500; cursor:pointer; transition:all .2s; }
  .btn-back:hover { background:var(--bg-card); color:var(--text-primary); }
  .lock-btn { display:inline-flex; align-items:center; gap:5px; background:none; border:1px solid var(--border); border-radius:var(--radius-sm); padding:8px 16px; font-size:var(--font-sm); font-weight:600; cursor:pointer; transition:color var(--transition-fast),border-color var(--transition-fast); }
  .lock-btn.locked { color:var(--text-muted); }
  .lock-btn.unlocked { color:var(--accent-green); border-color:var(--accent-green); }
  .lock-form-bar { display:none; gap:8px; align-items:center; background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius-sm); padding:8px 12px; margin-bottom:8px; }
  .lock-form-bar.visible { display:flex; }
  @keyframes lock-shake { 0%,100%{transform:translateX(0)} 25%{transform:translateX(-6px)} 75%{transform:translateX(6px)} }
  .shake-inp { animation:lock-shake .35s; }
  .lock-pwd-input { flex:1; height:34px; padding:0 10px; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); font-size:var(--font-md); outline:none; min-width:0; }
  .lock-pwd-input.error { border-color:var(--accent-red); }
  .lock-pwd-msg { font-size:var(--font-xs); color:var(--accent-red); min-width:100px; }
  @media (max-width:600px) {
    .lock-pwd-msg { min-width:20px; }
  }
  .lock-close-btn { background:none; border:none; color:var(--text-muted); font-size:18px; cursor:pointer; padding:0 6px; }
  body.editing-locked .lockable-btn { pointer-events:none; opacity:.45; }
  body.editing-locked .lockable-btn::before { content:''; display:inline-block; width:11px; height:11px; vertical-align:-1px; margin-right:4px;
    background:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23f1f5f9' stroke-width='2.5'%3E%3Crect x='3' y='11' width='18' height='11' rx='2'/%3E%3Cpath d='M7 11V7a5 5 0 0 1 10 0v4'/%3E%3C/svg%3E") no-repeat center/contain; }

  /* Modal */
  .modal-overlay { position:fixed; top:0; left:0; right:0; bottom:0; background:rgba(0,0,0,.7); backdrop-filter:blur(4px); display:flex; align-items:center; justify-content:center; z-index:1000; opacity:0; visibility:hidden; transition:all .2s ease; }
  .modal-overlay.show { opacity:1; visibility:visible; }
  .modal-box { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius-lg); padding:24px; max-width:360px; width:90%; transform:scale(.9); transition:transform .2s ease; }
  .modal-overlay.show .modal-box { transform:scale(1); }
  .modal-icon { width:48px; height:48px; margin:0 auto 16px; border-radius:50%; display:flex; align-items:center; justify-content:center; }
  .modal-icon svg { width:24px; height:24px; }
  .modal-icon.ok   { background:rgba(34,197,94,.15); }  .modal-icon.ok svg   { color:var(--accent-green); }
  .modal-icon.err  { background:rgba(239,68,68,.15); }  .modal-icon.err svg  { color:var(--accent-red); }
  .modal-icon.warn { background:rgba(234,179,8,.15); }  .modal-icon.warn svg { color:var(--accent-yellow); }
  .modal-icon.info { background:rgba(56,189,248,.15); } .modal-icon.info svg { color:var(--accent-blue); }
  .modal-title { font-size:var(--font-lg); font-weight:700; text-align:center; margin-bottom:8px; }
  .modal-text { font-size:var(--font-sm); text-align:center; color:var(--text-secondary); margin-bottom:20px; line-height:1.5; }
  .modal-actions { display:flex; gap:10px; }
  .modal-actions button { flex:1; margin:0; }

  /* Stepper prędkości wentylatora — wygląda jak jeden przycisk (kolory/wysokość
     spójne z resztą), podzielony na dwie strefy kliknięcia ze strzałkami */
  .fan-stepper { display:flex; align-items:stretch; border-radius:var(--radius-sm); border:1px solid var(--border); background:var(--bg-input); overflow:hidden; transition:all var(--transition-fast); }
  .fan-stepper.active { border-color:var(--service-border); background:var(--service-bg); }
  .fan-stepper.disabled { opacity:.45; pointer-events:none; }
  .fan-step-btn { background:none; border:none; color:var(--text-primary); padding:10px 14px; cursor:pointer; font-size:.9rem; margin:0; border-radius:0; }
  .fan-step-btn:hover { background:rgba(255,255,255,0.08); }
  .fan-step-btn:disabled { opacity:1; cursor:not-allowed; } /* fade już daje .fan-stepper.disabled na całości, bez podwójnego przyciemnienia */
  .fan-step-val { flex:1; padding:0 4px; font-size:.9rem; font-weight:600; color:var(--text-primary); min-width:46px; text-align:center; align-self:center; }
  .fan-stepper.active .fan-step-val, .fan-stepper.active .fan-step-btn { color:var(--service-text); }
</style>
</head>
<body>
<div class="container">

<div class="topbar">
  <div class="logo">
    <div class="logo-icon">
      <svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>
    </div>
    <h1>Thermo Control</h1>
  </div>
  <div class="topbar-actions">
    <button class="lock-btn locked" id="lockBtn" onclick="toggleLock()">LOCKED</button>
    <button class="btn-back" id="btnLogout">Back</button>
  </div>
</div>
<form id="lockFormBar" class="lock-form-bar" onsubmit="submitUnlock();return false;" autocomplete="off">
  <input type="password" id="lockPwdInput" class="lock-pwd-input" placeholder="PIN…" inputmode="numeric" maxlength="8" pattern="[0-9]*" autocomplete="one-time-code">
  <span id="lockFormMsg" class="lock-pwd-msg"></span>
  <button type="submit">Unlock</button>
  <button type="button" class="lock-close-btn" onclick="hideLockForm()">&#215;</button>
</form>

<!-- Pierwsza karta: SYSTEM STATUS — wzorzec dla pozostałych kart (2026-07-15) -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(56,189,248,0.15);">
      <svg fill="currentColor" style="color:var(--accent-blue);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
    </div>
    <h2>System Status</h2>
    <div class="status-main-wifi wifi-off" id="wifiItem">
      <span class="wifi-label">WiFi</span>
      <span class="wifi-dot" id="wifiStatus">●</span>
    </div>
  </div>
  <div class="status-main status-ok" id="statusMain">
    <div class="status-main-body">
      <div class="temp"><span id="temp">--.-°C</span></div>
      <div class="status-main-sub">
        <span class="badge idle" id="state">IDLE</span>
        <span class="sub-sep">•</span>
        <span>Heater: <span class="badge" id="heater">OFF</span></span>
        <span class="sub-sep">•</span>
        <span>Fan: <span class="badge" id="fan">0%</span></span>
        <span class="sub-sep">•</span>
        <span>Alarms: <span class="badge" id="alarms">none</span></span>
      </div>
    </div>
  </div>
</div>

<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(234,179,8,0.15);">
      <svg fill="currentColor" style="color:var(--accent-yellow);" viewBox="0 0 24 24"><path d="M11 21h-1l1-7H7.5c-.58 0-.57-.32-.38-.66C8.48 10.94 10.42 7.54 13.01 3h1l-1 7h3.51c.4 0 .58.19.36.66C13.94 15.35 11 21 11 21z"/></svg>
    </div>
    <h2>Energy</h2>
  </div>
  <div class="energy-row">
    <div class="energy-total"><span class="energy-total-label">Total Energy:</span> <span class="energy-total-value"><span id="energy_total">00.00</span> kWh</span></div>
    <div class="energy-reset-group">
      <button class="lockable-btn" id="btnEnergyReset">Reset Total Counter</button>
      <div class="energy-reset-info">Last Reset: <b id="energyResetTs">never</b></div>
    </div>
  </div>
</div>

<!-- Druga karta: SYSTEM CONTROL — te same elementy graficzne co System Status.
     Ręczne sterowanie (heater/fan) i karta System/Service Mode jako całość
     to jedyny wyjątek od mechanizmu PIN-lock (potwierdzone przez użytkownika,
     2026-07-15) — dodatkowo ręczne przyciski są aktywne TYLKO w Service Mode
     (patrz .manual-ctrl w refresh()). -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(34,197,94,0.15);">
      <svg fill="currentColor" style="color:var(--accent-green);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 14.5v-9l6 4.5-6 4.5z"/></svg>
    </div>
    <h2>System Control</h2>
  </div>

  <div class="btn-row">
    <button id="btnSystemToggle">…</button>
    <button id="btnAck">Mute Alarm</button>
    <button id="btnResetSensor" class="danger">Reset Sensor Fault</button>
  </div>
  <div id="sensorFaultBanner" style="display:none; margin-top:8px; color:var(--accent-red); font-weight:bold;">
    ⚠ SENSOR FAULT (critical) — heater and fan frozen until manually confirmed
  </div>

  <div class="btn-row" style="margin-top:14px; padding-top:14px; border-top:1px solid var(--border);">
    <button class="manual-ctrl" id="heaterToggle">Heater: OFF</button>
    <button class="manual-ctrl" id="fanToggle">Fan: OFF</button>
    <div class="fan-stepper manual-ctrl disabled" id="fanStepper">
      <button type="button" class="fan-step-btn" onclick="stepFan(-1)">▼</button>
      <span class="fan-step-val" id="fanStepVal">0%</span>
      <button type="button" class="fan-step-btn" onclick="stepFan(1)">▲</button>
    </div>
  </div>
</div>

<button id="algSettingsBtn" class="settings-toggle-btn" onclick="toggleAlgSettings()">⚙ Settings</button>

<div class="card" id="algSettingsPanel" style="display:none;">
  <div class="settings-title">Algorithm Parameters</div>
  <div class="settings-grid">
    <label class="settings-field">Target temperature [°C]<input type="number" step="0.1" id="target_temp"></label>
    <label class="settings-field">Heater hysteresis [°C]<input type="number" step="0.1" min="0.1" id="heat_hyst"></label>
    <label class="settings-field">Thermal buffer [°C]<input type="number" step="0.1" id="thermal_buffer"></label>
    <label class="settings-field">Fan hysteresis [°C]<input type="number" step="0.1" min="0.1" id="cool_start"></label>
    <label class="settings-field">Fan 100% [°C]<input type="number" step="0.1" id="cool_full"></label>
    <label class="settings-field">Minimum fan speed [%]<input type="number" step="1" min="0" max="100" id="fan_min_pct"></label>
    <label class="settings-field">ALARM_LOW threshold [°C]<input type="number" step="0.1" id="alarm_delta_low"></label>
    <label class="settings-field">ALARM_HIGH threshold [°C]<input type="number" step="0.1" id="alarm_delta_high"></label>
    <label class="settings-field">Heater fault: min. temp rise [°C]<input type="number" step="0.1" id="trend_alarm_delta"></label>
    <label class="settings-field">Heater fault: check window [min]<input type="number" step="1" min="5" id="trend_window_min"></label>
    <label class="settings-field">Sensor calibration [°C]<input type="number" step="0.1" id="temp_offset"></label>
    <label class="settings-field">Reserved<input type="number" step="1" id="heater_watt"></label>
    <label class="settings-field">Pulses / kWh (energy meter)<input type="number" step="1" id="pulses_per_kwh"></label>
  </div>
  <div class="btn-row" style="margin-top:10px;">
    <button class="lockable-btn primary" id="btnSaveThermo">Save</button>
    <button type="button" onclick="toggleAlgSettings()">Cancel</button>
  </div>
</div>

<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(56,189,248,0.15);">
      <svg fill="none" stroke="currentColor" stroke-width="2" style="color:var(--accent-blue);" viewBox="0 0 24 24"><polyline points="3,17 9,11 13,15 21,7"/><polyline points="15,7 21,7 21,13"/></svg>
    </div>
    <h2>Temperature History</h2>
    <div class="legend" id="chartLegend" style="margin-left:auto; margin-bottom:0;">
      <span><span class="dot" style="background:var(--accent-blue);"></span>LOW</span>
      <span><span class="dot" style="background:var(--accent-red);"></span>HIGH</span>
      <span><span class="dot" style="background:var(--accent-yellow);"></span>TREND</span>
      <span><span class="dot" style="background:var(--accent-red); width:14px; height:14px;"></span>SENSOR</span>
    </div>
  </div>

  <div class="chart-sub-title">Hourly (rolling 24h)</div>
  <div class="chart-body">
    <button class="chart-nav-btn" id="prevBtnHourly" title="Older">&#9664;</button>
    <div class="chart-yaxis" id="yAxisHourly"></div>
    <div class="chart-scroll" id="scrollHourly">
      <div class="chart-inner" id="innerHourly">
        <svg class="plot-svg" id="svgHourly"></svg>
        <div class="chart-labels" id="labelsHourly"></div>
      </div>
    </div>
    <button class="chart-nav-btn" id="nextBtnHourly" title="Newer">&#9654;</button>
  </div>

  <div class="chart-sub-title">Daily (~1 year)</div>
  <div class="chart-body">
    <button class="chart-nav-btn" id="prevBtnDaily" title="Older">&#9664;</button>
    <div class="chart-yaxis" id="yAxisDaily"></div>
    <div class="chart-scroll" id="scrollDaily">
      <div class="chart-inner" id="innerDaily">
        <svg class="plot-svg" id="svgDaily"></svg>
        <div class="chart-labels" id="labelsDaily"></div>
      </div>
    </div>
    <button class="chart-nav-btn" id="nextBtnDaily" title="Newer">&#9654;</button>
  </div>

  <button id="btnLoadCharts" style="margin-top:14px;">Refresh Charts</button>
</div>

<div class="modal-overlay" id="alertModal">
  <div class="modal-box">
    <div class="modal-icon" id="alertIcon"></div>
    <div class="modal-title" id="alertTitle"></div>
    <div class="modal-text" id="alertText"></div>
    <div class="modal-actions" id="alertActions"></div>
  </div>
</div>

<script>
// AlarmEventType w algorithm_config.h — jeden wpis = fakt wystąpienia, bez start/end
const ALARM_EVENT_NAMES = ['LOW','HIGH','TREND','SENSOR'];

// Mirror FAN_MANUAL_DEFAULT_PCT z algorithm_config.h — brak mechanizmu
// wstrzykiwania #define do PROGMEM, wartość trzeba synchronizować ręcznie.
const FAN_MANUAL_DEFAULT_PCT = 50;
const FAN_STEP_PCT = 5;

// ── Modal boxes (zamiast alert()/confirm()) ─────────────────────────────
const MODAL_ICONS = {
  ok:   '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><polyline points="22,4 12,14.01 9,11.01"/></svg>',
  err:  '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>',
  warn: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
  info: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
};
let alertCallback = null;
let alertAutoHideTimer = null;
const ALERT_AUTO_HIDE_MS = 1500;

// Auto-hide tylko dla showAlert (czysto informacyjny modal) — showConfirm
// wymaga jawnej decyzji (Confirm/Cancel), więc nigdy nie znika sam.
function showAlert(title, msg, type) {
  clearTimeout(alertAutoHideTimer);
  document.getElementById('alertIcon').className = 'modal-icon ' + type;
  document.getElementById('alertIcon').innerHTML = MODAL_ICONS[type] || MODAL_ICONS.info;
  document.getElementById('alertTitle').textContent = title;
  document.getElementById('alertText').textContent = msg;
  alertCallback = null;
  document.getElementById('alertActions').innerHTML = '<button onclick="closeAlert()">OK</button>';
  document.getElementById('alertModal').classList.add('show');
  alertAutoHideTimer = setTimeout(closeAlert, ALERT_AUTO_HIDE_MS);
}
function showConfirm(title, msg, type, onConfirm) {
  clearTimeout(alertAutoHideTimer);
  document.getElementById('alertIcon').className = 'modal-icon ' + type;
  document.getElementById('alertIcon').innerHTML = MODAL_ICONS[type] || MODAL_ICONS.info;
  document.getElementById('alertTitle').textContent = title;
  document.getElementById('alertText').textContent = msg;
  document.getElementById('alertActions').innerHTML =
    '<button onclick="closeAlert()">Cancel</button><button class="primary" onclick="closeAlert(true)">Confirm</button>';
  alertCallback = onConfirm;
  document.getElementById('alertModal').classList.add('show');
}
function closeAlert(confirmed) {
  clearTimeout(alertAutoHideTimer);
  document.getElementById('alertModal').classList.remove('show');
  if (confirmed && alertCallback) alertCallback();
  alertCallback = null;
}
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeAlert(); });
document.addEventListener('click', (e) => { if (e.target.id === 'alertModal') closeAlert(); });

// ── Lock edycji GUI (PIN, osobny od hasła logowania) ────────────────────
const LOCK_TIMEOUT_MS = 5 * 60 * 1000;
let isLocked = true, lockTimer = null, lockUnlockAt = 0;
let lockFailedAttempts = 0, lockThrottleUntil = 0, _lockCdInt = null;

function toggleLock() { if (isLocked) showLockForm(); else setLocked(true); }
function showLockForm() {
  document.getElementById('lockFormBar').classList.add('visible');
  const i = document.getElementById('lockPwdInput'); i.value = ''; i.focus();
  document.getElementById('lockFormMsg').textContent = '';
}
function hideLockForm() {
  document.getElementById('lockFormBar').classList.remove('visible');
  document.getElementById('lockPwdInput').value = '';
  document.getElementById('lockFormMsg').textContent = '';
}
function setLocked(locked) {
  isLocked = locked;
  if (locked) {
    clearTimeout(lockTimer); lockTimer = null;
    clearInterval(_lockCdInt); _lockCdInt = null;
    hideLockForm();
  } else {
    lockUnlockAt = Date.now() + LOCK_TIMEOUT_MS;
    resetLockTimer();
    startLockCountdown();
    hideLockForm();
  }
  updateLockUI();
}
function resetLockTimer() {
  if (isLocked) return;
  clearTimeout(lockTimer);
  lockUnlockAt = Date.now() + LOCK_TIMEOUT_MS;
  lockTimer = setTimeout(() => setLocked(true), LOCK_TIMEOUT_MS);
}
function lockMinsLeft() { return Math.max(1, Math.ceil((lockUnlockAt - Date.now()) / 60000)); }
function startLockCountdown() {
  clearInterval(_lockCdInt);
  _lockCdInt = setInterval(() => { if (isLocked) clearInterval(_lockCdInt); else updateLockUI(); }, 60000);
}
function updateLockUI() {
  const btn = document.getElementById('lockBtn');
  if (isLocked) { btn.className = 'lock-btn locked'; btn.textContent = 'LOCKED'; }
  else { btn.className = 'lock-btn unlocked'; btn.textContent = 'EDITING · ' + lockMinsLeft() + 'm'; }
  document.body.classList.toggle('editing-locked', isLocked);
}
function submitUnlock() {
  if (Date.now() < lockThrottleUntil) {
    document.getElementById('lockFormMsg').textContent = 'Wait ' + Math.ceil((lockThrottleUntil - Date.now()) / 1000) + 's…';
    return;
  }
  const inp = document.getElementById('lockPwdInput');
  const msg = document.getElementById('lockFormMsg');
  if (!inp.value) { inp.focus(); return; }
  fetch('api/verify-pin', { method: 'POST', body: new URLSearchParams({ pin: inp.value }) })
    .then(r => r.json()).then(d => {
      if (d.success) { lockFailedAttempts = 0; setLocked(false); }
      else {
        lockFailedAttempts++;
        inp.value = ''; inp.focus();
        inp.classList.add('error', 'shake-inp');
        setTimeout(() => inp.classList.remove('error', 'shake-inp'), 400);
        if (lockFailedAttempts >= 3) { lockThrottleUntil = Date.now() + 30000; lockFailedAttempts = 0; msg.textContent = 'Too many attempts. Wait 30s.'; }
        else msg.textContent = 'Wrong PIN (' + (3 - lockFailedAttempts) + ' left)';
      }
    }).catch(() => { msg.textContent = 'Error'; });
}

// Włącza/wyłącza stepper (div, nie <button> — .disabled CSS samo w sobie
// wystarczy przez pointer-events:none, ale disabled na wewnętrznych
// przyciskach robimy jawnie, żeby nie polegać wyłącznie na dziedziczeniu CSS).
function setStepperEnabled(el, enabled) {
  el.classList.toggle('disabled', !enabled);
  el.querySelectorAll('button').forEach(b => { b.disabled = !enabled; });
}

// ── Status / refresh ────────────────────────────────────────────────────
let currentFanPct = 0;
let currentHeaterOn = false;
let currentFanOn = false;
let serviceModeActive = false;
let lastEnergyKwhStr = null;

async function refresh() {
  try {
    const r = await fetch('api/status');
    if (r.status === 401) { window.location.href = 'login'; return; }
    const d = await r.json();
    document.getElementById('temp').textContent = d.temp_c.toFixed(1) + '°C';
    document.getElementById('state').textContent = d.state;
    document.getElementById('heater').textContent = d.heater_on ? 'ON' : 'OFF';
    document.getElementById('fan').textContent = d.fan_pct + '%';
    document.getElementById('fanStepVal').textContent = d.fan_pct + '%';
    currentFanPct = d.fan_pct;

    // Przyciski bistabilne — label pokazuje stan BIEŻĄCY, klik wysyła przeciwny.
    currentHeaterOn = d.heater_on;
    currentFanOn = d.fan_pct > 0;
    document.getElementById('heaterToggle').textContent = 'Heater: ' + (currentHeaterOn ? 'ON' : 'OFF');
    document.getElementById('fanToggle').textContent = 'Fan: ' + (currentFanOn ? 'ON' : 'OFF');
    document.getElementById('heater').classList.toggle('idle', currentHeaterOn);
    document.getElementById('fan').classList.toggle('idle', currentFanOn);

    // Przycisk pokazuje stan BIEŻĄCY (nie cel kliknięcia) — Service Mode
    // dostaje ostrzegawczy pomarańcz (wzorzec .btn-service z dolewki).
    // Countdown do auto-enable — sekundy liczone przez backend (getSystemAutoEnableRemainingS()),
    // więc niezależne od tego, ile klientów GUI ma otwartą stronę; odświeża się w rytmie
    // pollingu refresh() (5s), bez lokalnego setInterval co sekundę.
    const sysBtn = document.getElementById('btnSystemToggle');
    sysBtn.textContent = d.system_disabled
      ? 'Service Mode' + (d.auto_enable_remaining_s > 0 ? ' (' + d.auto_enable_remaining_s + 's)' : '')
      : 'Auto Mode';
    sysBtn.classList.toggle('btn-service', d.system_disabled);

    // Ręczne sterowanie (heater/fan) — aktywne TYLKO w Service Mode, z tym
    // samym pomarańczowym oznaczeniem tła co przycisk System (user, 2026-07-15).
    serviceModeActive = d.system_disabled;
    document.querySelectorAll('.manual-ctrl').forEach(el => {
      if (el.tagName === 'BUTTON') el.disabled = !serviceModeActive;
      el.classList.toggle('btn-service', serviceModeActive);
    });
    document.getElementById('fanStepper').classList.toggle('active', serviceModeActive);
    setStepperEnabled(document.getElementById('fanStepper'), serviceModeActive);

    const flags = [];
    if (d.alarm_flags & 0x01) flags.push('TEMP_LOW');
    if (d.alarm_flags & 0x02) flags.push('TEMP_HIGH');
    if (d.alarm_flags & 0x04) flags.push('TREND');
    if (d.alarm_flags & 0x08) flags.push('SENSOR');
    document.getElementById('alarms').textContent = flags.length ? flags.join(', ') : 'none';
    document.getElementById('alarms').classList.toggle('alarm', flags.length > 0);
    document.getElementById('sensorFaultBanner').style.display = d.sensor_fault_latched ? 'block' : 'none';

    const hasError = (d.alarm_flags !== 0) || d.sensor_fault_latched;

    // Kolor #temp wg stanu — priorytet: alarm (LOW dostaje wyraźny ciemny
    // niebieski zamiast czerwieni, żeby odróżnić od HIGH/TREND/SENSOR mimo
    // że tło karty i tak jest czerwone dla każdego z nich) > heating > buffer > cooling.
    // fan_pct > 0 pokrywa dokładnie zakres fan_on_point..alarm_delta_high —
    // in_buffer_zone jest false w tym zakresie (patrz web_handlers.cpp), więc
    // bez tej gałęzi temperatura wyglądała na "białą"/neutralną mimo aktywnego fana.
    const tempEl = document.getElementById('temp');
    tempEl.classList.remove('temp-heating', 'temp-buffer', 'temp-cooling', 'temp-alarm', 'temp-alarm-low');
    if (hasError) {
      tempEl.classList.add((d.alarm_flags & 0x01) ? 'temp-alarm-low' : 'temp-alarm');
    } else if (d.heater_on) {
      tempEl.classList.add('temp-heating');
    } else if (d.in_buffer_zone) {
      tempEl.classList.add('temp-buffer');
    } else if (d.fan_pct > 0) {
      tempEl.classList.add('temp-cooling');
    }

    const statusMain = document.getElementById('statusMain');
    statusMain.className = 'status-main';
    statusMain.classList.add(hasError ? 'status-error' : d.system_disabled ? 'status-disabled' : 'status-ok');

    document.getElementById('wifiItem').className = 'status-main-wifi ' + (d.wifi_connected ? 'wifi-on' : 'wifi-off');

    const e = await fetch('api/energy');
    const en = await e.json();
    const kwhStr = en.total_kwh.toFixed(2);
    const totalEl = document.getElementById('energy_total');
    // "Heartbeat": pulsujący flash TYLKO gdy zaokrąglona wartość faktycznie
    // przeskoczyła o kolejne 0.01 kWh (nie na każdym pollu co 5s) — daje
    // wizualne potwierdzenie zliczenia impulsów, jak mrugająca dioda licznika.
    if (lastEnergyKwhStr !== null && kwhStr !== lastEnergyKwhStr) {
      const valueEl = totalEl.closest('.energy-total-value');
      valueEl.classList.remove('energy-tick');
      void valueEl.offsetWidth; // restart animacji, gdyby poprzednia jeszcze trwała
      valueEl.classList.add('energy-tick');
    }
    lastEnergyKwhStr = kwhStr;
    totalEl.textContent = kwhStr;
    document.getElementById('energyResetTs').textContent = formatResetTs(en.reset_ts);
  } catch(err) { console.error(err); }
}

// Kasowanie licznika: godzina/dzień/miesiąc/rok, świadomie bez minut/sekund.
function formatResetTs(ts) {
  if (!ts) return 'never';
  const d = new Date(ts * 1000);
  const hh = String(d.getHours()).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  return `${hh}h · ${dd}.${mm}.${d.getFullYear()}`;
}

async function loadThermoConfig() {
  const r = await fetch('api/thermo-config');
  const c = await r.json();
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','thermal_buffer','fan_min_pct',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','trend_window_min',
                    'temp_offset','heater_watt','pulses_per_kwh']) {
    if (c[k] !== undefined) document.getElementById(k).value = c[k];
  }
}

function toggleAlgSettings() {
  const panel = document.getElementById('algSettingsPanel');
  panel.style.display = panel.style.display === 'none' ? '' : 'none';
}

// ── Wykresy temperatury: hourly + daily w jednej karcie (portowane z
// chart_prototype/daily_alarms.html, 2026-07-17). Ręczne odświeżanie
// przyciskiem "Refresh Charts" — bez pollingu, tak jak wcześniej tabele
// hourly/daily/alarm-events, które ładowały się raz przy starcie strony.
const ALARM_COLOR_VARS = { 0:'--accent-blue', 1:'--accent-red', 2:'--accent-yellow', 3:'--accent-red' };
// Priorytet widoczności, gdy w jednym buckecie jest kilka typów alarmów naraz —
// rysowany jest tylko punkt typu o najwyższym priorytecie: LOW=HIGH < TREND < SENSOR.
const ALARM_PRIORITY = { 0:1, 1:1, 2:2, 3:3 };
const CHART_PLOT_HEIGHT = 170, CHART_LABEL_ROW_H = 48, CHART_PLOT_PAD_Y = 18;
const CHART_ALARM_R = 5, CHART_ALARM_R_SENSOR = 7;

function cssVar(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

// Ticki osi Y zaokrąglone w górę do pełnych stopni (1/2/5/10...), żeby
// etykiety były całkowite i równo rozłożone zamiast np. 28.6°C / 26.8°C.
function computeChartTicks(minRaw, maxRaw) {
  const pad = Math.max(0.5, (maxRaw - minRaw) * 0.15);
  let min = Math.floor(minRaw - pad);
  let max = Math.ceil(maxRaw + pad);
  if (max <= min) max = min + 1;
  const TARGET_TICK_COUNT = 5;
  const niceSteps = [1, 2, 5, 10, 20, 25, 50, 100];
  const rawStep = (max - min) / (TARGET_TICK_COUNT - 1);
  const step = niceSteps.find(s => s >= rawStep) || Math.ceil(rawStep / 50) * 50;
  max = min + Math.ceil((max - min) / step) * step;
  const ticks = [];
  for (let v = min; v <= max; v += step) ticks.push(v);
  return { min, max, ticks };
}

// Bucket = floor do pełnej godziny ('hour') albo lokalnej północy ('day').
function bucketKey(ts, granularity) {
  const d = new Date(ts * 1000);
  if (granularity === 'hour') d.setMinutes(0, 0, 0); else d.setHours(0, 0, 0, 0);
  return Math.floor(d.getTime() / 1000);
}
function bucketIndexFromTs(records, ts, granularity) {
  const key = bucketKey(ts, granularity);
  return records.findIndex(r => bucketKey(r.ts, granularity) === key);
}

function wireChartNavButtons(scrollArea, prevBtn, nextBtn) {
  const step = () => scrollArea.clientWidth * 0.9;
  prevBtn.onclick = () => scrollArea.scrollBy({ left: -step(), behavior: 'smooth' });
  nextBtn.onclick = () => scrollArea.scrollBy({ left: step(), behavior: 'smooth' });
  const updateBtns = () => {
    prevBtn.disabled = scrollArea.scrollLeft <= 0;
    nextBtn.disabled = scrollArea.scrollLeft >= scrollArea.scrollWidth - scrollArea.clientWidth - 1;
  };
  scrollArea.addEventListener('scroll', updateBtns);
  window.addEventListener('resize', updateBtns);
  updateBtns();
}

// records: oldest-first [{ts, temp_c}]. alarms: [{ts, temp_c, type}] (cała
// zwrócona historia — filtrowana tu do bucketów obecnych w records).
function renderChart(records, alarms, targetTemp, granularity, cellWidth, els) {
  const n = records.length;
  els.yAxis.innerHTML = ''; els.svg.innerHTML = ''; els.labels.innerHTML = '';
  if (n === 0) return;
  const totalWidth = n * cellWidth;
  const svgNS = 'http://www.w3.org/2000/svg';

  const temps = records.map(r => r.temp_c);
  const { min: minT, max: maxT, ticks } = computeChartTicks(Math.min(...temps), Math.max(...temps));
  const yFor = (t) => CHART_PLOT_PAD_Y + (maxT - t) / (maxT - minT) * (CHART_PLOT_HEIGHT - 2 * CHART_PLOT_PAD_Y);

  els.yAxis.style.height = CHART_PLOT_HEIGHT + 'px';
  ticks.forEach(v => {
    const el = document.createElement('div');
    el.className = 'tick';
    el.style.top = yFor(v) + 'px';
    el.textContent = v + '°C';
    els.yAxis.appendChild(el);
  });
  if (targetTemp !== undefined && targetTemp !== null) {
    const el = document.createElement('div');
    el.className = 'tick target-tick';
    el.style.top = yFor(targetTemp) + 'px';
    el.textContent = targetTemp.toFixed(1) + '°C';
    els.yAxis.appendChild(el);
  }

  els.inner.style.width = totalWidth + 'px';

  const byBucket = {};
  alarms.forEach(a => {
    const idx = bucketIndexFromTs(records, a.ts, granularity);
    if (idx < 0) return;
    (byBucket[idx] = byBucket[idx] || []).push(a);
  });

  els.svg.setAttribute('width', totalWidth);
  els.svg.setAttribute('height', CHART_PLOT_HEIGHT);

  ticks.forEach(v => {
    const line = document.createElementNS(svgNS, 'line');
    line.setAttribute('x1', 0); line.setAttribute('x2', totalWidth);
    line.setAttribute('y1', yFor(v)); line.setAttribute('y2', yFor(v));
    line.setAttribute('stroke', cssVar('--border'));
    line.setAttribute('stroke-width', '1');
    els.svg.appendChild(line);
  });

  if (targetTemp !== undefined && targetTemp !== null) {
    const targetY = yFor(targetTemp);
    const targetLine = document.createElementNS(svgNS, 'line');
    targetLine.setAttribute('x1', 0); targetLine.setAttribute('x2', totalWidth);
    targetLine.setAttribute('y1', targetY); targetLine.setAttribute('y2', targetY);
    targetLine.setAttribute('class', 'target-marker');
    els.svg.appendChild(targetLine);
  }

  const nowLine = document.createElementNS(svgNS, 'line');
  const nowX = (n - 1) * cellWidth + cellWidth / 2;
  nowLine.setAttribute('x1', nowX); nowLine.setAttribute('x2', nowX);
  nowLine.setAttribute('y1', 0); nowLine.setAttribute('y2', CHART_PLOT_HEIGHT);
  nowLine.setAttribute('class', 'now-marker');
  els.svg.appendChild(nowLine);

  const points = records.map((r, i) => [i * cellWidth + cellWidth / 2, yFor(r.temp_c)]);
  const poly = document.createElementNS(svgNS, 'polyline');
  poly.setAttribute('points', points.map(p => p.join(',')).join(' '));
  poly.setAttribute('fill', 'none');
  poly.setAttribute('stroke', cssVar('--accent-cyan'));
  poly.setAttribute('stroke-width', '1.5');
  els.svg.appendChild(poly);

  records.forEach((r, i) => {
    const [x, y] = points[i];
    const bucketEvents = byBucket[i];
    if (bucketEvents) {
      let topType = bucketEvents[0].type;
      bucketEvents.forEach(a => { if ((ALARM_PRIORITY[a.type] ?? 1) > (ALARM_PRIORITY[topType] ?? 1)) topType = a.type; });
      const marker = document.createElementNS(svgNS, 'circle');
      marker.setAttribute('cx', x); marker.setAttribute('cy', y);
      marker.setAttribute('r', topType === 3 ? CHART_ALARM_R_SENSOR : CHART_ALARM_R);
      marker.setAttribute('fill', cssVar(ALARM_COLOR_VARS[topType]) || '#999');
      const title = document.createElementNS(svgNS, 'title');
      title.textContent = bucketEvents.map(a =>
        `${ALARM_EVENT_NAMES[a.type] ?? a.type} @ ${new Date(a.ts * 1000).toLocaleString()} (${a.temp_c.toFixed(1)}°C)`
      ).join('\n');
      marker.appendChild(title);
      els.svg.appendChild(marker);
    } else {
      const c = document.createElementNS(svgNS, 'circle');
      c.setAttribute('cx', x); c.setAttribute('cy', y); c.setAttribute('r', 2.2);
      c.setAttribute('fill', cssVar('--accent-cyan'));
      const title = document.createElementNS(svgNS, 'title');
      title.textContent = `${new Date(r.ts * 1000).toLocaleString()} — ${r.temp_c.toFixed(1)}°C`;
      c.appendChild(title);
      els.svg.appendChild(c);
    }
  });

  els.labels.style.height = CHART_LABEL_ROW_H + 'px';
  records.forEach((r, i) => {
    const d = new Date(r.ts * 1000);
    const cell = document.createElement('div');
    cell.className = 'chart-label' + (i === n - 1 ? ' now' : '');
    cell.style.width = cellWidth + 'px';
    const span = document.createElement('span');
    span.textContent = granularity === 'hour'
      ? String(d.getHours()).padStart(2, '0') + ':00'
      : String(d.getDate()).padStart(2, '0') + '/' + String(d.getMonth() + 1).padStart(2, '0');
    cell.appendChild(span);
    els.labels.appendChild(cell);
  });

  els.scroll.scrollLeft = els.scroll.scrollWidth;
  wireChartNavButtons(els.scroll, els.prevBtn, els.nextBtn);
}

const hourlyChartEls = {
  yAxis: document.getElementById('yAxisHourly'), scroll: document.getElementById('scrollHourly'),
  inner: document.getElementById('innerHourly'), svg: document.getElementById('svgHourly'),
  labels: document.getElementById('labelsHourly'),
  prevBtn: document.getElementById('prevBtnHourly'), nextBtn: document.getElementById('nextBtnHourly'),
};
const dailyChartEls = {
  yAxis: document.getElementById('yAxisDaily'), scroll: document.getElementById('scrollDaily'),
  inner: document.getElementById('innerDaily'), svg: document.getElementById('svgDaily'),
  labels: document.getElementById('labelsDaily'),
  prevBtn: document.getElementById('prevBtnDaily'), nextBtn: document.getElementById('nextBtnDaily'),
};

async function loadCharts() {
  try {
    const [hr, dr, ar, cfg] = await Promise.all([
      fetch('api/history-hourly').then(r => r.json()),
      fetch('api/history-daily').then(r => r.json()),
      fetch('api/alarm-events').then(r => r.json()),
      fetch('api/thermo-config').then(r => r.json()),
    ]);
    const hourlyRecords = hr.records.slice().reverse();  // API: newest-first -> chart: oldest-first
    const dailyRecords  = dr.records.slice().reverse();
    renderChart(hourlyRecords, ar.events, cfg.target_temp, 'hour', 34, hourlyChartEls);
    renderChart(dailyRecords,  ar.events, cfg.target_temp, 'day',  30, dailyChartEls);
  } catch (err) { console.error(err); }
}

document.getElementById('heaterToggle').addEventListener('click', async () => {
  await fetch('api/test/heater', { method: 'POST', body: new URLSearchParams({on: currentHeaterOn ? '0' : '1'}) });
  refresh();
});
document.getElementById('fanToggle').addEventListener('click', async () => {
  const pct = currentFanOn ? 0 : FAN_MANUAL_DEFAULT_PCT;
  await fetch('api/test/fan', { method: 'POST', body: new URLSearchParams({pct: String(pct)}) });
  refresh();
});
function stepFan(dir) {
  if (!serviceModeActive) return;
  let v = currentFanPct + dir * FAN_STEP_PCT;
  v = Math.max(0, Math.min(100, v));
  fetch('api/test/fan', { method: 'POST', body: new URLSearchParams({pct: String(v)}) }).then(refresh);
}

document.getElementById('btnSystemToggle').addEventListener('click', async () => {
  await fetch('api/system-toggle', { method: 'POST' });
  refresh();
});
document.getElementById('btnAck').addEventListener('click', async () => {
  await fetch('api/mute-alarm', { method: 'POST' });
  refresh();
});
document.getElementById('btnResetSensor').addEventListener('click', () => {
  showConfirm('Reset sensor fault?', 'Sensor fault is a critical alarm — confirm only if the fault has actually been fixed.', 'warn', async () => {
    await fetch('api/reset-sensor-fault', { method: 'POST' });
    refresh();
  });
});
document.getElementById('btnEnergyReset').addEventListener('click', () => {
  showConfirm('Reset total energy counter?', 'This permanently zeroes the total kWh counter. This cannot be undone.', 'warn', async () => {
    await fetch('api/energy-reset-total', { method: 'POST' });
    refresh();
  });
});
document.getElementById('btnLogout').addEventListener('click', async () => {
  await fetch('api/logout', { method: 'POST' });
  window.location.href = 'login';
});

document.getElementById('btnSaveThermo').addEventListener('click', async () => {
  const body = {};
  for (const k of ['target_temp','heat_hyst','cool_start','cool_full','thermal_buffer','fan_min_pct',
                    'alarm_delta_low','alarm_delta_high','trend_alarm_delta','trend_window_min',
                    'temp_offset','heater_watt','pulses_per_kwh']) {
    body[k] = document.getElementById(k).value;
  }
  const r = await fetch('api/thermo-config', { method: 'POST', body: new URLSearchParams(body) });
  const j = await r.json();
  if (j.ok) {
    showAlert('Saved', 'Algorithm settings have been saved.', 'ok');
  } else {
    showAlert('Error', j.error || 'Save failed', 'err');
  }
});

document.getElementById('btnLoadCharts').addEventListener('click', loadCharts);

updateLockUI();
refresh();
loadThermoConfig();
loadCharts();
setInterval(refresh, 5000);
</script>
</div>
</body>
</html>
)rawliteral";

const char* getDashboardHtml() {
    return DASHBOARD_HTML;
}

static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Thermo Control — Login</title>
<style>
  :root {
    --bg-primary:#0a0f1a; --bg-card:#111827; --border:#2d3a4f;
    --text-primary:#f1f5f9; --accent-blue:#38bdf8; --accent-cyan:#22d3d5; --accent-red:#ef4444;
    --radius:12px;
  }
  body { font-family: sans-serif; background:var(--bg-primary); color:var(--text-primary); margin:0; padding:16px;
         display:flex; align-items:center; justify-content:center; min-height:100vh; }
  .card { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius); padding:24px; width:280px; }
  h1 { color:var(--accent-blue); font-size:1.3em; margin-top:0; }
  input { width:100%; padding:8px; margin:8px 0; border-radius:8px; border:1px solid var(--border); background:#1e293b; color:var(--text-primary); box-sizing:border-box; }
  button { width:100%; padding:8px; border-radius:8px; border:none; background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); color:var(--bg-primary); font-weight:700; cursor:pointer; }
  #err { color:var(--accent-red); font-size:.9em; min-height:1.2em; }
</style>
</head>
<body>
<div class="card">
  <h1>Thermo Control</h1>
  <form id="f">
    <input type="password" id="password" placeholder="Hasło" autofocus>
    <button type="submit">Zaloguj</button>
    <div id="err"></div>
  </form>
</div>
<script>
document.getElementById('f').addEventListener('submit', async (e) => {
  e.preventDefault();
  const password = document.getElementById('password').value;
  const body = new URLSearchParams({password});
  const r = await fetch('api/login', { method: 'POST', body });
  if (r.ok) { window.location.href = './'; }
  else { document.getElementById('err').textContent = 'Błędne hasło'; }
});
</script>
</body>
</html>
)rawliteral";

const char* getLoginHtml() {
    return LOGIN_HTML;
}
