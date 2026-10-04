#pragma once
// Interactive web dashboard, embedded in the binary and served by smeterd at "/".
// Self-contained (no external libraries / network needed): canvas charts + fetch() polling.
namespace sm {

inline const char* dashboard_html() {
    return R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Meter Agent</title>
<style>
:root{--bg:#0b1117;--card:#121a23;--fg:#e6edf3;--mut:#8493a3;--line:#223040;--acc:#2dd4bf;--ok:#4ade80;--warn:#fbbf24;--bad:#f87171}
:root[data-theme=light]{--bg:#f3f7f6;--card:#fff;--fg:#14211f;--mut:#5d716e;--line:#d9e4e2;--acc:#0d9488;--ok:#15803d;--warn:#b45309;--bad:#dc2626}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,Segoe UI,Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;gap:10px;align-items:center;padding:14px 20px;border-bottom:1px solid var(--line)}
h1{font-size:18px;margin:0;flex:1}.badge{padding:4px 12px;border-radius:99px;font-weight:700;font-size:12px;background:var(--line)}
.MEASURING{background:var(--ok);color:#06281a}.IDLE{background:var(--mut);color:#fff}.OVERLOAD{background:var(--bad);color:#fff;animation:p 1s infinite}.FAULT{background:var(--warn);color:#241700}.INIT{background:var(--line)}
@keyframes p{50%{opacity:.55}}
main{padding:16px 20px;display:grid;gap:14px;grid-template-columns:repeat(4,1fr)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.card h3{margin:0 0 6px;font-size:12px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.05em}
.big{font-size:30px;font-weight:700}.sub{color:var(--mut);font-size:12px;margin-top:2px}
.wide{grid-column:span 4}.half{grid-column:span 2}
.bar{height:8px;background:var(--line);border-radius:6px;margin-top:10px;overflow:hidden}.bar>i{display:block;height:100%;width:0;background:var(--ok);transition:width .4s,background .4s}
canvas{width:100%;height:220px;display:block;cursor:crosshair}
.tools{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin-bottom:8px}.tools h3{margin:0;flex:1}
button{background:var(--card);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:5px 11px;cursor:pointer;font:inherit}
button:hover{border-color:var(--acc)}button.on{background:var(--acc);border-color:var(--acc);color:#fff}
ul{list-style:none;margin:0;padding:0;max-height:210px;overflow:auto}li{padding:5px 8px;border-left:3px solid var(--acc);margin-bottom:4px;background:var(--bg);border-radius:4px;font-size:13px}
li.a{border-color:var(--warn)}li.o{border-color:var(--bad)}li.g{border-color:var(--ok)}
#err{display:none;background:var(--bad);color:#fff;padding:8px 20px;font-weight:600}
@media(max-width:900px){main{grid-template-columns:1fr 1fr}.wide,.half{grid-column:span 2}}
nav{display:flex;flex-wrap:wrap;gap:4px;padding:8px 20px 0;border-bottom:1px solid var(--line)}
nav button{border:0;border-bottom:3px solid transparent;border-radius:8px 8px 0 0;background:none;padding:9px 14px;font-weight:600;color:var(--mut)}
nav button.on{color:var(--fg);border-bottom-color:var(--acc)}
main[hidden]{display:none}
.zone{display:inline-block;padding:3px 10px;border-radius:99px;font-weight:700;font-size:12px;background:var(--line)}
.SAFE{background:var(--ok);color:#06281a}.APPROACHING{background:var(--warn);color:#241700}.HIGH{background:var(--bad);color:#fff}
#slabbar{position:relative;padding:22px 0 24px}.slabbar{display:flex;height:28px;border-radius:8px;overflow:hidden}
.seg{display:flex;align-items:center;justify-content:center;font-size:11px;color:#10131c;font-weight:600;white-space:nowrap;overflow:hidden}
.mark{position:absolute;top:8px;height:56px;width:2px;background:var(--fg)}.mark span{position:absolute;left:4px;font-size:11px;white-space:nowrap;top:-4px}.mark.p span{top:auto;bottom:-4px}
.rec{border-left:4px solid var(--acc);background:var(--bg);padding:10px 12px;border-radius:6px;margin-bottom:8px;line-height:1.45}
.rec b{display:block;margin-bottom:3px}.rec small{color:var(--ok);font-weight:600}.rec.slab,.rec.spike{border-color:var(--bad)}.rec.approach,.rec.action{border-color:var(--warn)}
.abar{margin:9px 0}.abar .t{display:flex;justify-content:space-between;font-size:12px;gap:8px}.abar .b{height:8px;background:var(--line);border-radius:6px;overflow:hidden;margin-top:3px}.abar .b i{display:block;height:100%;background:var(--acc)}
input,select{background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:8px 10px;font:inherit;width:100%}input[type=range]{padding:0}
label{font-size:12px;color:var(--mut);display:block;margin:11px 0 4px}
table{width:100%;border-collapse:collapse}td,th{padding:6px 8px;text-align:right;border-bottom:1px solid var(--line);font-size:13px}td:first-child,th:first-child{text-align:left}th{color:var(--mut);font-weight:600}
.alert{padding:10px 14px;border-radius:8px;background:var(--warn);color:#241700;font-weight:600}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:12px}.grid4 .card{padding:10px}
.row{display:flex;justify-content:space-between;gap:10px;padding:7px 0;border-bottom:1px solid var(--line)}.row span:first-child{color:var(--mut)}
@media(max-width:900px){.grid4{grid-template-columns:1fr 1fr}}

/* v3 interface: sidebar navigation, gauge, theme toggle */
nav{position:fixed;left:0;top:0;bottom:0;width:196px;flex-direction:column;flex-wrap:nowrap;gap:4px;padding:76px 10px 10px;border-right:1px solid var(--line);border-bottom:0;background:var(--card);z-index:5}
nav::before{content:"\26A1  SmartMeter";position:absolute;top:22px;left:16px;font-weight:800;font-size:17px;color:var(--acc)}
nav button{text-align:left;border:0;border-left:3px solid transparent;border-radius:10px;padding:11px 12px;font-weight:600;color:var(--mut)}
nav button.on{background:color-mix(in srgb,var(--acc) 15%,transparent);border-left-color:var(--acc);color:var(--fg)}
header,main,#err{margin-left:196px}
header{position:sticky;top:0;z-index:4;background:var(--bg)}
.card{border-radius:16px;box-shadow:0 1px 0 #0001}
.big{letter-spacing:-.02em}
#gwrap{text-align:center;margin-top:-4px}
@media(max-width:900px){nav{top:auto;right:0;width:auto;flex-direction:row;overflow-x:auto;padding:6px;border-right:0;border-top:1px solid var(--line)}nav::before{display:none}header,main,#err{margin-left:0}main{padding-bottom:84px}}
</style></head><body>
<div id="dlg" style="display:none;position:fixed;inset:0;background:#0009;align-items:center;justify-content:center;z-index:9"><div class="card" style="max-width:380px;margin:16px"><div id="dmsg" style="white-space:pre-line;margin-bottom:14px"></div><div style="text-align:right"><button id="dno">Cancel</button> <button id="dyes" class="on">OK</button></div></div></div>
<div id="err">Cannot reach smeterd - retrying...</div>
<header><h1>&#9889; Smart Meter Agent</h1><span id="src" class="sub"></span><span id="clock" class="sub"></span>
<span id="state" class="badge INIT">INIT</span><button id="theme" title="Toggle light / dark">&#9680; Theme</button><button id="pause">&#10074;&#10074; Pause</button><button id="reset">&#10226; Reset counters</button><button id="restart" style="border-color:var(--bad);color:var(--bad)">&#8635; Restart daemon</button></header>
<nav id="tabs"><button data-t="over" class="on">📊 Live Meter</button><button data-t="agent">🧠 Smart Agent</button><button data-t="whatif">💡 What if I save?</button><button data-t="bill">🧮 Bill Calculator</button><button data-t="budget">🎯 Budget &amp; Carbon</button></nav>
<main id="t-over">
<div class="card"><h3>Power now</h3><div class="big" id="pw">-</div><div class="sub" id="sm"></div><div id="gwrap"><svg viewBox="0 0 120 68" style="width:100%;max-width:210px"><path d="M10 62A50 50 0 0 1 110 62" fill="none" stroke="var(--line)" stroke-width="10" stroke-linecap="round"/><path id="garc" d="M10 62A50 50 0 0 1 110 62" fill="none" stroke="var(--ok)" stroke-width="10" stroke-linecap="round" stroke-dasharray="0 157.1" style="transition:stroke-dasharray .4s,stroke .4s"/></svg></div><div class="sub" id="lim"></div></div>
<div class="card"><h3>Energy</h3><div class="big" id="en">-</div><div class="sub" id="pl"></div></div>
<div class="card"><h3>Cost</h3><div class="big" id="co">-</div><div class="sub" id="ra"></div></div>
<div class="card"><h3>Peak demand</h3><div class="big" id="pk">-</div><div class="sub" id="qa"></div></div>
<div class="card wide"><div class="tools"><h3>Live power (last 2 min, 1 s samples) - hover for values</h3></div><canvas id="c1"></canvas></div>
<div class="card wide"><div class="tools"><h3>Per-minute history</h3>
 <span id="metric"><button data-m="avg_w" class="on">Avg W</button><button data-m="peak_w">Peak W</button><button data-m="energy_wh">Energy Wh</button></span>
 <span id="range"><button data-n="15">15m</button><button data-n="60" class="on">1h</button><button data-n="240">4h</button><button data-n="1440">24h</button></span>
 <button id="csv">&#11015; CSV</button></div><canvas id="c2"></canvas></div>
<div class="card half"><h3>Event log</h3><ul id="ev"></ul></div>
<div class="card half"><h3>Counters</h3><div id="cnt" class="sub" style="line-height:1.9"></div></div>
</main>
<main id="t-agent" hidden>
<div class="card wide" id="a-alert" style="display:none"></div>
<div class="card"><h3>Units this month</h3><div class="big" id="a-units">-</div><div class="sub" id="a-days"></div></div>
<div class="card"><h3>Current estimated bill</h3><div class="big" id="a-bill">-</div><div class="sub" id="a-bill2"></div></div>
<div class="card"><h3>Current slab</h3><div class="big" id="a-slab">-</div><div class="sub" id="a-rate"></div></div>
<div class="card"><h3>Units to next slab</h3><div class="big" id="a-next">-</div><div class="sub"><span id="a-zone" class="zone">-</span></div></div>
<div class="card wide"><h3>Slab position: now vs projected month-end</h3><div id="slabbar"></div><div id="a-msg"></div></div>
<div class="card"><h3>Projected month units</h3><div class="big" id="p-units">-</div><div class="sub" id="p-sub"></div></div>
<div class="card"><h3>Projected monthly bill</h3><div class="big" id="p-bill">-</div><div class="sub" id="p-bill2"></div></div>
<div class="card"><h3>Expected slab</h3><div class="big" id="p-slab">-</div><div class="sub" id="p-slab2"></div></div>
<div class="card"><h3>Peak consumption period</h3><div class="big" id="p-peak" style="font-size:22px">-</div><div class="sub" id="p-peak2"></div></div>
<div class="card"><h3>Potential units saved</h3><div class="big" id="s-units">-</div><div class="sub">trim top-3 appliances 20% for the rest of the month</div></div>
<div class="card"><h3>Potential money saved</h3><div class="big" id="s-money">-</div><div class="sub" id="s-money2"></div></div>
<div class="card"><h3>Units to cut to avoid slab</h3><div class="big" id="s-need">-</div><div class="sub" id="s-need2"></div></div>
<div class="card"><h3>Daily saving target</h3><div class="big" id="s-daily">-</div><div class="sub" id="s-daily2"></div></div>
<div class="card"><h3>Top consuming appliance</h3><div class="big" id="i-top" style="font-size:20px">-</div><div class="sub" id="i-top2"></div></div>
<div class="card"><h3>Current warning</h3><div id="i-warn" style="line-height:1.45">-</div></div>
<div class="card half"><h3>Top 3 personalised recommendations</h3><div id="i-recs"></div></div>
<div class="card"><h3>Today so far</h3><div class="big" id="t-today">-</div><div class="sub" id="t-today2"></div></div>
<div class="card"><h3>Yesterday</h3><div class="big" id="t-yday">-</div><div class="sub" id="t-yday2"></div></div>
<div class="card"><h3>7-day average</h3><div class="big" id="t-avg7">-</div><div class="sub" id="t-avg72"></div></div>
<div class="card"><h3>Consumption trend</h3><div class="big" id="t-trend" style="font-size:22px">-</div><div class="sub" id="t-trend2"></div></div>
<div class="card half"><div class="tools"><h3 id="ac-title">Daily consumption</h3>
 <span id="ac-mode"><button data-m="daily" class="on">Daily</button><button data-m="hourly">Hourly</button><button data-m="weekly">Weekly</button></span></div><canvas id="c3"></canvas></div>
<div class="card half"><h3>Appliance consumption breakdown (estimated per month)</h3><div id="a-brk"></div></div>
</main>
<main id="t-whatif" hidden>
<div class="card half"><h3>What if I save?</h3>
 <label>Appliance</label><select id="w-app"></select>
 <label id="w-lab">Proposed usage</label><input type="range" id="w-h" min="0" max="24" step="0.25">
 <div class="sub" id="w-cur" style="margin-top:8px"></div></div>
<div class="card half"><h3>Result</h3><div id="w-sum" class="big" style="font-size:18px;line-height:1.4">-</div><div class="grid4" id="w-nums"></div></div>
<div class="card wide"><h3>Appliance register (watts x hours x days, priced with the slab tariff)</h3><div style="overflow-x:auto"><table id="w-tab"></table></div></div>
</main>
<main id="t-bill" hidden>
<div class="card half"><h3>Smart bill calculator</h3>
 <label>Current consumed units *</label><input id="b-units" type="number" min="0" step="0.1" value="92">
 <label>Days elapsed in the billing period *</label><input id="b-days" type="number" min="1" step="1" value="27">
 <label>Days in the full month</label><input id="b-md" type="number" min="1" step="1" value="30">
 <label>Previous month's units (optional)</label><input id="b-prev" type="number" min="0" step="0.1" placeholder="e.g. 105">
 <label>Average daily consumption, units/day (optional)</label><input id="b-avg" type="number" min="0" step="0.1" placeholder="e.g. 3.4">
 <button class="on" id="b-go" style="margin-top:14px;width:100%">Calculate</button></div>
<div class="card half" id="b-out"><h3>Result</h3><div class="sub">Enter your readings and press Calculate.</div></div>
</main>
<main id="t-budget" hidden>
<div class="card half"><h3>Monthly goals</h3>
 <label>Monthly bill budget (<span id="m-cur">&#8377;</span>)</label><input id="m-bud" type="number" min="0" step="10">
 <label>Grid emission factor (kg CO&#8322; per kWh)</label><input id="m-co2" type="number" min="0" step="0.01">
 <div class="sub" style="margin-top:10px">Saved in this browser only. 0.82 is a typical Indian grid average; edit it for your region.</div></div>
<div class="card half"><h3>Budget status</h3><span id="m-badge" class="zone">-</span><div class="big" id="m-used" style="margin-top:8px">-</div><div class="bar"><i id="m-bar"></i></div><div class="sub" id="m-used2"></div></div>
<div class="card"><h3>Projected bill</h3><div class="big" id="m-proj">-</div><div class="sub" id="m-proj2"></div></div>
<div class="card"><h3>Daily allowance</h3><div class="big" id="m-allow">-</div><div class="sub" id="m-allow2"></div></div>
<div class="card"><h3>CO&#8322; so far</h3><div class="big" id="m-ca">-</div><div class="sub" id="m-ca2"></div></div>
<div class="card"><h3>CO&#8322; projected</h3><div class="big" id="m-cb">-</div><div class="sub">for the full month</div></div>
<div class="card wide"><h3>Estimated CO&#8322; by appliance (per month)</h3><div id="m-brk"></div></div>
</main>
<script>
const $=i=>document.getElementById(i),cs=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
class Chart{
 constructor(cv,unit){this.cv=cv;this.unit=unit;this.ys=[];this.xs=[];this.hi=null;this.limit=null;this.bars=false;
  cv.addEventListener('mousemove',e=>{const r=cv.getBoundingClientRect(),n=this.ys.length;if(!n)return;
   const f=(e.clientX-r.left-46)/(r.width-56);this.hi=Math.max(0,Math.min(n-1,Math.round(f*(n-1))));this.draw();});
  cv.addEventListener('mouseleave',()=>{this.hi=null;this.draw();});addEventListener('resize',()=>this.draw());}
 set(ys,xs){this.ys=ys;this.xs=xs;this.draw();}
 draw(){const cv=this.cv,r=cv.getBoundingClientRect(),d=devicePixelRatio||1;cv.width=r.width*d;cv.height=r.height*d;
  const c=cv.getContext('2d');c.setTransform(d,0,0,d,0,0);const W=r.width,H=r.height,L=46,R=10,T=10,B=22,n=this.ys.length;
  c.font='11px system-ui';c.fillStyle=cs('--mut');c.strokeStyle=cs('--line');
  let mx=Math.max(this.minmax||1,...this.ys,...(this.ys2||[]),this.limit||0)*1.1;
  for(let i=0;i<=4;i++){const y=T+(H-T-B)*i/4;c.beginPath();c.moveTo(L,y);c.lineTo(W-R,y);c.stroke();c.fillText((mx*(1-i/4)).toFixed(mx<10?1:0),4,y+4);}
  if(!n){c.fillText('waiting for data...',L+10,H/2);return;}
  const X=i=>L+(n>1?i/(n-1):.5)*(W-L-R),Y=v=>T+(H-T-B)*(1-v/mx),acc=cs('--acc');
  if(this.limit){c.strokeStyle=cs('--bad');c.setLineDash([6,4]);c.beginPath();c.moveTo(L,Y(this.limit));c.lineTo(W-R,Y(this.limit));c.stroke();c.setLineDash([]);c.fillStyle=cs('--bad');c.fillText('overload limit',L+4,Y(this.limit)-4);}
  c.strokeStyle=acc;c.fillStyle=acc;
  if(this.bars){const bw=Math.max(1,(W-L-R)/n-1);this.ys.forEach((v,i)=>c.fillRect(X(i)-bw/2,Y(v),bw,H-B-Y(v)));}
  else{c.lineWidth=2;c.beginPath();this.ys.forEach((v,i)=>i?c.lineTo(X(i),Y(v)):c.moveTo(X(i),Y(v)));c.stroke();c.lineWidth=1;
   const g=c.createLinearGradient(0,T,0,H-B);g.addColorStop(0,acc+'55');g.addColorStop(1,acc+'00');c.lineTo(X(n-1),H-B);c.lineTo(X(0),H-B);c.fillStyle=g;c.fill();}
  if(this.ys2&&this.ys2.length){c.strokeStyle=cs('--warn');c.lineWidth=2;c.setLineDash([5,4]);c.beginPath();this.ys2.forEach((v,i)=>i?c.lineTo(X(i),Y(v)):c.moveTo(X(i),Y(v)));c.stroke();c.setLineDash([]);c.lineWidth=1;}
  c.fillStyle=cs('--mut');c.fillText(this.xs[0]||'',L,H-6);const l=this.xs[n-1]||'';c.fillText(l,W-R-c.measureText(l).width,H-6);
  if(this.hi!=null){const i=this.hi,x=X(i),y=Y(this.ys[i]);c.strokeStyle=cs('--fg');c.beginPath();c.moveTo(x,T);c.lineTo(x,H-B);c.stroke();
   c.fillStyle=acc;c.beginPath();c.arc(x,y,4,0,7);c.fill();
   const t=this.ys[i].toFixed(this.dec==null?1:this.dec)+' '+this.unit+(this.ys2?' (typical '+this.ys2[i].toFixed(2)+')':'')+'  '+(this.xs[i]||''),w=c.measureText(t).width+14,bx=Math.min(Math.max(x-w/2,L),W-R-w);
   c.fillStyle=cs('--card');c.strokeStyle=cs('--line');c.fillRect(bx,T+2,w,22);c.strokeRect(bx,T+2,w,22);c.fillStyle=cs('--fg');c.fillText(t,bx+7,T+17);}}
}
const live=new Chart($('c1'),'W'),hist=new Chart($('c2'),'W');hist.bars=true;
let liveY=[],liveX=[],metric='avg_w',range=60,paused=false,lastHist=null,lastStatus=null;
const hhmm=s=>{const d=new Date(s*1000);return d.toTimeString().slice(0,8);};
const units={avg_w:'W',peak_w:'W',energy_wh:'Wh'};
function renderHist(){if(!lastHist)return;hist.unit=units[metric];hist.set(lastHist[metric],lastHist.t.map(t=>hhmm(t)));}
async function poll(){
 try{
  const s=await (await fetch('/api/status')).json();lastStatus=s;$('err').style.display='none';restarting=false;restarting=false;
  $('state').textContent=s.state;$('state').className='badge '+s.state;$('src').textContent=s.source;$('clock').textContent='meter time '+hhmm(s.wall_s);
  $('pw').textContent=s.power_w.toFixed(0)+' W';$('sm').textContent='smoothed '+s.smooth_w.toFixed(0)+' W';
  const pct=Math.min(100,s.power_w/s.overload_w*100);$('garc').style.strokeDasharray=(pct*1.571)+' 157.1';$('garc').style.stroke=pct>90?cs('--bad'):pct>60?cs('--warn'):cs('--ok');
  $('lim').textContent=pct.toFixed(0)+'% of '+s.overload_w+' W limit';
  $('en').textContent=s.energy_kwh.toFixed(3)+' kWh';$('pl').textContent=s.pulses+' pulses';
  $('co').textContent=s.cost.toFixed(2);$('ra').textContent='current rate '+s.rate.toFixed(2)+' / kWh';
  $('pk').textContent=s.peak_w.toFixed(0)+' W';$('qa').textContent=s.spikes+' anomalies flagged';
  $('cnt').innerHTML='Pulses counted: <b>'+s.pulses+'</b><br>Bounce rejected: <b>'+s.debounced+'</b><br>Out-of-order: <b>'+s.out_of_order+'</b><br>Spikes: <b>'+s.spikes+'</b>';
  $('ev').innerHTML=s.events.slice().reverse().map(e=>'<li class="'+(e.includes('OVERLOAD')?'o':e.includes('anomaly')?'a':e.includes('MEASURING')?'g':'')+'">'+e+'</li>').join('')||'<li>no events yet</li>';
  liveY.push(s.power_w);liveX.push(new Date().toTimeString().slice(0,8));if(liveY.length>120){liveY.shift();liveX.shift();}
  live.limit=s.overload_w;live.set(liveY.slice(),liveX.slice());
  const h=await (await fetch('/api/history?n='+range)).json();lastHist=h;renderHist();
 }catch(e){$('err').textContent=restarting?'Restarting daemon... reconnecting automatically':'Cannot reach smeterd - retrying...';$('err').style.display='block';}
}
let restarting=false;
const ask=m=>new Promise(r=>{$('dmsg').textContent=m;$('dlg').style.display='flex';const done=v=>{$('dlg').style.display='none';r(v);};$('dyes').onclick=()=>done(true);$('dno').onclick=()=>done(false);});
const toast=m=>{$('err').textContent=m;$('err').style.display='block';setTimeout(()=>{$('err').style.display='none';},3000);};
const post=async p=>{const r=await fetch(p,{method:'POST',headers:{'X-Smeter':'1'}});if(!r.ok)throw 0;};
$('restart').onclick=async()=>{
 if(!await ask('Restart the smeterd daemon?\n\nThe whole program restarts (a few seconds). Energy and cost totals are kept; live charts and history start fresh.'))return;
 try{await post('/api/restart');restarting=true;liveY=[];liveX=[];lastHist=null;live.set([],[]);hist.set([],[]);
  $('err').textContent='Restarting daemon... reconnecting automatically';$('err').style.display='block';}
 catch(e){toast('Restart failed - is smeterd running?');}};
$('reset').onclick=async()=>{
 if(!await ask('Reset counters?\n\nEnergy, cost, history and events go back to zero and the simulation restarts.'))return;
 try{await post('/api/reset');liveY=[];liveX=[];lastHist=null;live.set([],[]);hist.set([],[]);setTimeout(poll,300);}
 catch(e){toast('Reset failed - is smeterd running?');}};
$('pause').onclick=()=>{paused=!paused;$('pause').innerHTML=paused?'&#9654; Resume':'&#10074;&#10074; Pause';$('pause').classList.toggle('on',paused);};
document.querySelectorAll('#metric button').forEach(b=>b.onclick=()=>{metric=b.dataset.m;document.querySelectorAll('#metric button').forEach(x=>x.classList.toggle('on',x===b));renderHist();});
document.querySelectorAll('#range button').forEach(b=>b.onclick=()=>{range=+b.dataset.n;document.querySelectorAll('#range button').forEach(x=>x.classList.toggle('on',x===b));poll();});
$('csv').onclick=()=>{if(!lastHist)return;let t='time,avg_w,peak_w,energy_wh\n';lastHist.t.forEach((x,i)=>t+=hhmm(x)+','+lastHist.avg_w[i]+','+lastHist.peak_w[i]+','+lastHist.energy_wh[i]+'\n');
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([t],{type:'text/csv'}));a.download='smeter_history.csv';a.click();};
/* ===================== Smart Energy Agent tabs ===================== */
let tab='over',AG=null,acMode='daily',APPS=[],CUR='\u20B9',wTimer=null;
const f=(v,d=0)=>Number(v).toFixed(d),fa=v=>Math.abs(v-Math.round(v))<0.05?f(v,0):f(v,1);
const ach=new Chart($('c3'),'kWh');ach.bars=true;ach.dec=2;
const SEGC=['#3ddc97','#ffb020','#ff7a45','#ff5d5d'],ZN={SAFE:'\uD83D\uDFE2 Safe zone',APPROACHING:'\uD83D\uDFE1 Approaching slab',HIGH:'\uD83D\uDD34 High consumption'};
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{tab=b.dataset.t;
 document.querySelectorAll('#tabs button').forEach(x=>x.classList.toggle('on',x===b));
 ['over','agent','whatif','bill','budget'].forEach(t=>$('t-'+t).hidden=(t!==tab));
 if(tab==='agent')loadAgent();if(tab==='budget')loadBudget();if(tab==='whatif')initWhatIf();if(tab==='bill'&&!$('b-out').dataset.done)runBill();
 setTimeout(()=>{live.draw();hist.draw();ach.draw();},60);});

async function loadAgent(){
 let d;try{d=await (await fetch('/api/agent')).json();}catch(e){return;}
 AG=d;CUR=d.cur;const c=d.cur,s=d.status,p=d.prediction,k=d.consumption,v=d.savings,i=d.intelligence,cal=d.calendar;
 $('a-units').textContent=f(s.units,1)+' units';$('a-days').textContent='day '+cal.day+' of '+cal.dim+' \u2022 '+f(k.avg_daily,1)+' units/day avg';
 $('a-bill').textContent=c+f(s.bill);$('a-bill2').textContent='energy '+c+f(s.energy_charge)+' + fixed '+c+f(s.fixed);
 $('a-slab').textContent=s.slab_label;$('a-rate').textContent=c+f(s.rate,2)+' per unit';
 $('a-next').textContent=s.to_next<0?'top slab':fa(s.to_next)+' units';
 const z=$('a-zone');z.textContent=ZN[s.zone];z.className='zone '+s.zone;
 const al=$('a-alert');if(k.spike_msg){const sp=k.spike;al.className='card wide alert';al.style.display='block';
  al.textContent='\u26A0 '+k.spike_msg+(sp?' '+f(sp.actual,1)+' kWh used vs ~'+f(sp.expected,1)+' kWh typical for those hours.':'');}else al.style.display='none';
 // slab position bar
 const sl=d.tariff.slabs,fin=sl.filter(x=>x.upto!=null),max=Math.ceil(fin[fin.length-1].upto*1.35/50)*50;let lo=0,h='<div class="slabbar">';
 sl.forEach((x,j)=>{const hi=x.upto==null?max:Math.min(x.upto,max);h+='<div class="seg" style="width:'+((hi-lo)/max*100)+'%;background:'+SEGC[j%4]+'">'+x.label+' \u2022 '+c+f(x.rate,2)+'</div>';lo=hi;});
 const pos=u=>Math.min(99.5,u/max*100);
 h+='</div><div class="mark" style="left:'+pos(s.units)+'%"><span>now '+f(s.units,0)+'</span></div><div class="mark p" style="left:'+pos(p.units)+'%"><span>projected '+f(p.units,0)+'</span></div>';
 $('slabbar').innerHTML=h;$('a-msg').innerHTML='<b>'+s.message+'</b>';
 // prediction
 $('p-units').textContent=f(p.units,0)+' units';$('p-sub').textContent=f(cal.remaining,1)+' days left in the month';
 $('p-bill').textContent=c+f(p.bill);$('p-bill2').textContent=(p.bill>s.bill?'+':'')+c+f(p.bill-s.bill)+' more than the bill so far';
 $('p-slab').textContent=p.slab_label;$('p-slab2').textContent=p.to_next<0?'highest slab':fa(p.to_next)+' units below the next slab';
 $('p-peak').textContent=k.peak?k.peak.label:'learning...';$('p-peak2').textContent=k.peak?f(k.peak.share,0)+'% of daily usage ('+f(k.peak.kwh,1)+' kWh)':'';
 // savings
 $('s-units').textContent=f(v.potential_units,1)+' units';$('s-money').textContent=c+f(v.potential_money);$('s-money2').textContent='at your projected slab rates';
 $('s-need').textContent=v.needed_units>0?f(v.needed_units,1)+' units':'none';$('s-need2').textContent=v.needed_units>0?'to fall back below the '+f(s.threshold,0)+'-unit boundary':'no slab crossing projected';
 $('s-daily').textContent=v.daily_target>0?f(v.daily_target,2)+' units/day':(v.daily_allowance>0?'\u2264 '+f(v.daily_allowance,1)+' units/day':'on track');
 $('s-daily2').textContent=v.daily_target>0?'reduction to stay below the next slab':(v.daily_allowance>0?'daily allowance to stay in your slab':'no reduction needed');
 // intelligence
 $('i-top').textContent=i.top_appliance||'-';$('i-top2').textContent=f(i.top_kwh,0)+' units/month \u2022 '+f(i.top_share,0)+'% of estimated usage';
 const zc={SAFE:'var(--ok)',APPROACHING:'var(--warn)',HIGH:'var(--bad)'}[s.zone];
 $('i-warn').innerHTML='<span class="zone '+s.zone+'">'+ZN[s.zone]+'</span><div style="margin-top:8px">'+i.warning+'</div>';
 $('i-recs').innerHTML=i.recommendations.slice(0,3).map(r=>'<div class="rec '+r.id+'"><b>'+r.title+'</b>'+r.detail+((r.units>0||r.money>0)?'<br><small>\u2248 '+f(r.units,1)+' units \u2022 '+c+f(r.money)+' saving</small>':'')+'</div>').join('');
 // trend
 const arrow=x=>(x>=0?'\u25B2 ':'\u25BC ')+f(Math.abs(x),0)+'%';
 $('t-today').textContent=f(k.today,2)+' kWh';$('t-today2').textContent=d.ready?arrow(k.pct_vs_usual)+' vs usual for this time of day':'';
 $('t-yday').textContent=f(k.yesterday,2)+' kWh';$('t-yday2').textContent=arrow(k.pct_vs_yesterday)+' today vs yesterday (same time)';
 $('t-avg7').textContent=f(k.avg7,2)+' kWh/day';$('t-avg72').textContent='last 7 days total '+f(k.last7,1)+' kWh';
 $('t-trend').textContent={increasing:'\u2197 Increasing',decreasing:'\u2198 Decreasing',stable:'\u2192 Stable'}[k.trend];$('t-trend2').textContent=f(k.trend_pct,1)+'% (last 3 days vs previous 4)';
 // analytics
 drawAc();const a=d.analytics,mx=Math.max.apply(null,a.appliances.map(x=>x.kwh));
 $('a-brk').innerHTML=a.appliances.map(x=>'<div class="abar"><div class="t"><span>'+x.name+'</span><span>'+f(x.kwh,1)+' units \u2022 '+c+f(x.cost)+' \u2022 '+f(x.share,0)+'%</span></div><div class="b"><i style="width:'+(x.kwh/mx*100)+'%"></i></div></div>').join('')
  +'<div class="sub">Modelled total '+f(i.modeled_month,0)+' units/month vs projected '+f(p.units,0)+'.</div>';
}
function drawAc(){if(!AG)return;const a=AG.analytics;
 if(acMode==='daily'){ach.ys2=null;$('ac-title').textContent='Daily consumption, last 14 days (kWh)';
  ach.set(a.daily,a.daily.map((_,j)=>new Date((AG.now-(a.daily.length-1-j)*86400)*1000).toLocaleDateString(undefined,{day:'numeric',month:'short'})));}
 else if(acMode==='hourly'){ach.ys2=a.hourly_typical;$('ac-title').textContent='Today by hour vs your typical hour (dashed)';ach.set(a.hourly_today,a.hourly_today.map((_,j)=>j+':00'));}
 else{ach.ys2=null;$('ac-title').textContent='Weekly consumption, last 4 weeks (kWh)';ach.set(a.weekly,['3 weeks ago','2 weeks ago','last week','last 7 days']);}}
document.querySelectorAll('#ac-mode button').forEach(b=>b.onclick=()=>{acMode=b.dataset.m;document.querySelectorAll('#ac-mode button').forEach(x=>x.classList.toggle('on',x===b));drawAc();});

/* ---------- What if I save? ---------- */
async function initWhatIf(){
 if(APPS.length)return;
 try{APPS=await (await fetch('/api/appliances')).json();}catch(e){return;}
 $('w-app').innerHTML=APPS.map((x,j)=>'<option value="'+j+'">'+x.name+'</option>').join('');
 $('w-tab').innerHTML='<tr><th>Appliance</th><th>Watts</th><th>Hours/day</th><th>Days/month</th><th>kWh/month</th><th>Cost/month</th><th>Share</th></tr>'
  +APPS.map(x=>'<tr><td>'+x.name+'</td><td>'+x.watts+'</td><td>'+fa(x.hours)+'</td><td>'+x.days+'</td><td>'+f(x.kwh,1)+'</td><td>'+CUR+f(x.cost)+'</td><td>'+f(x.share,0)+'%</td></tr>').join('');
 $('w-app').onchange=wSelect;$('w-h').oninput=()=>{wLabel();clearTimeout(wTimer);wTimer=setTimeout(runWhatIf,120);};wSelect();}
const wApp=()=>APPS[+$('w-app').value];
function wLabel(){const a=wApp();$('w-lab').textContent='Proposed usage: '+fa(+$('w-h').value)+' hours/day  (currently '+fa(a.hours)+')';
 $('w-cur').textContent=a.name+': '+a.watts+' W \u2022 '+fa(a.hours)+' h/day \u2022 '+a.days+' days/month = '+f(a.kwh,1)+' units/month';}
function wSelect(){const a=wApp();$('w-h').value=a.hours;wLabel();runWhatIf();}
async function runWhatIf(){const a=wApp();if(!a)return;
 const r=await (await fetch('/api/whatif?appliance='+encodeURIComponent(a.name)+'&hours='+$('w-h').value)).json();if(r.error)return;
 $('w-sum').textContent=r.summary;
 $('w-nums').innerHTML=[['Current monthly',f(r.current_kwh,1)+' units'],['New monthly',f(r.new_kwh,1)+' units'],['Units saved',f(r.units_saved,1)],['Money saved',r.cur+f(r.money_saved)],
  ['Reduction (this appliance)',f(r.pct_reduction,0)+'%'],['Share of total usage',f(r.pct_of_total,1)+'%'],['Bill before',r.cur+f(r.bill_before)],['Bill after',r.cur+f(r.bill_after)]]
  .map(x=>'<div class="card"><h3>'+x[0]+'</h3><div class="big" style="font-size:20px">'+x[1]+'</div></div>').join('');}

/* ---------- Smart bill calculator ---------- */
async function runBill(){
 const q=new URLSearchParams({units:$('b-units').value||0,days:$('b-days').value||1,month_days:$('b-md').value||30});
 if($('b-prev').value)q.set('prev',$('b-prev').value);if($('b-avg').value)q.set('avg',$('b-avg').value);
 const o=$('b-out');let r;try{r=await (await fetch('/api/bill?'+q)).json();}catch(e){o.innerHTML='<div class="alert">Cannot reach smeterd.</div>';return;}
 o.dataset.done=1;if(r.error){o.innerHTML='<h3>Result</h3><div class="alert">'+r.error+'</div>';return;}
 const c=r.cur,row=(a,b)=>'<div class="row"><span>'+a+'</span><b>'+b+'</b></div>';
 o.innerHTML='<h3>Result</h3><span class="zone '+r.zone+'">'+ZN[r.zone]+'</span>'
  +'<div style="margin:10px 0;line-height:1.45"><b>'+r.warning+'</b></div>'
  +row('Total units consumed',f(r.units,1))+row('Estimated electricity charge (so far)',c+f(r.charge)+' <small>('+c+f(r.energy_charge)+' energy + '+c+f(r.fixed)+' fixed)</small>')
  +row('Average units/day',f(r.avg_per_day,2))+row('Estimated units for the complete month',f(r.projected_units,1))
  +row('Estimated monthly bill',c+f(r.projected_bill))+row('Current slab \u2192 projected slab',r.slab+' \u2192 '+r.proj_slab)
  +row('Units remaining before next slab',r.to_next<0?'top slab':fa(r.to_next)+' units')
  +row('Potential savings',f(r.potential_units,1)+' units \u2248 '+c+f(r.potential_money))
  +(r.has_prev?row('vs previous month',(r.prev_change_pct>=0?'\u25B2 ':'\u25BC ')+f(Math.abs(r.prev_change_pct),1)+'%'):'')
  +'<div class="rec" style="margin-top:12px"><b>Recommended action</b>'+r.action+'</div>'
  +'<table style="margin-top:8px"><tr><th>Slab</th><th>Units</th><th>Rate</th><th>Amount</th></tr>'+r.lines.map(l=>'<tr><td>'+l.slab+'</td><td>'+f(l.units,1)+'</td><td>'+c+f(l.rate,2)+'</td><td>'+c+f(l.amount)+'</td></tr>').join('')+'</table>';}
$('b-go').onclick=runBill;['b-units','b-days','b-md','b-prev','b-avg'].forEach(id=>$(id).addEventListener('keydown',e=>{if(e.key==='Enter')runBill();}));
setInterval(()=>{if(tab==='agent'&&!paused)loadAgent();},2000);
/* ---------- Theme, Budget & Carbon (v3) ---------- */
const LS={get(k,d){try{const v=localStorage.getItem(k);return v==null?d:v;}catch(e){return d;}},set(k,v){try{localStorage.setItem(k,v);}catch(e){}}};
function setTheme(t){document.documentElement.dataset.theme=t;LS.set('sm_theme',t);live.draw();hist.draw();ach.draw();}
setTheme(LS.get('sm_theme',matchMedia('(prefers-color-scheme:light)').matches?'light':'dark'));
$('theme').onclick=()=>setTheme(document.documentElement.dataset.theme==='light'?'dark':'light');
$('m-bud').value=LS.get('sm_budget','600');$('m-co2').value=LS.get('sm_co2','0.82');
['m-bud','m-co2'].forEach(id=>$(id).addEventListener('input',()=>{LS.set('sm_budget',$('m-bud').value);LS.set('sm_co2',$('m-co2').value);loadBudget();}));
function unitsFor(B,sl,fixed){let left=B-fixed,u=0,lo=0;if(left<=0)return 0;
 for(const x of sl){const hi=x.upto==null?Infinity:x.upto,cap=(hi-lo)*x.rate;if(left<=cap)return u+left/x.rate;left-=cap;u+=hi-lo;lo=hi;}return u;}
async function loadBudget(){
 let d;try{d=await (await fetch('/api/agent')).json();}catch(e){return;}
 AG=d;const c=d.cur,s=d.status,p=d.prediction,cal=d.calendar,B=+$('m-bud').value||0,F=+$('m-co2').value||0;
 $('m-cur').textContent=c;
 const used=B>0?s.bill/B*100:0,zone=s.bill>B?'HIGH':p.bill>B?'APPROACHING':'SAFE',cap=unitsFor(B,d.tariff.slabs,s.fixed),room=cap-s.units;
 const b=$('m-badge');b.className='zone '+zone;b.textContent={HIGH:'\uD83D\uDD34 Over budget',APPROACHING:'\uD83D\uDFE1 At risk',SAFE:'\uD83D\uDFE2 On track'}[zone];
 $('m-used').textContent=c+f(s.bill)+' of '+c+f(B);$('m-bar').style.width=Math.min(100,used)+'%';$('m-bar').style.background={HIGH:cs('--bad'),APPROACHING:cs('--warn'),SAFE:cs('--ok')}[zone];
 $('m-used2').textContent=f(used,0)+'% of the budget used, day '+cal.day+' of '+cal.dim;
 $('m-proj').textContent=c+f(p.bill);$('m-proj2').textContent=p.bill>B?c+f(p.bill-B)+' over budget':c+f(B-p.bill)+' under budget';
 $('m-allow').textContent=room>0&&cal.remaining>0?f(room/cal.remaining,1)+' units/day':'none left';
 $('m-allow2').textContent=room>0?'keeps the bill within budget (about '+f(cap,0)+' units in total)':'budget already reached';
 const kg=s.units*F,kp=p.units*F;
 $('m-ca').textContent=f(kg,1)+' kg';$('m-ca2').textContent='about '+f(kg/1.75,1)+' tree-months of absorption (rough estimate)';
 $('m-cb').textContent=f(kp,1)+' kg';
 const a=d.analytics.appliances,mx=Math.max.apply(null,a.map(x=>x.kwh));
 $('m-brk').innerHTML=a.map(x=>'<div class="abar"><div class="t"><span>'+x.name+'</span><span>'+f(x.kwh*F,1)+' kg CO\u2082 \u2022 '+f(x.kwh,1)+' units</span></div><div class="b"><i style="width:'+(x.kwh/mx*100)+'%"></i></div></div>').join('');}
setInterval(()=>{if(!paused&&tab==='budget')loadBudget();},2000);

poll();setInterval(()=>{if(!paused)poll();},1000);
</script></body></html>)HTML";
}

}  // namespace sm
