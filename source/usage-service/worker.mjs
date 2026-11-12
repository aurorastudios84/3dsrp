// SPDX-License-Identifier: AGPL-3.0-only
const MAX_BODY = 192;
const headers = { 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' };
const uuid = (value) => typeof value === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(value);
const date = (value) => typeof value === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(value) && Number.isFinite(Date.parse(value+'T00:00:00Z')) && new Date(value+'T00:00:00Z').toISOString().slice(0,10) === value;
const reply = (status) => new Response(null, { status, headers });

async function boundedBody(request) {
  if (!request.body) throw new Error('missing body');
  const declared = request.headers.get('content-length');
  if (declared && (!/^\d+$/.test(declared) || Number(declared) > MAX_BODY)) throw new Error('size');
  const reader = request.body.getReader();
  const chunks = []; let size = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > MAX_BODY) throw new Error('size');
      chunks.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  const bytes = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
}

export async function record(request, env, now = new Date()) {
  const url = new URL(request.url);
  if (url.pathname === '/health' && request.method === 'GET') return reply(204);
  const typed = url.pathname === '/v3/session';
  const recovery = typed || url.pathname === '/v2/session';
  if ((!recovery && url.pathname !== '/v1/session') || url.search) return reply(404);
  if (request.method !== 'POST') return reply(405);
  if (request.headers.get('content-type')?.split(';')[0].trim().toLowerCase() !== 'application/json') return reply(415);
  let body;
  try { body = JSON.parse(await boundedBody(request)); } catch { return reply(400); }
  if (!body || Array.isArray(body) || typeof body !== 'object') return reply(400);
  if (recovery) {
    if (Object.keys(body).sort().join(',') !== (typed?'app,day,installation_id,seconds,session_id':'day,installation_id,seconds,session_id') ||
        (typed && !['new_3ds','original_3ds'].includes(body.app)) ||
        !uuid(body.installation_id) || !uuid(body.session_id) || !date(body.day) ||
        !Number.isInteger(body.seconds) || body.seconds < 0 || body.seconds > 86400) return reply(400);
    const minimum = new Date(now.getTime()-90*86400000).toISOString().slice(0,10);
    const maximum = new Date(now.getTime()+86400000).toISOString().slice(0,10);
    if (body.day < minimum) return reply(410);
    if (body.day > maximum) return reply(400);
    const table=typed&&body.app==='original_3ds'?'relay_session_day_receipts':'session_day_receipts';
    try {
      await env.DB.prepare(`INSERT INTO ${table}(day, installation_id, session_id, seconds) VALUES (?, ?, ?, ?)
        ON CONFLICT(day, installation_id, session_id) DO UPDATE SET seconds=MAX(${table}.seconds,excluded.seconds)`)
        .bind(body.day, body.installation_id, body.session_id, body.seconds).run();
      return reply(204);
    } catch { return reply(503); }
  }
  if (Object.keys(body).sort().join(',') !== 'installation_id,seconds' ||
      !uuid(body.installation_id) || !Number.isInteger(body.seconds) || body.seconds < 0 || body.seconds > 604800) return reply(400);
  const day = now.toISOString().slice(0, 10);
  try {
    await env.DB.batch([
      env.DB.prepare(`INSERT INTO installation_daily (day, installation_id, sessions, seconds) VALUES (?, ?, 1, ?)
        ON CONFLICT(day, installation_id) DO UPDATE SET sessions = MIN(sessions + 1, 1000000000),
        seconds = MIN(seconds + excluded.seconds, 1000000000000)`).bind(day, body.installation_id, body.seconds),
      env.DB.prepare(`INSERT INTO daily_totals (day, sessions, seconds) VALUES (?, 1, ?)
        ON CONFLICT(day) DO UPDATE SET sessions = MIN(sessions + 1, 1000000000),
        seconds = MIN(seconds + excluded.seconds, 1000000000000)`).bind(day, body.seconds),
    ]);
    return reply(204);
  } catch { return reply(503); }
}

export async function prune(env, now = new Date()) {
  const cutoff = (days) => new Date(now.getTime() - days * 86400000).toISOString().slice(0, 10);
  await env.DB.batch([
    env.DB.prepare('DELETE FROM session_day_receipts WHERE day < ?').bind(cutoff(90)),
    env.DB.prepare('DELETE FROM installation_daily WHERE day < ?').bind(cutoff(90)),
    env.DB.prepare('DELETE FROM daily_totals WHERE day < ?').bind(cutoff(365)),
    env.DB.prepare('DELETE FROM relay_session_day_receipts WHERE day < ?').bind(cutoff(90)),
    env.DB.prepare('DELETE FROM relay_installation_daily WHERE day < ?').bind(cutoff(90)),
    env.DB.prepare('DELETE FROM relay_daily_totals WHERE day < ?').bind(cutoff(365)),
  ]);
}

export default {
  fetch(request, env) { return record(request, env); },
  async scheduled(_event, env) { try { await prune(env); } catch { /* No request logs or retries. */ } },
};
