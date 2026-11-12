import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import worker, { record, prune } from '../worker.mjs';
const id='01234567-89ab-4def-8123-456789abcdef', now=new Date('2026-10-23T23:59:59Z');
function database(failSecond=false) {
 const sql=new DatabaseSync(':memory:');sql.exec(readFileSync(new URL('../schema.sql',import.meta.url),'utf8'));
 return {sql,env:{DB:{
  prepare(q) {return {bind(...v) {return {q,v,async run(){if(failSecond)throw Error("database outage");return sql.prepare(q).run(...v);}};}};},
  async batch(statements) {sql.exec('BEGIN');try {const results=statements.map((s,i)=>{if(failSecond && i===1)throw Error('failure');return sql.prepare(s.q).run(...s.v);});sql.exec('COMMIT');return results;}catch(e){sql.exec('ROLLBACK');throw e;}}
 }}};
}
function req(body={installation_id:id,seconds:65},headers={}) {return new Request('https://test/v1/session',{method:'POST',headers:{'Content-Type':'application/json',...headers},body:JSON.stringify(body)});}
test('aggregate seconds/session counts and distinct installations; no metadata retained',async()=>{
 const {sql,env}=database();
 await record(req(undefined,{'CF-Connecting-IP':'192.0.2.1','User-Agent':'private'}),env,now);
 await record(req(),env,now);await record(req({installation_id:'11234567-89ab-4def-8123-456789abcdef',seconds:10}),env,now);
 assert.deepEqual({...sql.prepare('SELECT * FROM daily_totals').get()},{day:'2026-10-23',sessions:3,seconds:140});
 assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM installation_daily').get().n,2);
 assert.deepEqual(sql.prepare('PRAGMA table_info(installation_daily)').all().map(x=>x.name),['day','installation_id','sessions','seconds']);
});
test('100 concurrent increments are atomic',async()=>{
 const {sql,env}=database();assert((await Promise.all(Array.from({length:100},()=>record(req(),env,now)))).every(r=>r.status===204));
 assert.equal(sql.prepare('SELECT seconds FROM daily_totals').get().seconds,6500);assert.equal(sql.prepare('SELECT sessions FROM installation_daily').get().sessions,100);
});
for(const body of [null,[],{}, {installation_id:id,seconds:-1},{installation_id:id,seconds:1.5},{installation_id:id,seconds:604801},{installation_id:id,seconds:'5'}, {installation_id:'not-uuid',seconds:5},{installation_id:id,seconds:5,ip:'x'}]) {
 test(`invalid payload ${JSON.stringify(body)}`,async()=>{const {sql,env}=database();assert.equal((await record(req(body),env,now)).status,400);assert.equal(sql.prepare('SELECT count(*) AS n FROM daily_totals').get().n,0);});
}
test('malformed/oversized/media/method/query restrictions',async()=>{
 const {env}=database();
 for(const body of ['{',' '.repeat(193)]) assert.equal((await record(new Request('https://test/v1/session',{method:'POST',headers:{'Content-Type':'application/json'},body}),env)).status,400);
 assert.equal((await record(req(undefined,{'Content-Type':'text/plain'}),env)).status,415);
 assert.equal((await record(new Request('https://test/v1/session'),env)).status,405);
 assert.equal((await record(new Request('https://test/v1/session?x=1'),env)).status,404);
});
test('database outage rolls back both aggregates; empty generic response',async()=>{
 const {sql,env}=database(true);const r=await record(req(),env,now);assert.equal(r.status,503);assert.equal(await r.text(),'');assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM installation_daily').get().n,0);
});
test('different retention for identifier data and overall totals',async()=>{
 const {sql,env}=database();
 for(const day of ['2025-10-22','2025-10-23','2026-07-24','2026-07-25','2026-10-23']) {sql.prepare('INSERT INTO installation_daily VALUES(?,?,1,60)').run(day,id);sql.prepare('INSERT INTO daily_totals VALUES(?,1,60)').run(day);}
 await prune(env,now);
 assert.deepEqual(sql.prepare('SELECT day FROM installation_daily ORDER BY day').all().map(x=>x.day),['2026-07-25','2026-10-23']);
 assert.deepEqual(sql.prepare('SELECT day FROM daily_totals ORDER BY day').all().map(x=>x.day),['2025-10-23','2026-07-24','2026-07-25','2026-10-23']);
});
test('health exposes no statistics; provider context is not a clock',async()=>{
 const {env}=database();assert.equal((await record(new Request('https://test/health'),env)).status,204);assert.equal((await record(new Request('https://test/stats'),env)).status,404);assert.equal((await worker.fetch(req(),env,{waitUntil(){}})).status,204);
});

const session='21234567-89ab-4def-8123-456789abcdef';
function recovery(body={installation_id:id,session_id:session,day:'2026-10-23',seconds:7200}) {
 return new Request('https://test/v2/session',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
}
test('recovery uploaded tomorrow maps two hours to yesterday, not receipt date',async()=>{
 const {sql,env}=database();assert.equal((await record(recovery(),env,new Date('2026-10-24T12:00:00Z'))).status,204);
 assert.deepEqual({...sql.prepare('SELECT * FROM daily_totals').get()},{day:'2026-10-23',sessions:1,seconds:7200});
});
test('repeated/ambiguous delivery, cumulative checkpoints and stale replay cannot double count',async()=>{
 const {sql,env}=database();
 for(const seconds of [7200,7200,10800,3600,10800])assert.equal((await record(recovery({installation_id:id,session_id:session,day:'2026-10-23',seconds}),env,now)).status,204);
 assert.deepEqual({...sql.prepare('SELECT * FROM daily_totals').get()},{day:'2026-10-23',sessions:1,seconds:10800});
 assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM session_day_receipts').get().n,1);
});
test('100 concurrent identical reports count once',async()=>{
 const {sql,env}=database();assert((await Promise.all(Array.from({length:100},()=>record(recovery(),env,now)))).every(r=>r.status===204));
 assert.deepEqual({...sql.prepare('SELECT * FROM daily_totals').get()},{day:'2026-10-23',sessions:1,seconds:7200});
});
test('cross-midnight segments retain their respective activity dates',async()=>{
 const {sql,env}=database();for(const [day,seconds] of [['2026-10-23',600],['2026-10-24',900]])await record(recovery({installation_id:id,session_id:session,day,seconds}),env,new Date('2026-10-25T12:00:00Z'));
 assert.deepEqual(sql.prepare('SELECT day,seconds FROM daily_totals ORDER BY day').all().map(x=>({...x})),[{day:'2026-10-23',seconds:600},{day:'2026-10-24',seconds:900}]);
});
for(const [name,patch,status] of [['invalid date',{day:'2026-02-31'},400],['future date',{day:'2026-10-25'},400],['expired',{day:'2026-01-01'},410],['invalid session',{session_id:'invalid'},400],['too long',{seconds:86401},400],['extra field',{ip:'192.0.2.1'},400]]) {
 test(`recovery rejects ${name}`,async()=>{const {sql,env}=database();assert.equal((await record(recovery({installation_id:id,session_id:session,day:'2026-10-23',seconds:1,...patch}),env,now)).status,status);assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM session_day_receipts').get().n,0);});
}
test('trigger failure rolls back receipt and all aggregate changes',async()=>{
 const {sql,env}=database();sql.exec('DROP TABLE daily_totals');assert.equal((await record(recovery(),env,now)).status,503);
 assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM session_day_receipts').get().n,0);assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM installation_daily').get().n,0);
});
test('dedup receipts expire with identifier-linked activity',async()=>{
 const {sql,env}=database();await record(recovery(),env,now);await prune(env,new Date('2027-02-01T00:00:00Z'));
 assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM session_day_receipts').get().n,0);assert.equal(sql.prepare('SELECT COUNT(*) AS n FROM installation_daily').get().n,0);
 assert.equal(sql.prepare('SELECT seconds FROM daily_totals').get().seconds,7200);
 assert.equal((await record(recovery(),env,new Date('2027-02-01T00:00:00Z'))).status,410);
});

test('one cross-midnight session counts once, including older segment arriving last',async()=>{
 const {sql,env}=database();
 for(const [day,seconds] of [['2026-10-24',900],['2026-10-23',600],['2026-10-24',1200]])await record(recovery({installation_id:id,session_id:session,day,seconds}),env,new Date('2026-10-25T12:00:00Z'));
 assert.deepEqual(sql.prepare('SELECT day,sessions,seconds FROM daily_totals ORDER BY day').all().map(x=>({...x})),[{day:'2026-10-23',sessions:1,seconds:600},{day:'2026-10-24',sessions:0,seconds:1200}]);
 assert.deepEqual({...sql.prepare('SELECT SUM(sessions) AS sessions,SUM(seconds) AS seconds FROM installation_daily').get()},{sessions:1,seconds:1800});
});

const v3=(app='original_3ds',extra={})=>new Request('https://test/v3/session',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({app,installation_id:id,session_id:session,day:'2026-10-23',seconds:60,...extra})});
test('typed reports separate app histories and retain idempotent maximum durations',async()=>{
 const {sql,env}=database();
 assert.equal((await record(v3('new_3ds'),env,now)).status,204);
 assert.equal((await record(v3(),env,now)).status,204);
 assert.equal((await record(v3(),env,now)).status,204);
 assert.equal((await record(v3('original_3ds',{seconds:90}),env,now)).status,204);
 assert.equal((await record(v3('original_3ds',{seconds:30}),env,now)).status,204);
 assert.deepEqual({...sql.prepare('SELECT sessions,seconds FROM daily_totals').get()},{sessions:1,seconds:60});
 assert.deepEqual({...sql.prepare('SELECT sessions,seconds FROM relay_daily_totals').get()},{sessions:1,seconds:90});
 assert.equal(sql.prepare('SELECT count(*) AS n FROM relay_session_day_receipts').get().n,1);
});
test('original relay midnight recovery corrects the earliest start day without duplicating sessions',async()=>{
 const {sql,env}=database();await record(v3('original_3ds',{day:'2026-10-23',seconds:120}),env,now);await record(v3('original_3ds',{day:'2026-10-22',seconds:30}),env,now);
 assert.deepEqual(sql.prepare('SELECT day,sessions,seconds FROM relay_daily_totals ORDER BY day').all().map(x=>({...x})),[{day:'2026-10-22',sessions:1,seconds:30},{day:'2026-10-23',sessions:0,seconds:120}]);
 assert.equal(sql.prepare('SELECT count(*) AS n FROM daily_totals').get().n,0);
});
test('typed reports reject unknown apps and retain exact payload/date bounds',async()=>{
 for(const app of ['',null,'android','__proto__',"original_3ds';DROP TABLE daily_totals;"]){const {env}=database();assert.equal((await record(v3(app),env,now)).status,400);}
 const {env}=database();assert.equal((await record(v3('original_3ds',{ip:'private'}),env,now)).status,400);assert.equal((await record(v3('original_3ds',{day:'2026-01-01'}),env,now)).status,410);
});
test('relay retention matches existing linked/anonymous retention',async()=>{
 const {sql,env}=database();for(const day of ['2025-10-22','2026-07-24','2026-07-25']){sql.prepare('INSERT INTO relay_installation_daily VALUES(?,?,1,10)').run(day,id);sql.prepare('INSERT INTO relay_daily_totals VALUES(?,1,10)').run(day);}
 await prune(env,now);assert.deepEqual(sql.prepare('SELECT day FROM relay_installation_daily').all().map(x=>x.day),['2026-07-25']);assert.deepEqual(sql.prepare('SELECT day FROM relay_daily_totals ORDER BY day').all().map(x=>x.day),['2026-07-24','2026-07-25']);
});
test('additive relay migration is repeatable and preserves legacy New 3DS history',()=>{
 const db=new DatabaseSync(':memory:');db.exec(readFileSync(new URL('../schema.sql',import.meta.url),'utf8').split('-- Original 3DS relay app tables')[0]);
 db.prepare('INSERT INTO installation_daily VALUES(?,?,?,?)').run('2026-10-23',id,7,12345);db.exec("INSERT INTO daily_totals VALUES('2026-10-23',7,12345)");
 const migration=readFileSync(new URL('../schema-relay.sql',import.meta.url),'utf8');db.exec(migration);db.exec(migration);
 assert.deepEqual({...db.prepare('SELECT sessions,seconds FROM daily_totals').get()},{sessions:7,seconds:12345});assert.equal(db.prepare('SELECT count(*) AS n FROM relay_daily_totals').get().n,0);
});
