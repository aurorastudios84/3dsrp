import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {readFileSync} from 'node:fs';
import {handle,parameters} from '../worker.mjs';
const now=new Date('2026-10-23T12:00:00Z'), id='00000000-0000-4000-8000-000000000001',other='00000000-0000-4000-8000-000000000002';
const ctx={access:{getIdentity:async()=>({email:'maintainer@example.invalid'})}};
function environment(){const db=new DatabaseSync(':memory:');db.exec(readFileSync(new URL('../../usage-service/schema.sql',import.meta.url),'utf8'));db.prepare('INSERT INTO installation_daily VALUES (?,?,?,?)').run('2026-10-22',id,1,7200);db.prepare('INSERT INTO installation_daily VALUES (?,?,?,?)').run('2026-10-23',id,0,900);db.prepare('INSERT INTO installation_daily VALUES (?,?,?,?)').run('2026-10-23',other,1,900);db.exec("INSERT INTO daily_totals VALUES ('2026-10-22',1,7200),('2026-10-23',1,1800)");return {
  _sql:db,
  ALLOWED_EMAIL: 'maintainer@example.invalid',
  DB: {
    prepare(sql) {
      assert.match(sql, /^SELECT /);
      return {bind(...values) {
        return {all: async () => ({results: db.prepare(sql).all(...values)})};
      }};
    },
    batch: async statements => Promise.all(statements.map(s => s.all()))
  }
};}

const request=(path,options)=>new Request('https://example.test'+path,options);
test('private routes reject missing identity and forged email headers without querying D1',async()=>{const env={ALLOWED_EMAIL:'maintainer@example.invalid',DB:{prepare(){throw Error('must not access');}}};for(const path of ['/','/app.js','/api/overview','/api/export.csv']){const r=await handle(request(path,{headers:{'Cf-Access-Authenticated-User-Email':env.ALLOWED_EMAIL}}),env,{},now);assert.equal(r.status,403);assert.equal(r.headers.get('Cache-Control'),'private, no-store');}});
test('other accounts and identity lookup failure are denied',async()=>{for(const access of [{getIdentity:async()=>({email:'other@example.com'})},{getIdentity:async()=>{throw Error('fail');}}])assert.equal((await handle(request('/api/overview'),environment(),{access},now)).status,403);});
test('summaries count distinct installations, sum seconds, and preserve original activity dates',async()=>{const response=await handle(request('/api/overview'),environment(),ctx,now);assert.equal(response.status,200);const data=await response.json();assert.deepEqual(data.summary,{sessions:2,seconds:9000,installations:2,average_installation_seconds:4500});assert.deepEqual(data.daily.map(d=>d.sessions),[1,1]);assert.equal(data.daily[0].day,'2026-10-22');});
test('grouped installations and UUID daily breakdown include midnight continuation',async()=>{const env=environment();const data=await(await handle(request('/api/installations'),env,ctx,now)).json();assert.equal(data.total,2);assert.equal(data.rows[0].seconds,8100);const detail=await(await handle(request('/api/installation/'+id),env,ctx,now)).json();assert.equal(detail.rows[0].sessions,0);assert.equal(detail.rows[0].day,'2026-10-23');});
test('search, date filters, pagination, sort and CSV agree',async()=>{const env=environment(),p='?from=2026-10-23&to=2026-10-23&search=000000000002&sort=recent';const data=await(await handle(request('/api/installations'+p),env,ctx,now)).json();assert.equal(data.total,1);assert.equal(data.rows[0].installation_id,other);assert.equal(data.rows[0].seconds,900);const csv=await handle(request('/api/export.csv'+p),env,ctx,now);assert.equal(csv.status,200);assert.match(await csv.text(),/"15.0","0.25"/);assert.match(csv.headers.get('Content-Disposition'),/2026-10-23/);const empty=await(await handle(request('/api/installations?page=2'),env,ctx,now)).json();assert.deepEqual(empty.rows,[]);});
test('invalid dates and SQL injection attempts are rejected',async()=>{for(const value of ['from=2026-02-30','from=2025-01-01','from=2026-10-24&to=2026-10-22','search=%25','sort=seconds%3BDROP+TABLE+daily_totals','page=-1','size=999','to=2026-10-25']){assert.throws(()=>parameters(new URL('https://example.test/?'+value),now));assert.equal((await handle(request('/api/overview?'+value),environment(),ctx,now)).status,400);}});
test('dashboard is read-only, static files have CSP, DB failures expose no details',async()=>{assert.equal((await handle(request('/api/overview',{method:'POST'}),environment(),ctx,now)).status,405);const asset=await handle(request('/app.js'),environment(),ctx,now);assert.match(asset.headers.get('Content-Security-Policy'),/script-src 'self'/);const env=environment();env.DB.batch=async()=>{throw Error('secret database detail');};const fail=await handle(request('/api/overview'),env,ctx,now);assert.equal(fail.status,503);assert.doesNotMatch(await fail.text(),/secret/);});

test('app filter splits same UUID into distinct app installations, tables, details and CSV',async()=>{
 const env=environment();env._sql.prepare('INSERT INTO relay_installation_daily VALUES(?,?,?,?)').run('2026-10-23',id,2,300);env._sql.exec("INSERT INTO relay_daily_totals VALUES('2026-10-23',2,300)");
 const original=await(await handle(request('/api/overview?app=original_3ds'),env,ctx,now)).json();assert.deepEqual(original.summary,{sessions:2,seconds:300,installations:1,average_installation_seconds:300});
 const native=await(await handle(request('/api/overview?app=new_3ds'),env,ctx,now)).json();assert.equal(native.summary.seconds,9000);assert.equal(native.summary.installations,2);
 const all=await(await handle(request('/api/overview'),env,ctx,now)).json();assert.equal(all.summary.installations,3);assert.equal(all.summary.sessions,4);assert.equal(all.summary.seconds,9300);
 const rows=await(await handle(request('/api/installations'),env,ctx,now)).json();assert.equal(rows.rows.filter(x=>x.installation_id===id).length,2);
 const details=await(await handle(request('/api/installation/'+id+'?app=original_3ds'),env,ctx,now)).json();assert.equal(details.rows.length,1);assert.equal(details.rows[0].seconds,300);assert.equal(details.rows[0].app,'original_3ds');
 const csv=await(await handle(request('/api/export.csv?app=original_3ds'),env,ctx,now)).text();assert.match(csv,/"app","unique_id"/);assert.match(csv,/Original 3DS Relay/);assert.doesNotMatch(csv,/New 3DS/);
});
test('unknown app selectors are rejected before database access',async()=>{for(const app of ['android',"original_3ds';DROP TABLE daily_totals",'__proto__'])assert.equal((await handle(request('/api/overview?app='+encodeURIComponent(app)),environment(),ctx,now)).status,400);});
