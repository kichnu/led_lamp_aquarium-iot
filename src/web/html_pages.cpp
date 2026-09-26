#include "html_pages.h"

// GUI lampy RL90. CSS: pełny wzorzec z termostatu (thermo_control-iot) + dodatki
// lampy na końcu bloku <style>. Kolor przewodni do ustalenia — zmienne w :root.
// Strony serwowane z flasha bez kopii do heapu (web_handlers.cpp, sendPage()).
// Ścieżki API względne ('api/...') — działa też za nginx pod /device/lampN/.

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RL90 Lamp</title>
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
  .modal-input { display:block; width:100%; height:40px; padding:0 12px; margin-top:4px; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); font-size:var(--font-md); }
  .modal-input:focus { outline:none; border-color:var(--accent-cyan); }
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
  /* ================= RL90 Lamp — dodatki do wzorca termostatu ================= */
  :root { --ch-a:#ffff66; --ch-b:#cc33ff; --ch-c:#0066ff; --ch-d:#00ffcc; }
  .power-label { font-size:var(--font-sm); color:var(--text-muted); text-transform:uppercase; letter-spacing:.05em; align-self:center; }
  /* .status-main podzielony w pionie: słupki kanałów po lewej, moc/tryb po prawej */
  .ch-bars { display:grid; grid-template-columns:repeat(4,52px); gap:6px; flex-shrink:0; padding-right:16px; border-right:1px solid var(--border); }
  .ch-bar { display:flex; flex-direction:column; align-items:center; gap:4px; font-size:var(--font-xs); }
  .ch-bar b { font-family:'Courier New',monospace; font-size:var(--font-sm); }
  .ch-slot { position:relative; width:28px; height:150px; }
  .ch-track { display:flex; flex-direction:column; justify-content:flex-end; width:100%; height:100%; background:var(--bg-primary); border:1px solid var(--border); border-radius:6px; overflow:hidden; }
  /* Test Light: słupek = suwak. Kreska = wartość zadana, wypełnienie = faktyczne wyjście
     (po wejściu w tryb dojeżdża rampą do 50 %). Kreska poza .ch-track, bo overflow:hidden. */
  .ch-knob { display:none; position:absolute; left:-7px; right:-7px; bottom:0; height:6px; margin-bottom:-3px; border-radius:3px; background:var(--text-primary); box-shadow:0 0 0 1px var(--bg-primary), 0 1px 4px rgba(0,0,0,.6); pointer-events:none; }
  .ch-bars.edit .ch-knob { display:block; }
  .ch-bars.edit .ch-slot { cursor:ns-resize; touch-action:none; user-select:none; }
  .ch-bars.dragging .ch-fill { transition:none; }
  /* Liniowo i tyle, ile trwa odstęp odpytywania (--poll, ustawiane w JS) — słupek jedzie płynnie
     między odczytami zamiast skoku + postoju */
  .ch-fill { width:100%; height:0; transition:height var(--poll,2s) linear; }
  .ch-val { font-family:'Courier New',monospace; color:var(--text-secondary); white-space:nowrap; }
  @media (max-width:600px) {
    .status-main { flex-direction:column; align-items:stretch; }
    .ch-bars { grid-template-columns:repeat(4,1fr); justify-items:center; padding:0 0 12px; border-right:none; border-bottom:1px solid var(--border); }
  }
  /* Auto/Service (wzorzec z dolewki) + Night/Test Light aktywne tylko w Service Mode */
  .mode-row { margin-top:12px; }
  .btn-auto { background:rgba(34,197,94,0.15); border-color:rgba(34,197,94,0.3); color:var(--accent-green); }
  .btn-auto:hover { border-color:var(--accent-green); }
  .mode-row > button.on { border-color:var(--service-border); background:var(--service-bg); color:var(--service-text); }
  .diag { margin-top:12px; font-size:var(--font-xs); color:var(--text-muted); line-height:1.6; }
  .diag b { color:var(--text-secondary); font-weight:600; }

  /* Pod wykresem jedna strefa: lista programów (podgląd) albo klawiatura edytora. Oba panele w tej
     samej komórce grida (visibility zamiast display) — wysokość strefy = klawiatura z przyciskami,
     więc przełączanie nie przesuwa strony. contain:size: lista nie rozpycha strefy, tylko przewija się. */
  .prog-zone { display:grid; margin-top:8px; }
  .prog-zone > div { grid-area:1/1; min-width:0; }
  .prog-zone > .off { visibility:hidden; pointer-events:none; }
  #progPanel { display:flex; flex-direction:column; contain:size; }
  .prog-list { flex:1; min-height:0; display:flex; flex-direction:column; gap:6px; overflow-y:auto; scrollbar-width:thin; scrollbar-color:var(--border) transparent; }
  .prog-list.scroll { padding-right:6px; }
  .prog-row { display:flex; align-items:center; gap:8px; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); padding:6px 6px 6px 12px; cursor:pointer; flex-shrink:0; }
  .prog-row.active { border-color:rgba(34,197,94,0.45); background:rgba(34,197,94,0.06); }
  /* Oglądany na wykresie — obrys niezależny od „active” (zielone tło zostaje) */
  .prog-row.selected { outline:2px solid var(--accent-blue); outline-offset:-2px; }
  .prog-name { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; font-weight:600; }
  .prog-row button { margin:0; padding:6px 10px; font-size:var(--font-xs); }
  .prog-row button, .prog-row .badge { border-radius:var(--radius-sm); }
  .prog-ctrl { display:flex; align-items:center; gap:8px; flex-shrink:0; }
  /* Mobile: nazwa w pierwszej linii, pod nią badge i przyciski — każdy 1/4 szerokości (max 4 w wierszu),
     wyśrodkowane; fabryczny ma 3 elementy tej samej szerokości, więc zajmuje mniej miejsca */
  @media (max-width:600px) {
    .prog-row { flex-wrap:wrap; row-gap:8px; padding:8px; }
    .prog-name { flex:1 0 100%; padding-left:4px; }
    .prog-ctrl { flex:1 0 100%; justify-content:center; }
    .prog-ctrl > * { flex:0 0 calc((100% - 24px) / 4); min-width:0; text-align:center; padding-left:0; padding-right:0; }
    .prog-ctrl > .badge { padding-top:6px; padding-bottom:6px; }
  }
  .prog-meta { font-size:var(--font-xs); color:var(--text-muted); margin-top:8px; }

  /* Edytor — port docs/curve_editor_linear.html (krzyż 3×3, karetka, wykres) */
  #edCanvas { display:block; width:100%; height:240px; touch-action:manipulation; background:var(--bg-primary); border:1px solid var(--border); border-radius:var(--radius-sm); }
  #edTrack { position:relative; height:30px; margin-top:8px; background:var(--bg-primary); border:1px solid var(--border); border-radius:var(--radius-sm); overflow:hidden; }
  #edCarriage { position:absolute; top:50%; transform:translateY(-50%); width:20px; height:78%; background:#1e1e10; border:3px solid #ccc; border-radius:3px; cursor:grab; user-select:none; touch-action:none; }
  #edCarriage.dragging { cursor:grabbing; }
  .ed-pad { display:grid; gap:8px; margin-top:8px; grid-template-columns:1fr 1fr 1fr; grid-template-rows:repeat(3,58px); }
  .ed-pad > div, .ed-pad > button { margin:0; display:flex; align-items:center; justify-content:center; background:var(--bg-input); border:1px solid var(--border); border-radius:var(--radius-sm); color:var(--text-primary); font-size:22px; cursor:pointer; user-select:none; touch-action:manipulation; -webkit-tap-highlight-color:transparent; }
  .ed-pad > div:active { border-color:var(--accent-cyan); }
  .ed-pad .btn-ch { font-family:'Courier New',monospace; font-weight:700; font-size:18px; padding:0; }
  .ed-pad .btn-ch.active { color:var(--c); border-color:var(--c); }
  #edDel.mode-del { border-color:var(--accent-red); color:var(--accent-red); background:rgba(239,68,68,0.10); }
  #edDel.mode-add { border-color:var(--accent-green); color:var(--accent-green); background:rgba(34,197,94,0.10); }
  .ed-tools { display:flex; gap:8px; align-items:center; margin-top:10px; flex-wrap:wrap; }
  .ed-tools button { margin:0; }
  #edFine.on { border-color:var(--accent-yellow); color:var(--accent-yellow); }

</style>
</head>
<body>
<div class="container">

<div class="topbar">
  <div class="logo">
    <div class="logo-icon">
      <svg viewBox="0 0 24 24"><path d="M9 21c0 .55.45 1 1 1h4c.55 0 1-.45 1-1v-1H9v1zm3-19C8.14 2 5 5.14 5 9c0 2.38 1.19 4.47 3 5.74V17c0 .55.45 1 1 1h6c.55 0 1-.45 1-1v-2.26c1.81-1.27 3-3.36 3-5.74 0-3.86-3.14-7-7-7z"/></svg>
    </div>
    <h1>RL90 Lamp</h1>
  </div>
  <div class="topbar-actions">
    <button class="btn-back" id="btnLogout">Back</button>
  </div>
</div>

<!-- SYSTEM STATUS -->
<div class="card">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(56,189,248,0.15);">
      <svg fill="currentColor" style="color:var(--accent-blue);" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
    </div>
    <h2>System Status</h2>
    <div class="status-main-wifi wifi-off" id="wifiItem">
      <span class="wifi-label">WiFi</span>
      <span class="wifi-dot">●</span>
    </div>
  </div>
  <div class="status-main status-ok" id="statusMain">
    <div class="ch-bars" id="chBars"></div>
    <div class="status-main-body">
      <div class="temp"><span id="power">--%</span><span class="power-label">LED power</span></div>
      <div class="status-main-sub">
        <span class="badge idle" id="mode">PROGRAM</span>
        <span class="sub-sep">•</span>
        <span id="activeName">—</span>
        <span class="sub-sep">•</span>
        <span>Fan: <span class="badge" id="fan">OFF</span></span>
        <span class="sub-sep">•</span>
        <span id="clock">--:--</span>
      </div>
    </div>
  </div>
  <div class="btn-row mode-row">
    <button id="btnMode" class="btn-auto">Auto Mode</button>
    <button id="btnNight" disabled>Night Light</button>
    <button id="btnTest" disabled>Test Light</button>
  </div>
</div>

<!-- PROGRAMS: wykres zawsze na górze; pod nim lista (podgląd) albo klawiatura (edycja) -->
<div class="card" id="progCard">
  <div class="card-header">
    <div class="card-header-icon" style="background:rgba(34,197,94,0.15);">
      <svg fill="none" stroke="currentColor" stroke-width="2" style="color:var(--accent-green);" viewBox="0 0 24 24"><polyline points="3,18 8,10 13,14 21,5"/></svg>
    </div>
    <h2 id="edTitle">Programs</h2>
  </div>
  <canvas id="edCanvas"></canvas>
  <div id="edTrack"><div id="edCarriage"></div></div>
  <div class="prog-zone">
    <div id="progPanel">
      <div class="prog-list" id="progList"></div>
      <div class="prog-meta" id="progMeta"></div>
    </div>
    <div id="edPanel" class="off">
      <div class="ed-pad">
        <button class="btn-ch" id="edCh0">A</button>
        <div id="edUp">▲</div>
        <button class="btn-ch" id="edCh1">B</button>
        <div id="edLeft">◀</div>
        <div id="edDel">＋</div>
        <div id="edRight">▶</div>
        <button class="btn-ch" id="edCh3">D</button>
        <div id="edDown">▼</div>
        <button class="btn-ch" id="edCh2">C</button>
      </div>
      <div class="ed-tools">
        <button id="edFine" title="Fine step 0.1%">0.1%</button>
        <button id="edView" title="View 00:00–24:00 / 08:00–24:00">08–24</button>
      </div>
      <div class="btn-row" style="margin-top:10px;">
        <button class="primary" id="edSave">Save as new program</button>
        <button id="edClose">Close</button>
      </div>
    </div>
  </div>
</div>

<button id="settingsBtn" class="settings-toggle-btn">⚙ Settings</button>

<div class="card" id="settingsPanel" style="display:none;">
  <div class="settings-title">Lamp</div>
  <div class="settings-grid">
    <label class="settings-field">Ramp time [s]<input type="number" step="1" id="ramp_s"></label>
    <label class="settings-field">Fan ON at power ≥ [%]<input type="number" step="1" min="0" max="100" id="fan_on_pct"></label>
    <label class="settings-field">Fan OFF at power &lt; [%]<input type="number" step="1" min="0" max="100" id="fan_off_pct"></label>
  </div>
  <div class="settings-title" style="margin-top:16px;">Night Light preset [%]</div>
  <div class="settings-grid" id="nightSettings"></div>
  <div class="settings-title" style="margin-top:16px;">Channels</div>
  <div class="settings-grid" id="chSettings"></div>
  <div class="btn-row" style="margin-top:10px;">
    <button class="primary" id="btnSaveSettings">Save</button>
    <button type="button" id="btnCancelSettings">Cancel</button>
  </div>
</div>

<!-- DIAGNOSTICS (tymczasowo na końcu — do przerobienia) -->
<div class="card">
  <div class="diag" id="diag" style="margin-top:0;"></div>
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
// ── Modal boxes (zamiast alert()/confirm()) — wzorzec z termostatu ──────
const MODAL_ICONS = {
  ok:   '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><polyline points="22,4 12,14.01 9,11.01"/></svg>',
  err:  '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>',
  warn: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
  info: '<svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>'
};
let alertCallback = null;
let alertAutoHideTimer = null;
const ALERT_AUTO_HIDE_MS = 1000;

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
// Okienko z polem tekstowym (zmiana nazwy); onOk(wartość) po Confirm/Enter
function showPrompt(title, value, maxLen, onOk) {
  clearTimeout(alertAutoHideTimer);
  document.getElementById('alertIcon').className = 'modal-icon info';
  document.getElementById('alertIcon').innerHTML = MODAL_ICONS.info;
  document.getElementById('alertTitle').textContent = title;
  const txt = document.getElementById('alertText');
  txt.textContent = '';
  const inp = document.createElement('input');
  inp.type = 'text'; inp.className = 'modal-input'; inp.maxLength = maxLen; inp.value = value;
  inp.addEventListener('keydown', e => { if (e.key === 'Enter') closeAlert(true); });
  txt.appendChild(inp);
  document.getElementById('alertActions').innerHTML =
    '<button onclick="closeAlert()">Cancel</button><button class="primary" onclick="closeAlert(true)">Save</button>';
  alertCallback = () => onOk(inp.value.trim());
  document.getElementById('alertModal').classList.add('show');
  inp.focus(); inp.select();
}
function closeAlert(confirmed) {
  clearTimeout(alertAutoHideTimer);
  document.getElementById('alertModal').classList.remove('show');
  if (confirmed && alertCallback) alertCallback();
  alertCallback = null;
}
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeAlert(); });
document.addEventListener('click', (e) => { if (e.target.id === 'alertModal') closeAlert(); });

// ── API ──────────────────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const CH = ['A', 'B', 'C', 'D'];
const CH_COLORS = ['#ffff66', '#cc33ff', '#0066ff', '#00ffcc'];

async function apiGet(url) {
  const r = await fetch(url);
  if (r.status === 401) { window.location.href = 'login'; throw new Error('unauthorized'); }
  return r.json();
}
async function apiPost(url, params) {
  const r = await fetch(url, { method: 'POST', body: new URLSearchParams(params || {}) });
  if (r.status === 401) { window.location.href = 'login'; throw new Error('unauthorized'); }
  let j = {};
  try { j = await r.json(); } catch (e) {}
  if (!r.ok || j.success === false) throw new Error(j.error || ('HTTP ' + r.status));
  return j;
}
const pct = v100 => (v100 / 100).toFixed(v100 < 1000 ? 2 : 1);
const hhmm = m => String(Math.floor(m / 60) % 24).padStart(2, '0') + ':' + String(Math.floor(m % 60)).padStart(2, '0');
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm';
}

// ── Status ───────────────────────────────────────────────────────────────
let lastStatus = null;
let manualMode = 'program';
// Service Mode to stan GUI: odblokowuje Night/Test Light, lampa dalej jedzie programem,
// dopóki nie wybierze się jednego z nich (manual-enter); powrót do Auto = manual-exit.
let serviceMode = false;

(function buildBars() {
  $('chBars').innerHTML = CH.map((c, i) =>
    '<div class="ch-bar">' +
    '<div class="ch-slot" id="slot' + i + '"><div class="ch-track"><div class="ch-fill" id="bar' + i + '" style="background:' + CH_COLORS[i] + '"></div></div>' +
    '<div class="ch-knob" id="knob' + i + '"></div></div>' +
    '<b style="color:' + CH_COLORS[i] + '">' + c + '</b>' +
    '<span class="ch-val" id="val' + i + '">--</span></div>').join('');
})();

async function refresh() {
  let s;
  try { s = await apiGet('api/status'); } catch (e) { return; }
  lastStatus = s;
  $('power').textContent = s.power_pct.toFixed(1) + '%';
  const m = $('mode');
  m.textContent = s.mode.toUpperCase() + (s.ramp ? ' ↗' : '');
  m.className = 'badge ' + (s.mode === 'program' ? 'idle' : 'heating');
  $('activeName').textContent = s.active_name;
  const f = $('fan');
  f.textContent = s.fan_on ? s.fan_pct + '%' : 'OFF';
  f.className = 'badge ' + (s.fan_on ? 'cooling' : '');
  $('clock').textContent = s.time_valid ? s.time.substring(11, 16) : 'no time';
  if (s.mode !== manualMode) setManualUI(s.mode, s.manual);
  // W Test Light lokalne wartości zadane są ważniejsze od odczytu, dopóki trwa przeciąganie
  // albo czeka wysyłka — inaczej odpytywanie cofałoby kreskę pod palcem
  else if (s.mode === 'test' && dragCh < 0 && !manualTimer) { manualValues = s.manual.slice(); drawKnobs(); }
  s.channels.forEach((c, i) => {
    if (i !== dragCh) $('bar' + i).style.height = (c.out / 100) + '%';
    if (s.mode !== 'test') $('val' + i).textContent = pct(c.out) + '%';
  });
  const wi = $('wifiItem');
  wi.classList.toggle('wifi-on', s.wifi); wi.classList.toggle('wifi-off', !s.wifi);
  const sm = $('statusMain');
  const warn = !s.fram_ok || !s.time_valid || s.rtc_battery;
  sm.className = 'status-main ' + (warn ? 'status-warn' : 'status-ok');
  const r = s.resets;
  $('diag').innerHTML =
    'Time: <b>' + s.time + '</b> (' + s.time_src + (s.ntp_age_s !== null ? ', NTP ' + Math.round(s.ntp_age_s / 60) + ' min ago' : '') + ')<br>' +
    'Uptime: <b>' + fmtUptime(s.uptime_s) + '</b> · Heap: <b>' + Math.round(s.heap / 1024) + ' KB</b> (min ' +
    Math.round(s.heap_min / 1024) + ', largest ' + Math.round(s.heap_largest / 1024) + ')<br>' +
    'Last reset: <b>' + s.reset_reason + '</b> · WDT ' + r.wdt + ' · panic ' + r.panic + ' · brownout ' + r.brownout + ' · other ' + r.other + '<br>' +
    'FRAM: <b>' + (s.fram_ok ? 'OK' : 'ERROR') + '</b> · RTC: <b>' + (s.rtc_ok ? 'OK' : 'ERROR') + '</b>' +
    (s.rtc_battery ? ' <span class="sub-danger">(battery?)</span>' : '') +
    ' · RSSI ' + s.rssi + ' dBm · ' + s.ip + ' · FW ' + s.fw;
  if (ed.open) drawCurve();
}

// ── Programy ─────────────────────────────────────────────────────────────
let programs = [];
let activeId = '';

async function loadPrograms() {
  const j = await apiGet('api/programs');
  activeId = j.active_id;
  programs = j.programs.sort((a, b) => a.name.localeCompare(b.name, 'pl', { sensitivity: 'base' }));
  const list = $('progList');
  list.innerHTML = '';
  if (!j.active_in_library) {
    const row = document.createElement('div');
    row.className = 'prog-row active' + (ed.viewId === activeId ? ' selected' : '');
    row.innerHTML = '<span class="prog-name"></span><span class="badge alarm">RAM only</span>';
    row.querySelector('.prog-name').textContent = lastStatus ? lastStatus.active_name : 'Factory';
    row.onclick = () => showProgram(activeId);
    list.appendChild(row);
  }
  programs.forEach(p => {
    const row = document.createElement('div');
    row.className = 'prog-row' + (p.active ? ' active' : '') + (p.id === ed.viewId ? ' selected' : '');
    row.dataset.id = p.id;
    // Klik w wiersz (poza przyciskami) = podgląd na wykresie
    row.addEventListener('click', ev => { if (!ev.target.closest('button')) showProgram(p.id); });
    const name = document.createElement('span');
    name.className = 'prog-name';
    name.textContent = p.name;
    row.appendChild(name);
    const ctrl = document.createElement('div');   // badge + przyciski — na mobile osobna linia pod nazwą
    ctrl.className = 'prog-ctrl';
    row.appendChild(ctrl);
    if (p.factory) ctrl.insertAdjacentHTML('beforeend', '<span class="badge cooling">factory</span>');
    if (p.active) {
      ctrl.insertAdjacentHTML('beforeend', '<span class="badge idle">active</span>');
    } else {
      const b = document.createElement('button');
      b.className = 'primary'; b.textContent = 'Activate';
      b.onclick = () => activate(p);
      ctrl.appendChild(b);
    }
    const e = document.createElement('button');
    e.textContent = 'Edit';
    e.onclick = () => openEditor(p.id);
    ctrl.appendChild(e);
    if (!p.factory) {
      const r = document.createElement('button');
      r.textContent = '✎'; r.title = 'Rename';
      r.onclick = () => showPrompt('Rename program', p.name, 23, n => rename(p, n));
      ctrl.appendChild(r);
    }
    // Fabryczny bez ✕ — skasowany nie wraca (tombstone), a to jedyna wzorcowa krzywa
    if (!p.factory) {
      const d = document.createElement('button');
      d.className = 'danger'; d.textContent = '✕';
      d.disabled = p.active;
      d.title = p.active ? 'Active program cannot be deleted' : 'Delete';
      d.onclick = () => showConfirm('Delete program', '"' + p.name + '" will be removed from the library.', 'warn', () => del(p));
      ctrl.appendChild(d);
    }
    list.appendChild(row);
  });
  $('progMeta').textContent = programs.length + ' / ' + j.capacity + ' programs · ' + j.created_total + ' created';
  list.classList.toggle('scroll', list.scrollHeight > list.clientHeight);
  // Oglądany zniknął z listy (skasowany) albo jeszcze nic nie wczytano — pokaż aktywny
  if (!ed.viewId || (ed.viewId !== activeId && !programs.some(p => p.id === ed.viewId))) showProgram(activeId);
}

async function activate(p) {
  try {
    await apiPost('api/program-activate', { id: p.id });
    showAlert('Activated', p.name, 'ok');
    await loadPrograms(); refresh();
  } catch (e) { showAlert('Error', e.message, 'err'); }
}
async function rename(p, name) {
  if (!name || name === p.name) return;
  try {
    const j = await apiPost('api/program-rename', { id: p.id, name });
    if (ed.viewId === p.id) { ed.viewId = ed.parent = j.id; ed.name = name; updateTitle(); }   // kopia pod nowym id
    showAlert('Renamed', name, 'ok');
    await loadPrograms(); refresh();
  } catch (e) { showAlert('Error', e.message, 'err'); }
}
async function del(p) {
  try {
    await apiPost('api/program-delete', { id: p.id });
    showAlert('Deleted', p.name, 'ok');
    loadPrograms();
  } catch (e) { showAlert('Error', e.message, 'err'); }
}

// ── Edytor (port docs/curve_editor_linear.html) ──────────────────────────
// v w % mocy kanału (0–100, 2 miejsca); w API setne procenta.
const DAY_MIN = 1440, MAX_POINTS = 48, STEP_NORMAL = 2, STEP_FINE = 0.1;
const HOLD_DELAY_MS = 350, HOLD_STEP_MS = 90, SHIFT_MIN = 10, SNAP_TOL = 8, MERGE_MIN = 1;
const VIEW_START_DEFAULT = 480;
// mode: 'view' = podgląd programu z listy (tylko karetka), 'edit' = klawiatura. open = wykres wczytany.
const ed = { open: false, mode: 'view', chans: [[], [], [], []], cur: -1, editCur: 2, carriage: 720, fine: false,
             viewStart: VIEW_START_DEFAULT, viewEnd: DAY_MIN, parent: '', viewId: '', name: '', dirty: false,
             drag: null, loadSeq: 0 };
const round2 = x => Math.round(x * 100) / 100;
const clampV = x => Math.max(0, Math.min(100, x));
const pts = () => ed.cur < 0 ? [] : ed.chans[ed.cur];

function evalPts(p, t) {
  const n = p.length;
  if (!n) return 0;
  if (t <= p[0].t) return p[0].v;
  if (t >= p[n - 1].t) return p[n - 1].v;
  let lo = 0, hi = n - 1;
  while (hi - lo > 1) { const mid = (lo + hi) >> 1; p[mid].t <= t ? lo = mid : hi = mid; }
  const h = p[hi].t - p[lo].t;
  if (h < 1e-9) return p[lo].v;
  return p[lo].v + (p[hi].v - p[lo].v) * (t - p[lo].t) / h;
}
const getV = (t, i = ed.cur) => clampV(evalPts(ed.chans[i], t));

function updateTitle() {
  $('edTitle').textContent = (ed.mode === 'edit' ? 'Editing — ' : 'Programs — ') + ed.name;
}

// Wczytuje program na wykres; szybkie klikanie po liście — liczy się ostatnie żądanie
async function loadChart(id) {
  const seq = ++ed.loadSeq;
  const j = await apiGet('api/program?id=' + id);
  if (seq !== ed.loadSeq) return false;
  ed.chans = CH.map(c => j[c].map(p => ({ t: p[0], v: p[1] / 100 })));
  ed.viewId = ed.parent = j.id;
  ed.name = j.name;
  ed.dirty = false;
  if (!ed.open) { ed.open = true; resizeCanvas(); }
  document.querySelectorAll('#progList .prog-row').forEach(r =>
    r.classList.toggle('selected', (r.dataset.id || activeId) === ed.viewId));
  updateTitle();
  updateAll();
  return true;
}

async function showProgram(id) {
  if (ed.mode !== 'view' || !id) return;
  try { await loadChart(id); } catch (e) { showAlert('Error', e.message, 'err'); }
}

function setMode(mode) {
  if (mode === 'edit') { ed.cur = ed.editCur; }
  else { if (ed.cur >= 0) ed.editCur = ed.cur; ed.cur = -1; }   // podgląd: wszystkie kanały równo
  ed.mode = mode;
  $('progPanel').classList.toggle('off', mode === 'edit');
  $('edPanel').classList.toggle('off', mode !== 'edit');
  updateTitle();
  updateAll();
}

async function openEditor(id) {
  try {
    if (id !== ed.viewId && !(await loadChart(id))) return;
    ed.dirty = false;
    setMode('edit');
  } catch (e) { showAlert('Error', e.message, 'err'); }
}

// Close: niezapisane zmiany → pytanie; porzucenie = ponowne wczytanie programu z lampy
async function closeEditor() {
  const back = async () => {
    setMode('view');
    try { await loadChart(ed.viewId); } catch (e) { showAlert('Error', e.message, 'err'); }
  };
  if (ed.dirty) showConfirm('Discard changes?', 'Unsaved changes to "' + ed.name + '" will be lost.', 'warn', back);
  else setMode('view');
}
$('edClose').onclick = closeEditor;

// Przesunięcie całej krzywej aktywnego kanału o dt minut, doba cykliczna.
// Dawny punkt krańcowy (00:00 = 24:00) przesuwa się jak każdy inny — bez tego zbocze, które
// przechodzi przez północ, traci stopę i wolny koniec wykresu odrywa się od osi.
// Wstawiany tylko, gdy nie leży na prostej między sąsiadami (inaczej punkty mnożyłyby się co krok).
// Bez round2 — zaokrąglanie przy każdym kroku kumulowało błąd; setne % dopiero przy zapisie.
function shiftCurve(dt) {
  if (ed.mode !== 'edit' || ed.cur < 0) return;
  const wrap = t => ((t % DAY_MIN) + DAY_MIN) % DAY_MIN;
  const P = pts();
  const v0 = getV(wrap(-dt));
  const inner = [];
  for (const p of P) {
    if (p.t <= 0 || p.t >= DAY_MIN) continue;
    const t = wrap(p.t + dt);
    if (t !== 0) inner.push({ t, v: p.v });
  }
  const edge = { t: wrap(dt), v: P.length ? P[0].v : 0 };
  if (edge.t !== 0) {
    const without = [{ t: 0, v: v0 }, ...[...inner].sort((a, b) => a.t - b.t), { t: DAY_MIN, v: v0 }];
    if (Math.abs(evalPts(without, edge.t) - edge.v) > 1e-6) inner.push(edge);
  }
  inner.sort((a, b) => a.t - b.t);
  const merged = [];
  for (const p of inner) if (!merged.length || p.t - merged[merged.length - 1].t >= MERGE_MIN) merged.push(p);
  if (merged.length + 2 > MAX_POINTS) { showAlert('Limit', 'Max ' + MAX_POINTS + ' points per channel.', 'warn'); return; }
  ed.chans[ed.cur] = [{ t: 0, v: v0 }, ...merged, { t: DAY_MIN, v: v0 }];
  ed.dirty = true;
  updateAll();
}

function findNearbyIdx(t) {
  const P = pts();
  for (let i = 1; i < P.length - 1; i++) if (Math.abs(P[i].t - t) <= SNAP_TOL) return i;
  return null;
}

function changeValue(delta) {
  if (ed.mode !== 'edit' || ed.cur < 0) return;
  const P = pts();
  const t = Math.round(ed.carriage);
  const idx = findNearbyIdx(t);
  if (idx !== null) {
    P[idx].v = round2(clampV(P[idx].v + delta));
  } else {
    if (P.some(p => p.t === t) || t <= 0 || t >= DAY_MIN || P.length >= MAX_POINTS) return;
    P.push({ t, v: round2(clampV(getV(t) + delta)) });
    P.sort((a, b) => a.t - b.t);
  }
  ed.dirty = true;
  updateAll();
}

// Canvas (CSS px, skalowanie devicePixelRatio)
const canvas = $('edCanvas');
const ctx = canvas.getContext('2d');
const PAD = { l: 8, r: 8, t: 32, b: 24 };
let W = 0, H = 0;
function resizeCanvas() {
  const dpr = window.devicePixelRatio || 1;
  W = canvas.clientWidth; H = canvas.clientHeight;
  canvas.width = Math.round(W * dpr); canvas.height = Math.round(H * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawCurve();
}
window.addEventListener('resize', () => { if (ed.open) { resizeCanvas(); positionCarriage(); } });
const cX = t => PAD.l + ((t - ed.viewStart) / (ed.viewEnd - ed.viewStart)) * (W - PAD.l - PAD.r);
const cY = v => PAD.t + (1 - v / 100) * (H - PAD.t - PAD.b);

function tracePath(i) {
  ctx.beginPath();
  ctx.moveTo(cX(ed.viewStart), cY(getV(ed.viewStart, i)));
  for (let t = ed.viewStart + 2; t <= ed.viewEnd; t += 2) ctx.lineTo(cX(t), cY(getV(t, i)));
}

function drawCurve() {
  if (!ed.open || !W) return;
  ctx.clearRect(0, 0, W, H);
  ctx.lineWidth = 1; ctx.font = '10px monospace';
  const span = ed.viewEnd - ed.viewStart;
  const gridStep = span <= 240 ? 30 : span <= 480 ? 60 : 240;
  for (let t = Math.ceil(ed.viewStart / gridStep) * gridStep; t <= ed.viewEnd; t += gridStep) {
    ctx.strokeStyle = '#1e293b';
    ctx.beginPath(); ctx.moveTo(cX(t), PAD.t); ctx.lineTo(cX(t), H - PAD.b); ctx.stroke();
    ctx.fillStyle = '#94a3b8'; ctx.fillText(hhmm(t), cX(t) - 13, H - 6);
  }
  // linia "teraz"
  if (lastStatus && lastStatus.time_valid && lastStatus.minute >= ed.viewStart) {
    ctx.strokeStyle = '#22d3d5'; ctx.globalAlpha = 0.5; ctx.setLineDash([2, 4]);
    ctx.beginPath(); ctx.moveTo(cX(lastStatus.minute), PAD.t); ctx.lineTo(cX(lastStatus.minute), H - PAD.b); ctx.stroke();
    ctx.setLineDash([]); ctx.globalAlpha = 1;
  }
  CH.forEach((c, i) => {
    if (i === ed.cur) return;
    tracePath(i);
    ctx.strokeStyle = CH_COLORS[i]; ctx.globalAlpha = ed.cur < 0 ? 0.9 : 0.28; ctx.lineWidth = ed.cur < 0 ? 1.5 : 1; ctx.stroke();
    ctx.globalAlpha = 1;
  });
  if (ed.cur >= 0) {
    const col = CH_COLORS[ed.cur];
    tracePath(ed.cur);
    ctx.lineTo(cX(ed.viewEnd), H - PAD.b); ctx.lineTo(cX(ed.viewStart), H - PAD.b); ctx.closePath();
    ctx.fillStyle = col; ctx.globalAlpha = 0.07; ctx.fill(); ctx.globalAlpha = 1;
    tracePath(ed.cur);
    ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
    const nearby = findNearbyIdx(Math.round(ed.carriage));
    pts().forEach((p, i) => {
      if (p.t < ed.viewStart || p.t > ed.viewEnd) return;
      const edge = p.t === 0 || p.t === DAY_MIN, hl = i === nearby;
      ctx.beginPath();
      ctx.arc(cX(p.t), cY(p.v), hl ? 7 : (edge ? 3 : 5), 0, Math.PI * 2);
      ctx.fillStyle = hl ? '#fff' : (edge ? '#2a2a2a' : col);
      ctx.strokeStyle = '#0a0f1a'; ctx.lineWidth = 1.5; ctx.fill(); ctx.stroke();
    });
  }
  // karetka
  const kx = cX(ed.carriage);
  ctx.setLineDash([3, 3]);
  ctx.beginPath(); ctx.moveTo(kx, PAD.t); ctx.lineTo(kx, H - PAD.b);
  ctx.strokeStyle = '#f97316'; ctx.lineWidth = 1; ctx.stroke();
  ctx.setLineDash([]);
  (ed.cur < 0 ? [0, 1, 2, 3] : [ed.cur]).forEach(i => {
    ctx.beginPath(); ctx.arc(kx, cY(getV(ed.carriage, i)), 4, 0, Math.PI * 2);
    ctx.fillStyle = ed.cur < 0 ? CH_COLORS[i] : '#f97316'; ctx.fill();
  });
  // odczyty: v% lewy-góra, czas prawy-góra
  ctx.font = 'bold 18px monospace'; ctx.fillStyle = '#f1f5f9'; ctx.textBaseline = 'top';
  ctx.textAlign = 'left';
  const v = ed.cur < 0 ? null : getV(ed.carriage);
  ctx.fillText(v === null ? '--%' : (ed.fine ? v.toFixed(2) : Math.round(v)) + '%', 10, 8);
  ctx.textAlign = 'right';
  ctx.fillText(hhmm(ed.carriage), W - 10, 8);
  ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';
}
canvas.addEventListener('click', () => { if (ed.cur >= 0) { ed.cur = -1; updateAll(); } });

// Karetka
const track = $('edTrack'), carriage = $('edCarriage');
function positionCarriage() {
  const tw = track.clientWidth, cw = carriage.offsetWidth;
  const frac = (ed.carriage - ed.viewStart) / (ed.viewEnd - ed.viewStart);
  carriage.style.left = Math.max(0, Math.min(tw - cw, frac * tw - cw / 2)) + 'px';
}
function dragStart(x) { ed.drag = { startX: x, startMins: ed.carriage }; carriage.classList.add('dragging'); }
function dragMove(x) {
  if (!ed.drag) return;
  const d = ((x - ed.drag.startX) / track.clientWidth) * (ed.viewEnd - ed.viewStart);
  ed.carriage = Math.max(ed.viewStart, Math.min(ed.viewEnd, ed.drag.startMins + d));
  updateAll();
}
function dragEnd() { ed.drag = null; carriage.classList.remove('dragging'); }
carriage.addEventListener('mousedown', e => { e.preventDefault(); dragStart(e.clientX); });
document.addEventListener('mousemove', e => dragMove(e.clientX));
document.addEventListener('mouseup', dragEnd);
carriage.addEventListener('touchstart', e => { e.preventDefault(); dragStart(e.touches[0].clientX); }, { passive: false });
document.addEventListener('touchmove', e => { if (ed.drag) { e.preventDefault(); dragMove(e.touches[0].clientX); } }, { passive: false });
document.addEventListener('touchend', dragEnd);

// Środkowy klawisz: ✕ gdy karetka stoi na punkcie, ＋ gdy go nie ma
function centerState() {
  if (ed.cur < 0) return 'off';
  const t = Math.round(ed.carriage);
  if (findNearbyIdx(t) !== null) return 'del';
  if (t <= 0 || t >= DAY_MIN || pts().length >= MAX_POINTS) return 'off';
  return 'add';
}
function updatePad() {
  const st = centerState(), b = $('edDel');
  b.classList.toggle('mode-del', st === 'del');
  b.classList.toggle('mode-add', st === 'add');
  b.textContent = st === 'del' ? '✕' : '＋';
  b.style.opacity = st === 'off' ? '0.25' : '1';
  CH.forEach((c, i) => {
    const cb = $('edCh' + i);
    cb.style.setProperty('--c', CH_COLORS[i]);
    cb.classList.toggle('active', i === ed.cur);
  });
  const off = ed.cur < 0 ? '0.25' : '1';
  ['edUp', 'edDown', 'edLeft', 'edRight'].forEach(id => $(id).style.opacity = off);
  $('edFine').classList.toggle('on', ed.fine);
  $('edView').textContent = ed.viewStart === 0 ? '00–24' : '08–24';
}
function updateAll() { positionCarriage(); drawCurve(); updatePad(); }

$('edDel').addEventListener('click', () => {
  const st = centerState();
  if (ed.mode !== 'edit') return;
  if (st === 'del') { pts().splice(findNearbyIdx(Math.round(ed.carriage)), 1); ed.dirty = true; updateAll(); }
  else if (st === 'add') changeValue(0);
});
// Przytrzymanie ▲/▼: krok od razu, potem ciągła zmiana co HOLD_STEP_MS
function bindHold(el, fn) {
  let delayTimer = null, repeatTimer = null;
  const stop = () => { clearTimeout(delayTimer); clearInterval(repeatTimer); delayTimer = repeatTimer = null; };
  const start = e => {
    e.preventDefault();
    fn();
    delayTimer = setTimeout(() => { repeatTimer = setInterval(fn, HOLD_STEP_MS); }, HOLD_DELAY_MS);
  };
  el.addEventListener('mousedown', start);
  el.addEventListener('touchstart', start, { passive: false });
  ['mouseup', 'mouseleave', 'touchend', 'touchcancel'].forEach(ev => el.addEventListener(ev, stop));
}
const step = () => ed.fine ? STEP_FINE : STEP_NORMAL;
bindHold($('edUp'), () => changeValue(+step()));
bindHold($('edDown'), () => changeValue(-step()));
$('edLeft').addEventListener('click', () => shiftCurve(-SHIFT_MIN));
$('edRight').addEventListener('click', () => shiftCurve(+SHIFT_MIN));
CH.forEach((c, i) => $('edCh' + i).addEventListener('click', () => { ed.cur = i; updateAll(); }));
$('edFine').onclick = () => { ed.fine = !ed.fine; updateAll(); };
$('edView').onclick = () => {
  ed.viewStart = ed.viewStart === 0 ? VIEW_START_DEFAULT : 0;
  ed.carriage = Math.max(ed.viewStart, ed.carriage);
  updateAll();
};

$('edSave').onclick = async () => {
  const params = { parent: ed.parent };   // nazwa „Program NNNN” z licznika w FRAM, zmiana przez ✎
  ['ch_a', 'ch_b', 'ch_c', 'ch_d'].forEach((k, i) => {
    params[k] = ed.chans[i].map(p => p.t + ':' + Math.round(p.v * 100)).join(',');
  });
  try {
    const j = await apiPost('api/program-save', params);
    ed.dirty = false;
    setMode('view');
    ed.viewId = j.id;              // nowy program od razu zaznaczony i na wykresie
    await loadPrograms();
    await loadChart(j.id);
    showAlert('Saved', '"' + j.name + '" saved as a new program.', 'ok');
  } catch (e) { showAlert('Save failed', e.message, 'err'); }
};

// ── Tryb ręczny: Test Light = słupki jako suwaki, Night Light = preset z Settings ──
let manualValues = [0, 0, 0, 0];
let manualTimer = null;
let dragCh = -1;

function drawKnobs() {
  CH.forEach((c, i) => {
    $('knob' + i).style.bottom = (manualValues[i] / 100) + '%';
    if (manualMode === 'test') $('val' + i).textContent = pct(manualValues[i]) + '%';
  });
}

function sendManual() {
  manualTimer = null;
  apiPost('api/manual-set', { ch_a: manualValues[0], ch_b: manualValues[1], ch_c: manualValues[2], ch_d: manualValues[3] })
    .catch(e => showAlert('Error', e.message, 'err'));
}

(function bindSlots() {
  const setFromY = (i, y) => {
    const r = $('slot' + i).getBoundingClientRect();
    const v = Math.round(Math.min(1, Math.max(0, (r.bottom - y) / r.height)) * 100) * 100;   // krok 1 %
    if (v === manualValues[i]) return;
    manualValues[i] = v;
    $('bar' + i).style.height = (v / 100) + '%';   // bez rampy w trybie ręcznym — wyjście idzie za kreską
    drawKnobs();
    clearTimeout(manualTimer);
    manualTimer = setTimeout(sendManual, 150);
  };
  CH.forEach((c, i) => {
    const slot = $('slot' + i);
    slot.addEventListener('pointerdown', e => {
      if (manualMode !== 'test') return;
      dragCh = i;
      $('chBars').classList.add('dragging');
      slot.setPointerCapture(e.pointerId);
      setFromY(i, e.clientY);
      e.preventDefault();
    });
    slot.addEventListener('pointermove', e => { if (dragCh === i) setFromY(i, e.clientY); });
    const end = () => { if (dragCh === i) { dragCh = -1; $('chBars').classList.remove('dragging'); } };
    slot.addEventListener('pointerup', end);
    slot.addEventListener('pointercancel', end);
  });
})();

function updateModeUI() {
  const b = $('btnMode');
  b.textContent = serviceMode ? 'Service Mode' : 'Auto Mode';
  b.className = serviceMode ? 'btn-service' : 'btn-auto';
  $('btnNight').disabled = !serviceMode;
  $('btnTest').disabled = !serviceMode;
}

function setManualUI(mode, values) {
  manualMode = mode;
  const manual = mode !== 'program';
  if (manual) serviceMode = true;
  updateModeUI();
  $('chBars').classList.toggle('edit', mode === 'test');
  $('btnTest').classList.toggle('on', mode === 'test');
  $('btnNight').classList.toggle('on', mode === 'night');
  if (values) manualValues = values.slice();
  drawKnobs();
}

async function enterManual(mode) {
  try {
    const j = await apiPost('api/manual-enter', { mode });
    setManualUI(mode, j.values);
    refresh();
  } catch (e) { showAlert('Error', e.message, 'err'); }
}
$('btnTest').onclick = () => enterManual('test');
$('btnNight').onclick = () => enterManual('night');
$('btnMode').onclick = async () => {
  if (!serviceMode) { serviceMode = true; updateModeUI(); return; }
  try {
    if (manualMode !== 'program') await apiPost('api/manual-exit');
    serviceMode = false;
    setManualUI('program', null);
    refresh();
  } catch (e) { showAlert('Error', e.message, 'err'); }
};
// ── Settings ─────────────────────────────────────────────────────────────
(function buildChSettings() {
  $('chSettings').innerHTML = CH.map((c, i) =>
    '<label class="settings-field">' + c + ': power share [%]<input type="number" step="0.01" min="0" max="100" id="pf' + i + '"></label>' +
    '<label class="settings-field">' + c + ': gamma<input type="number" step="0.01" min="0.2" max="4" id="ga' + i + '"></label>' +
    '<label class="settings-field">' + c + ': min duty [0–16384]<input type="number" step="1" min="0" max="16384" id="md' + i + '"></label>' +
    '<label class="settings-field">' + c + ': label<input type="text" maxlength="11" id="lb' + i + '"></label>').join('');
})();

(function buildNightSettings() {
  $('nightSettings').innerHTML = CH.map((c, i) =>
    '<label class="settings-field">' + c + '<input type="number" step="0.1" min="0" max="100" id="nt' + i + '"></label>').join('');
})();

async function loadSettings() {
  const j = await apiGet('api/config');
  $('ramp_s').value = j.ramp_s; $('ramp_s').min = j.ramp_min; $('ramp_s').max = j.ramp_max;
  $('fan_on_pct').value = j.fan_on_pct;
  $('fan_off_pct').value = j.fan_off_pct;
  j.night.forEach((v, i) => { $('nt' + i).value = (v / 100).toFixed(2); });
  j.channels.forEach((c, i) => {
    $('pf' + i).value = (c.power_frac / 100).toFixed(2);
    $('ga' + i).value = c.gamma.toFixed(2);
    $('md' + i).value = c.min_duty;
    $('lb' + i).value = c.label;
  });
}
function toggleSettings() {
  const p = $('settingsPanel');
  const show = p.style.display === 'none';
  if (show) loadSettings().catch(e => showAlert('Error', e.message, 'err'));
  p.style.display = show ? '' : 'none';
}
$('settingsBtn').onclick = toggleSettings;
$('btnCancelSettings').onclick = toggleSettings;
$('btnSaveSettings').onclick = async () => {
  const body = { ramp_s: $('ramp_s').value, fan_on_pct: $('fan_on_pct').value, fan_off_pct: $('fan_off_pct').value };
  ['a', 'b', 'c', 'd'].forEach((k, i) => {
    body['pf_' + k] = Math.round(parseFloat($('pf' + i).value) * 100);
    body['gamma_' + k] = $('ga' + i).value;
    body['min_duty_' + k] = $('md' + i).value;
    body['label_' + k] = $('lb' + i).value;
  });
  const night = CH.map((c, i) => Math.round(parseFloat($('nt' + i).value) * 100));
  if (night.some(v => !Number.isFinite(v) || v < 0 || v > 10000)) {
    showAlert('Error', 'Night Light preset: 0–100 %.', 'err');
    return;
  }
  ['a', 'b', 'c', 'd'].forEach((k, i) => { body['night_' + k] = night[i]; });
  try {
    await apiPost('api/config', body);
    // Włączony Night Light startuje z presetu tylko przy wejściu — nowy preset od razu na wyjście
    if (manualMode === 'night') {
      await apiPost('api/manual-set', { ch_a: night[0], ch_b: night[1], ch_c: night[2], ch_d: night[3] });
    }
    showAlert('Saved', 'Settings stored in FRAM.', 'ok');
    toggleSettings();
  } catch (e) { showAlert('Error', e.message, 'err'); }
};

$('btnLogout').addEventListener('click', async () => {
  await fetch('api/logout', { method: 'POST' });
  window.location.href = 'login';
});

refresh().then(loadPrograms).catch(() => {});
// Odpytywanie: co 0,5 s w trakcie rampy i w trybie ręcznym (wartości się zmieniają), inaczej co 2 s
const POLL_FAST_MS = 500, POLL_SLOW_MS = 2000;
async function pollLoop() {
  await refresh();
  const fast = lastStatus && (lastStatus.ramp || lastStatus.mode !== 'program');
  const ms = fast ? POLL_FAST_MS : POLL_SLOW_MS;
  $('chBars').style.setProperty('--poll', ms + 'ms');
  setTimeout(pollLoop, ms);
}
setTimeout(pollLoop, POLL_SLOW_MS);
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
<title>RL90 Lamp — Login</title>
<style>
  :root {
    --bg-primary:#0a0f1a; --bg-card:#111827; --border:#2d3a4f;
    --text-primary:#f1f5f9; --accent-blue:#38bdf8; --accent-cyan:#22d3d5; --accent-red:#ef4444;
    --radius:12px;
  }
  body { font-family: sans-serif; background:var(--bg-primary); color:var(--text-primary); margin:0; padding:16px;
         display:flex; align-items:center; justify-content:center; min-height:100vh; box-sizing:border-box; }
  .card { background:var(--bg-card); border:1px solid var(--border); border-radius:var(--radius); padding:24px; width:280px; }
  h1 { color:var(--accent-blue); font-size:1.3em; margin-top:0; }
  input { width:100%; padding:8px; margin:8px 0; border-radius:8px; border:1px solid var(--border); background:#1e293b; color:var(--text-primary); box-sizing:border-box; }
  button { width:100%; padding:8px; border-radius:8px; border:none; background:linear-gradient(135deg,var(--accent-cyan),var(--accent-blue)); color:var(--bg-primary); font-weight:700; cursor:pointer; }
  #err { color:var(--accent-red); font-size:.9em; min-height:1.2em; }
</style>
</head>
<body>
<div class="card">
  <h1>RL90 Lamp</h1>
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
