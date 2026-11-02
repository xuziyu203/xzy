/** Static report diagram generator. Never imports or runs the Qt application.
 * Requires Node.js, playwright and sharp for PNG/QA. --svg-only needs only Node.
 * Usage: node generate_flowchart.mjs [--outdir PATH] [--svg-only]
 * Optional: CHROME_PATH points to a Chromium/Chrome executable.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const req=createRequire(import.meta.url);
const argv=process.argv.slice(2);
const arg=argv.indexOf('--outdir');
const out=arg>=0?path.resolve(argv[arg+1]):path.dirname(fileURLToPath(import.meta.url));
await fs.mkdir(out,{recursive:true});
const WIDTH_MM=160, W=WIDTH_MM*72/25.4, H=384, FONT=10.4, SMALL=9.4, DPI=600;
const base='qt_flowchart_report_20261030';
const nodes=[
 ['start',185,4,83,18,['启动程序'],'round'],
 ['init',125,28,203,20,['初始化界面与通信资源']],
 ['bind',125,54,203,20,['绑定事件并启动定时器']],
 ['loop',155,80,143,20,['进入事件循环']],
 ['request',76,124,172,20,['处理连接或功能请求']],
 ['execute',281,124,156,20,['连接校验／执行命令']],
 ['key_state',76,164,172,20,['更新按键状态与高亮']],
 ['key_gate',76,194,172,40,['已连接且启用接管？'],'diamond'],
 ['key_send',281,204,156,20,['编码并发送控制命令']],
 ['update',76,259,172,20,['刷新状态与日志']],
 ['receive',281,259,156,20,['接收并解析回复']],
 ['close',76,299,172,20,['释放输入并请求停止']],
 ['worker_stop',281,299,156,20,['清理资源并退出']],
 ['cleanup',76,329,172,20,['确认线程退出并关闭界面']],
 ['end',119,359,86,18,['结束'],'round'],
];
const routes=[
 ['start_init',[[226.5,22],[226.5,28]]],
 ['init_bind',[[226.5,48],[226.5,54]]],
 ['bind_loop',[[226.5,74],[226.5,80]]],
 ['event_bus',[[155,90],[10,90],[10,309]],false,false],
 ['dispatch_request',[[10,134],[76,134]]],
 ['dispatch_key',[[10,174],[76,174]]],
 ['dispatch_update',[[10,269],[76,269]]],
 ['dispatch_close',[[10,309],[76,309]]],
 ['return_bus',[[445,287],[445,90],[298,90]]],
 ['request_return',[[162,144],[162,152],[445,152]],false,false],
 ['request_async',[[248,134],[281,134]],true],
 ['state_gate',[[162,184],[162,194]]],
 ['gate_yes',[[248,214],[264,214]],false,false],
 ['key_async',[[264,214],[281,214]],true],
 ['gate_no',[[162,234],[162,247]],false,false],
 ['key_handoff_return',[[264,214],[264,247]],false,false],
 ['key_return',[[162,247],[445,247]],false,false],
 ['received_queue',[[281,269],[248,269]],true],
 ['update_return',[[162,279],[162,287],[445,287]],false,false],
 ['close_async',[[248,309],[281,309]],true],
 ['close_check',[[162,319],[162,329]]],
 ['worker_finished',[[359,319],[359,339],[248,339]],true],
 ['cleanup_end',[[162,349],[162,359]]],
];
const labels=[
 [162,116,'界面线程',FONT,'middle'],
 [359,116,'串口工作线程',FONT,'middle'],
 [43,129,'连接／功能',SMALL,'middle'],
 [43,169,'键盘／键帽',SMALL,'middle'],
 [43,264,'通信反馈',SMALL,'middle'],
 [43,304,'关闭窗口',SMALL,'middle'],
 [260,208,'是',SMALL,'middle'],
 [174,242,'否',SMALL,'middle'],
];
const esc=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
function rich(s){return s.split(/([\x20-\x7E]+)/).filter(Boolean).map(t=>'<tspan font-family="'+(/^[\x20-\x7E]+$/.test(t)?'Times New Roman':'SimSun')+'">'+esc(t)+'</tspan>').join('');}
function textLine(x,y,s,size=FONT,anchor='middle',extra=''){return '<text x="'+x+'" y="'+y+'" font-size="'+size+'" text-anchor="'+anchor+'" '+extra+'>'+rich(s)+'</text>';}
let svg='<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="'+WIDTH_MM+'mm" height="'+(H*25.4/72).toFixed(4)+'mm" viewBox="0 0 '+W+' '+H+'" role="img" aria-labelledby="diagram-title diagram-desc">\n';
svg+='<title id="diagram-title">Qt 上位机交互与串口通信流程图</title><desc id="diagram-desc">依据 qt_panel_scifi_20261030.zip 静态源码绘制。界面处理事件并通过队列和输入信箱与串口线程交互；主线程不阻塞等待串口回复。</desc>\n';
svg+='<defs><marker id="arrow" viewBox="0 0 6 6" refX="5.6" refY="3" markerWidth="5.2" markerHeight="5.2" orient="auto" markerUnits="userSpaceOnUse"><path d="M0,0 L6,3 L0,6 Z" fill="#333333"/></marker></defs>\n';
svg+='<g id="background"><rect width="'+W+'" height="'+H+'" fill="white"/></g>\n';
svg+='<g id="connections" fill="none" stroke="#333333" stroke-width="0.72" stroke-linejoin="round">\n';
for(const [id,pts,dashed=false,arrow=true] of routes){svg+='<path id="'+id+'" data-route="'+esc(JSON.stringify(pts))+'" d="'+pts.map((p,i)=>(i?'L':'M')+p.join(',')).join(' ')+'"'+(dashed?' stroke-dasharray="3.5 2.3"':'')+(arrow?' marker-end="url(#arrow)"':'')+'/>\n';}
svg+='<circle cx="264" cy="214" r="1.1" fill="#333333" stroke="none"/></g><g id="nodes" fill="#171717" font-family="SimSun">\n';
for(const [id,x,y,w,h,lines,kind='rect'] of nodes){svg+='<g id="'+id+'" data-node="'+kind+'" data-box="'+[x,y,w,h].join(',')+'">';
 if(kind==='diamond')svg+='<polygon points="'+[[x+w/2,y],[x+w,y+h/2],[x+w/2,y+h],[x,y+h/2]].map(p=>p.join(',')).join(' ')+'" fill="#FBF5E5" stroke="#333333" stroke-width="0.8"/>';
 else svg+='<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'"'+(kind==='round'?' rx="7"':'')+' fill="#FBF5E5" stroke="#333333" stroke-width="0.8"/>';
 const lineh=12.8, mid=y+h/2;
 lines.forEach((s,i)=>svg+=textLine(x+w/2,mid+(i-(lines.length-1)/2)*lineh+3.4,s));
 svg+='</g>\n';
}
svg+='</g><g id="annotations" fill="#333333" font-family="SimSun">';
for(const a of labels)svg+=textLine(...a);

svg+='</g></svg>\n';
await fs.writeFile(path.join(out,base+'.svg'),svg,'utf8');
if(argv.includes('--svg-only')){console.log('SVG written to '+out);process.exit(0);}
const {chromium}=req('playwright');
const sharp=req('sharp');
const local=process.env.LOCALAPPDATA||'';
const candidates=[process.env.CHROME_PATH,chromium.executablePath(),path.join(local,'Google/Chrome/Bin/chrome.exe'),path.join(local,'Google/Chrome/Application/chrome.exe'),'C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].filter(Boolean);
let executablePath;
for(const p of candidates){if(await fs.access(p).then(()=>true).catch(()=>false)){executablePath=p;break;}}
if(!executablePath)throw new Error('No Chromium found. Set CHROME_PATH or run npx playwright install chromium.');
const browser=await chromium.launch({executablePath,headless:true,args:['--disable-gpu','--no-first-run']});
try{
 const page=await browser.newPage({viewport:{width:700,height:1000},deviceScaleFactor:DPI/96});
 await page.setContent('<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;background:white}svg{display:block}</style></head><body>'+svg.replace(/<\?xml[^>]+>/,'')+'</body></html>');
 await page.evaluate(()=>document.fonts.ready);
 const qa=await page.evaluate(()=>{
  const svg=document.querySelector('svg'), ns=svg.viewBox.baseVal, issues=[];
  const textBoxes=[];
  function box(el){const b=el.getBBox(),m=el.getCTM(),root=svg.getCTM().inverse().multiply(m),p=[[b.x,b.y],[b.x+b.width,b.y],[b.x,b.y+b.height],[b.x+b.width,b.y+b.height]].map(([x,y])=>new DOMPoint(x,y).matrixTransform(root));return {x:Math.min(...p.map(v=>v.x)),y:Math.min(...p.map(v=>v.y)),w:Math.max(...p.map(v=>v.x))-Math.min(...p.map(v=>v.x)),h:Math.max(...p.map(v=>v.y))-Math.min(...p.map(v=>v.y))};}
  for(const el of document.querySelectorAll('text')){const b=box(el);textBoxes.push({text:el.textContent,...b});if(b.x<0||b.y<0||b.x+b.w>ns.width||b.y+b.h>ns.height)issues.push({type:'canvas_overflow',text:el.textContent,b});}
  for(const n of document.querySelectorAll('[data-node]')){const [x,y,w,h]=n.dataset.box.split(',').map(Number);for(const t of n.querySelectorAll('text')){const b=box(t);let inside=b.x>x+2&&b.y>y+1&&b.x+b.w<x+w-2&&b.y+b.h<y+h-1;
   if(n.dataset.node==='diamond')inside=[[b.x,b.y],[b.x+b.w,b.y],[b.x,b.y+b.h],[b.x+b.w,b.y+b.h]].every(([a,c])=>Math.abs(a-x-w/2)/(w/2)+Math.abs(c-y-h/2)/(h/2)<.95);
   if(!inside)issues.push({type:'node_text_overflow',id:n.id,text:t.textContent,b});
  }}
  for(const p of document.querySelectorAll('[data-route]')){const pts=JSON.parse(p.dataset.route);for(let i=1;i<pts.length;i++){const [a,b]=[pts[i-1],pts[i]];for(const t of textBoxes){const pad=.7;let hit=false;if(a[0]===b[0])hit=a[0]>t.x-pad&&a[0]<t.x+t.w+pad&&Math.max(a[1],b[1])>t.y-pad&&Math.min(a[1],b[1])<t.y+t.h+pad;else if(a[1]===b[1])hit=a[1]>t.y-pad&&a[1]<t.y+t.h+pad&&Math.max(a[0],b[0])>t.x-pad&&Math.min(a[0],b[0])<t.x+t.w+pad;if(hit)issues.push({type:'line_text_intersection',route:p.id,text:t.text});}}}
  return {issues,node_count:document.querySelectorAll('[data-node]').length,text_count:textBoxes.length,fonts:{simsun:document.fonts.check('10px SimSun'),times_new_roman:document.fonts.check('10px "Times New Roman"')},text_boxes:textBoxes};
 });
 const client=await page.context().newCDPSession(page);
 await client.send('DOM.enable');await client.send('CSS.enable');
 const {root}=await client.send('DOM.getDocument');const {nodeIds}=await client.send('DOM.querySelectorAll',{nodeId:root.nodeId,selector:'text'});
 const platformFonts=new Map();
 for(const nodeId of nodeIds){const r=await client.send('CSS.getPlatformFontsForNode',{nodeId});for(const f of r.fonts)platformFonts.set(f.familyName,{family:f.familyName,postscript_name:f.postScriptName,custom:f.isCustomFont});}
 qa.actual_render_fonts=[...platformFonts.values()];
 const raw=await page.locator('svg').screenshot({omitBackground:false});
 const pixels={width:Math.round(WIDTH_MM*DPI/25.4),height:Math.round(H/72*DPI)};
 await sharp(raw).resize(pixels.width,pixels.height).flatten({background:'#ffffff'}).withMetadata({density:DPI}).png().toFile(path.join(out,base+'.png'));
 await sharp(raw).resize({width:1100}).flatten({background:'#ffffff'}).png().toFile(path.join(out,'preview.png'));
 qa.canvas={width_mm:WIDTH_MM,height_mm:H*25.4/72,node_font_pt:FONT,label_font_pt:SMALL,png_dpi:DPI,...pixels};
 qa.note='Static SVG only. Qt application was not imported or executed. Automated layout checks complement manual visual review.';
 await fs.writeFile(path.join(out,'render_check.json'),JSON.stringify(qa,null,2),'utf8');
 console.log(JSON.stringify({output:out,issues:qa.issues,fonts:qa.actual_render_fonts,canvas:qa.canvas},null,2));
 if(qa.issues.length)process.exitCode=2;
}finally{await browser.close();}
