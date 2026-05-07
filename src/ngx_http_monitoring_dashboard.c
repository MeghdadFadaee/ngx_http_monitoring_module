#include "ngx_http_monitoring.h"

static const u_char ngx_http_monitoring_dashboard_html[] =
"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Nginx Monitor</title><link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='14' fill='%230b0f14'/%3E%3Ccircle cx='32' cy='32' r='24' fill='%23111821' stroke='%23263344' stroke-width='3'/%3E%3Cpath d='M14 36h9l5-18 8 30 5-16h9' fill='none' stroke='%2345d4d0' stroke-width='5' stroke-linecap='round' stroke-linejoin='round'/%3E%3Cpath d='M17 20h30' fill='none' stroke='%2345d483' stroke-width='4' stroke-linecap='round'/%3E%3C/svg%3E\">"
"<style>"
":root{color-scheme:dark;--bg:#0b0f14;--panel:#111821;--panel2:#151f2b;--line:#263344;--text:#e6edf3;--muted:#8fa1b3;--green:#45d483;--red:#ff5f6d;--yellow:#f4c95d;--blue:#5aa9ff;--cyan:#45d4d0}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}"
"header{height:58px;display:flex;align-items:center;justify-content:space-between;padding:0 20px;border-bottom:1px solid var(--line);background:#0f151d;position:sticky;top:0;z-index:5}"
".brand{font-size:17px;font-weight:700}.sub{color:var(--muted);font-size:12px}.status{display:flex;align-items:center;gap:8px;color:var(--muted)}.dot{width:9px;height:9px;border-radius:50%;background:var(--red);box-shadow:0 0 0 3px rgba(255,95,109,.12)}.dot.ok{background:var(--green);box-shadow:0 0 0 3px rgba(69,212,131,.13)}"
".layout{display:grid;grid-template-columns:190px 1fr;min-height:calc(100vh - 58px)}nav{border-right:1px solid var(--line);padding:14px;background:#0d131a;position:sticky;top:58px;height:calc(100vh - 58px)}"
"button.tab{width:100%;height:36px;border:0;background:transparent;color:var(--muted);text-align:left;padding:0 12px;border-radius:7px;cursor:pointer;font-weight:600}button.tab.active,button.tab:hover{background:var(--panel2);color:var(--text)}"
"main{padding:18px;max-width:1500px;width:100%;margin:0 auto}.grid{display:grid;gap:12px}.cards{grid-template-columns:repeat(6,minmax(120px,1fr))}.card,.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px}.card{padding:12px;min-height:86px}.label{color:var(--muted);font-size:12px}.value{font-size:24px;font-weight:750;margin-top:8px;letter-spacing:0}.small{font-size:12px;color:var(--muted)}"
".charts{grid-template-columns:repeat(2,minmax(280px,1fr));margin-top:12px}.panel{padding:14px;min-width:0}.panel h2{font-size:14px;margin:0 0 10px}.chart{width:100%;height:210px;display:block}.wide{grid-column:1/-1}"
".toolbar{display:flex;gap:8px;align-items:center;margin:0 0 10px}input,select{height:32px;background:#0d131a;border:1px solid var(--line);border-radius:7px;color:var(--text);padding:0 10px;min-width:0}input{width:260px;max-width:100%}"
"table{width:100%;border-collapse:collapse;font-size:13px}th,td{text-align:left;padding:8px 9px;border-bottom:1px solid var(--line);white-space:nowrap}th{color:var(--muted);font-weight:650;cursor:pointer}td.truncate{max-width:420px;overflow:hidden;text-overflow:ellipsis}.page{display:none}.page.active{display:block}.split{grid-template-columns:repeat(2,minmax(280px,1fr))}details{border-top:1px solid var(--line);padding-top:10px;margin-top:10px}summary{cursor:pointer;color:var(--muted)}"
"@media (max-width:980px){.layout{grid-template-columns:1fr}nav{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line);display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.cards{grid-template-columns:repeat(2,minmax(120px,1fr))}.charts,.split{grid-template-columns:1fr}header{padding:0 12px}main{padding:12px}td.truncate{max-width:220px}}"
"</style></head><body><header><div><div class=\"brand\">Nginx Monitor</div><div class=\"sub\" id=\"stamp\">waiting</div></div><div class=\"status\"><span class=\"dot\" id=\"dot\"></span><span id=\"conn\">offline</span></div></header>"
"<div class=\"layout\"><nav id=\"nav\"></nav><main>"
"<section class=\"page active\" data-page=\"Overview\"><div class=\"grid cards\" id=\"cards\"></div><div class=\"grid charts\"><div class=\"panel\"><h2>CPU</h2><canvas class=\"chart\" id=\"cpuChart\"></canvas></div><div class=\"panel\"><h2>Requests</h2><canvas class=\"chart\" id=\"rpsChart\"></canvas></div><div class=\"panel\"><h2>Network</h2><canvas class=\"chart\" id=\"netChart\"></canvas></div><div class=\"panel\"><h2>Latency</h2><canvas class=\"chart\" id=\"latChart\"></canvas></div></div></section>"
"<section class=\"page\" data-page=\"CPU\"><div class=\"grid split\"><div class=\"panel\"><h2>CPU Load</h2><canvas class=\"chart\" id=\"cpuChart2\"></canvas></div><div class=\"panel\"><h2>Load Average</h2><table id=\"loadTable\"></table></div></div></section>"
"<section class=\"page\" data-page=\"Memory\"><div class=\"grid split\"><div class=\"panel\"><h2>Memory</h2><canvas class=\"chart\" id=\"memChart\"></canvas></div><div class=\"panel\"><h2>Memory Stats</h2><table id=\"memTable\"></table></div></div></section>"
"<section class=\"page\" data-page=\"Network\"><div class=\"panel\"><div class=\"toolbar\"><input id=\"ifFilter\" placeholder=\"Filter\"><select id=\"ifSort\"><option value=\"rx_bytes\">RX</option><option value=\"tx_bytes\">TX</option><option value=\"name\">Name</option></select></div><table id=\"ifTable\"></table></div></section>"
"<section class=\"page\" data-page=\"Disk\"><div class=\"grid split\"><div class=\"panel\"><h2>Devices</h2><table id=\"diskTable\"></table></div><div class=\"panel\"><h2>Filesystems</h2><table id=\"fsTable\"></table></div></div></section>"
"<section class=\"page\" data-page=\"Requests\"><div class=\"panel\"><div class=\"toolbar\"><input id=\"urlFilter\" placeholder=\"Filter\"><select id=\"urlSort\"><option value=\"hits\">Hits</option><option value=\"errors\">Errors</option><option value=\"avg_latency\">Latency</option></select></div><table id=\"urlTable\"></table><details><summary>User agents</summary><table id=\"uaTable\"></table></details></div></section>"
"<section class=\"page\" data-page=\"Upstreams\"><div class=\"panel\"><table id=\"upTable\"></table></div></section>"
"<section class=\"page\" data-page=\"Workers\"><div class=\"panel\"><table id=\"workerTable\"></table></div></section>"
"<section class=\"page\" data-page=\"Errors\"><div class=\"grid split\"><div class=\"panel\"><h2>Status</h2><table id=\"errTable\"></table></div><div class=\"panel\"><h2>Error Rate</h2><canvas class=\"chart\" id=\"errChart\"></canvas></div></div></section>"
"</main></div><script>"
"const pages=['Overview','CPU','Memory','Network','Disk','Requests','Upstreams','Workers','Errors'];const S={data:null,hist:[],sort:{}};"
"const nav=document.getElementById('nav');pages.forEach(p=>{const b=document.createElement('button');b.className='tab'+(p==='Overview'?' active':'');b.textContent=p;b.onclick=()=>show(p);nav.appendChild(b)});"
"function show(p){document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b.textContent===p));document.querySelectorAll('.page').forEach(s=>s.classList.toggle('active',s.dataset.page===p));drawAll()}"
"function fmt(n,u=''){if(n==null)return '0'+u;if(Math.abs(n)>=1e12)return(n/1e12).toFixed(2)+'T'+u;if(Math.abs(n)>=1e9)return(n/1e9).toFixed(2)+'G'+u;if(Math.abs(n)>=1e6)return(n/1e6).toFixed(2)+'M'+u;if(Math.abs(n)>=1e3)return(n/1e3).toFixed(2)+'K'+u;return(Number(n).toFixed(Number(n)%1?2:0))+u}"
"function bytes(n){return fmt(n,'B')}function pct(n){return(Number(n)||0).toFixed(1)+'%'}function ms(n){return(Number(n)||0).toFixed(1)+'ms'}"
"function q(path){const u=new URL(path,location.origin);const t=new URLSearchParams(location.search).get('token');if(t)u.searchParams.set('token',t);return u.pathname+u.search}"
"async function poll(){try{const r=await fetch(q('/monitor/api'),{cache:'no-store'});if(!r.ok)throw Error(r.status);apply(await r.json(),true)}catch(e){setConn(false)}}"
"function start(){if(window.EventSource){const es=new EventSource(q('/monitor/live'));es.addEventListener('metrics',e=>apply(JSON.parse(e.data),true));es.onopen=()=>setConn(true);es.onerror=()=>setConn(false)}poll();setInterval(poll,5000)}"
"function setConn(ok){document.getElementById('dot').classList.toggle('ok',ok);document.getElementById('conn').textContent=ok?'live':'offline'}"
"function merge(a,b){for(const k in b){if(b[k]&&typeof b[k]==='object'&&!Array.isArray(b[k]))a[k]=merge(a[k]||{},b[k]);else a[k]=b[k]}return a}"
"function apply(d,ok){S.data=S.data&&!d.history?merge(S.data,d):d;if(d.history)S.hist=d.history;document.getElementById('stamp').textContent=new Date((d.timestamp||0)*1000).toLocaleString();setConn(ok);render()}"
"function card(k,v,s=''){return `<div class=\"card\"><div class=\"label\">${k}</div><div class=\"value\">${v}</div><div class=\"small\">${s}</div></div>`}"
"function render(){const d=S.data;if(!d)return;const sys=d.system||{},req=d.requests||{},ng=d.nginx||{},net=d.network||{},disk=d.disk||{};const mem=sys.memory||{},cpu=sys.cpu||{},lat=req.latency||{},st=req.status||{};"
"document.getElementById('cards').innerHTML=[card('CPU',pct(cpu.usage),`${cpu.cores||0} cores`),card('Memory',pct(mem.used_pct),bytes(mem.used||0)+' used'),card('RPS',fmt(req.requests_per_sec||ng.requests?.requests_per_sec||0),fmt(req.total||0)+' total'),card('Latency p95',ms(lat.p95),`p99 ${ms(lat.p99)}`),card('Network',bytes(net.rx_bytes||0)+' / '+bytes(net.tx_bytes||0),'rx / tx'),card('Errors',fmt((st['4xx']||0)+(st['5xx']||0)),`${fmt(st['5xx']||0)} 5xx`)].join('');"
"table('loadTable',[['Load 1m',cpu.load?.[0]||0],['Load 5m',cpu.load?.[1]||0],['Load 15m',cpu.load?.[2]||0],['Uptime',fmt(sys.uptime||0)+'s']]);"
"table('memTable',[['Total',bytes(mem.total||0)],['Available',bytes(mem.available||0)],['Free',bytes(mem.free||0)],['Swap',pct(sys.swap?.used_pct||0)]]);"
"rows('ifTable',net.interfaces||[],['name','rx_bytes','tx_bytes','rx_packets','tx_packets','up'],'ifFilter','ifSort');rows('diskTable',disk.devices||[],['name','reads','writes','read_bytes','write_bytes','io_ms']);rows('fsTable',disk.filesystems||[],['path','type','total','used','avail']);rows('urlTable',req.top_urls||[],['url','hits','errors','bytes','avg_latency'],'urlFilter','urlSort');rows('uaTable',req.user_agents||[],['user_agent','hits','errors','avg_latency']);rows('upTable',d.upstreams||[],['peer','requests','failures','status_4xx','status_5xx','avg_latency']);rows('workerTable',ng.workers||d.processes?.workers||[],['slot','pid','active','requests','bytes','errors','vm_rss','last_seen']);table('errTable',[['1xx',st['1xx']||0],['2xx',st['2xx']||0],['3xx',st['3xx']||0],['4xx',st['4xx']||0],['5xx',st['5xx']||0],['Error rate',((req.error_rate||0)*100).toFixed(3)+'%']]);drawAll()}"
"function table(id,pairs){document.getElementById(id).innerHTML='<tbody>'+pairs.map(r=>`<tr><th>${r[0]}</th><td>${r[1]}</td></tr>`).join('')+'</tbody>'}"
"function rows(id,data,cols,filterId,sortId){let a=[...data];const f=filterId&&document.getElementById(filterId)?.value.toLowerCase();if(f)a=a.filter(x=>JSON.stringify(x).toLowerCase().includes(f));const s=sortId&&document.getElementById(sortId)?.value;if(s)a.sort((x,y)=>typeof x[s]==='string'?String(x[s]).localeCompare(String(y[s])):(Number(y[s]||0)-Number(x[s]||0)));document.getElementById(id).innerHTML='<thead><tr>'+cols.map(c=>`<th>${c}</th>`).join('')+'</tr></thead><tbody>'+a.map(x=>'<tr>'+cols.map(c=>`<td class=\"${String(x[c]).length>36?'truncate':''}\">${val(c,x[c])}</td>`).join('')+'</tr>').join('')+'</tbody>'}"
"function val(k,v){if(k.includes('bytes')||['total','used','avail','free'].includes(k))return bytes(v||0);if(k.includes('latency'))return ms(v||0);return v==null?'':v}"
"['ifFilter','ifSort','urlFilter','urlSort'].forEach(id=>setTimeout(()=>{const e=document.getElementById(id);if(e)e.oninput=render},0));"
"function drawAll(){line('cpuChart',S.hist.map(x=>x.cpu),'#45d483',100);line('cpuChart2',S.hist.map(x=>x.cpu),'#45d483',100);line('rpsChart',S.hist.map(x=>x.rps),'#5aa9ff');line('netChart',S.hist.map(x=>(x.network_rx_per_sec+x.network_tx_per_sec)/1024),'#45d4d0');line('latChart',S.hist.map(x=>x.latency_p95),'#f4c95d');line('memChart',S.hist.map(x=>x.memory),'#ff5f6d',100);line('errChart',S.hist.map(x=>(x.status_4xx||0)+(x.status_5xx||0)),'#ff5f6d')}"
"function line(id,arr,color,max){const c=document.getElementById(id);if(!c)return;const r=c.getBoundingClientRect(),d=window.devicePixelRatio||1;c.width=r.width*d;c.height=r.height*d;const x=c.getContext('2d');x.scale(d,d);x.clearRect(0,0,r.width,r.height);x.strokeStyle='#263344';x.lineWidth=1;for(let i=0;i<4;i++){let y=10+i*(r.height-20)/3;x.beginPath();x.moveTo(0,y);x.lineTo(r.width,y);x.stroke()}if(!arr.length)return;const m=max||Math.max(...arr,1);x.strokeStyle=color;x.lineWidth=2;x.beginPath();arr.forEach((v,i)=>{let px=i*Math.max(1,r.width/(arr.length-1||1)),py=r.height-8-(Math.min(v,m)/m)*(r.height-18);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}"
"start();</script></body></html>";


ngx_int_t
ngx_http_monitoring_send_dashboard(ngx_http_request_t *r)
{
    ngx_buf_t       *b;
    ngx_chain_t      out;
    ngx_table_elt_t *h;

    ngx_http_discard_request_body(r);

    h = ngx_list_push(&r->headers_out.headers);
    if (h != NULL) {
        h->hash = 1;
        ngx_str_set(&h->key, "Cache-Control");
        ngx_str_set(&h->value, "no-store");
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h != NULL) {
        h->hash = 1;
        ngx_str_set(&h->key, "X-Frame-Options");
        ngx_str_set(&h->value, "DENY");
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h != NULL) {
        h->hash = 1;
        ngx_str_set(&h->key, "Content-Security-Policy");
        ngx_str_set(&h->value,
                    "default-src 'self'; script-src 'unsafe-inline' 'self'; "
                    "style-src 'unsafe-inline' 'self'; connect-src 'self'");
    }

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n =
        sizeof(ngx_http_monitoring_dashboard_html) - 1;
    ngx_str_set(&r->headers_out.content_type, "text/html");

    if (r->method == NGX_HTTP_HEAD) {
        r->header_only = 1;
        return ngx_http_send_header(r);
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = (u_char *) ngx_http_monitoring_dashboard_html;
    b->last = (u_char *) ngx_http_monitoring_dashboard_html
              + sizeof(ngx_http_monitoring_dashboard_html) - 1;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    ngx_http_send_header(r);
    return ngx_http_output_filter(r, &out);
}
