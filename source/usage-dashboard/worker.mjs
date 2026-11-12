// SPDX-License-Identifier: AGPL-3.0-only
import assets from './assets.mjs';
const DAY = 86400000;
const common = {
  'Cache-Control': 'private, no-store',
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'X-Frame-Options': 'DENY',
  'Permissions-Policy': 'camera=(), microphone=(), geolocation=()',
  'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
};
const json = (value, status = 200) => new Response(JSON.stringify(value), {status, headers: {...common, 'Content-Type': 'application/json; charset=utf-8'}});
const date = value => typeof value === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(value) && Number.isFinite(Date.parse(value+'T00:00:00Z')) && new Date(value+'T00:00:00Z').toISOString().slice(0,10) === value;
const iso = epoch => new Date(epoch).toISOString().slice(0,10);
export function parameters(url, now = new Date()) {
  const p=url.searchParams, latest=iso(now.getTime()+DAY), earliest=iso(now.getTime()-89*DAY);
  const from=p.get('from') || iso(now.getTime()-29*DAY), to=p.get('to') || iso(now.getTime());
  if(!date(from)||!date(to)||from>to||from<earliest||to>latest||Date.parse(to)-Date.parse(from)>89*DAY) throw new Error('Choose a valid date range within the last 90 days.');
  const app=p.get('app')||'all';
  if(!['all','new_3ds','original_3ds'].includes(app))throw new Error('Invalid app type.');
  const search=(p.get('search')||'').trim().toLowerCase();
  if(search.length>36 || !/^[0-9a-f-]*$/.test(search)) throw new Error('Search using an installation ID or part of one.');
  const page=Number(p.get('page')||1), size=Number(p.get('size')||25);
  if(!Number.isSafeInteger(page)||page<1||page>100000||![10,25,50,100].includes(size))throw new Error('Invalid page.');
  const sort=p.get('sort')||'time', orders={time:'seconds DESC, installation_id, app',sessions:'sessions DESC, installation_id, app',recent:'last_seen DESC, installation_id, app'};
  if(!Object.hasOwn(orders,sort))throw new Error('Invalid sort option.');
  return {from,to,app,search,page,size,sort,order:orders[sort],earliest,latest};
}
function statement(env,sql,...values){return env.DB.prepare(sql).bind(...values);}
const dailySource=`(SELECT 'new_3ds' AS app,day,sessions,seconds FROM daily_totals UNION ALL SELECT 'original_3ds' AS app,day,sessions,seconds FROM relay_daily_totals)`;
const installationSource=`(SELECT 'new_3ds' AS app,day,installation_id,sessions,seconds FROM installation_daily UNION ALL SELECT 'original_3ds' AS app,day,installation_id,sessions,seconds FROM relay_installation_daily)`;
const filter='day BETWEEN ? AND ? AND (?=\'all\' OR app=?)';
const values=p=>[p.from,p.to,p.app,p.app];
const appLabel=app=>app==='original_3ds'?'Original 3DS Relay':'New 3DS';
async function overview(env,p,now){
  const responses=await env.DB.batch([
    statement(env,`SELECT COALESCE(SUM(sessions),0) AS sessions,COALESCE(SUM(seconds),0) AS seconds FROM ${dailySource} WHERE ${filter}`,...values(p)),
    statement(env,`SELECT COUNT(DISTINCT app||':'||installation_id) AS installations,COALESCE(SUM(seconds),0) AS linked_seconds FROM ${installationSource} WHERE ${filter}`,...values(p)),
    statement(env,`SELECT day,SUM(sessions) AS sessions,SUM(seconds) AS seconds FROM ${dailySource} WHERE ${filter} GROUP BY day ORDER BY day`,...values(p)),
  ]);
  const totals=responses[0].results[0],ids=responses[1].results[0];
  return {app:p.app,range:{from:p.from,to:p.to,earliest:p.earliest,latest:p.latest},updated_at:now.toISOString(),summary:{sessions:totals.sessions,seconds:totals.seconds,installations:ids.installations,average_installation_seconds:ids.installations?ids.linked_seconds/ids.installations:0},daily:responses[2].results};
}
const grouped=`SELECT app,installation_id,SUM(sessions) AS sessions,SUM(seconds) AS seconds,MIN(day) AS first_seen,MAX(day) AS last_seen FROM ${installationSource} WHERE ${filter} AND installation_id LIKE ? GROUP BY app,installation_id`;
async function installations(env,p){
  const pattern='%'+p.search+'%';
  const responses=await env.DB.batch([
    statement(env,`SELECT COUNT(*) AS total FROM (${grouped})`,...values(p),pattern),
    statement(env,`${grouped} ORDER BY ${p.order} LIMIT ? OFFSET ?`,...values(p),pattern,p.size,(p.page-1)*p.size),
  ]);
  return {rows:responses[1].results,total:responses[0].results[0].total,page:p.page,page_size:p.size};
}
async function details(env,p,id){
  if(!/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(id))return json({error:'Invalid installation ID.'},400);
  const result=await statement(env,`SELECT app,day,sessions,seconds FROM ${installationSource} WHERE ${filter} AND installation_id=? ORDER BY day DESC,app`,...values(p),id).all();
  return json({app:p.app,installation_id:id,rows:result.results});
}
function csvCell(value){return '"'+String(value??'').replaceAll('"','""')+'"';}
async function csv(env,p){
  const result=await statement(env,`${grouped} ORDER BY ${p.order} LIMIT 5001`,...values(p),'%'+p.search+'%').all();
  if(result.results.length>5000)return json({error:'More than 5,000 installations match. Narrow the date range or ID search before exporting.'},422);
  const rows=[['app','unique_id','sessions','minutes_played','hours_played','first_activity','last_activity'],...result.results.map(r=>[appLabel(r.app),r.installation_id,r.sessions,(r.seconds/60).toFixed(1),(r.seconds/3600).toFixed(2),r.first_seen,r.last_seen])];
  return new Response('\ufeff'+rows.map(row=>row.map(csvCell).join(',')).join('\r\n')+'\r\n',{headers:{...common,'Content-Type':'text/csv; charset=utf-8','Content-Disposition':`attachment; filename="3dsrp-usage-${p.app}-${p.from}-to-${p.to}.csv"`}});
}
export async function handle(request,env,ctx,now=new Date()){
  // Trust only the runtime's validated Access identity, never an HTTP email header.
  let identity;
  try{identity=ctx?.access && await ctx.access.getIdentity();}catch{return json({error:'Sign-in could not be verified. Please reload.'},403);}
  if(!identity?.email || identity.email.toLowerCase()!==env.ALLOWED_EMAIL?.toLowerCase()){
    if(new URL(request.url).pathname.startsWith('/api/'))return json({error:'Sign in with the authorized Cloudflare account to view this dashboard.'},403);
    return new Response('<!doctype html><html lang="en"><meta name="viewport" content="width=device-width,initial-scale=1"><title>3DSRP · Sign-in required</title><body><h1>Sign-in required</h1><p>This dashboard is private. Cloudflare Access must authorize your account before usage data can be shown.</p></body></html>',{status:403,headers:{...common,'Content-Type':'text/html; charset=utf-8'}});
  }
  if(request.method!=='GET'&&request.method!=='HEAD')return json({error:'Read-only dashboard.'},405);
  const url=new URL(request.url);
  try{
    if(url.pathname==='/api/overview')return json(await overview(env,parameters(url,now),now));
    if(url.pathname==='/api/installations')return json(await installations(env,parameters(url,now)));
    if(url.pathname==='/api/export.csv')return await csv(env,parameters(url,now));
    if(url.pathname.startsWith('/api/installation/'))return await details(env,parameters(url,now),decodeURIComponent(url.pathname.slice('/api/installation/'.length)));
    if(url.pathname.startsWith('/api/'))return json({error:'Not found.'},404);
    const asset=assets[url.pathname === '/' ? '/index.html' : url.pathname];
    if(!asset)return json({error:'Not found.'},404);
    return new Response(request.method==='HEAD'?null:asset.body,{headers:{...common,'Content-Type':asset.type}});
  }catch(error){
    if(error instanceof Error && ['Choose a valid date range within the last 90 days.','Search using an installation ID or part of one.','Invalid page.','Invalid sort option.','Invalid app type.'].includes(error.message))return json({error:error.message},400);
    return json({error:'Statistics are temporarily unavailable. Please try again.'},503);
  }
}
export default{fetch:handle};
