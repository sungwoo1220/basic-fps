import { connect } from 'cloudflare:sockets';

// Room board for the DX11 trial: the host's own PC runs the room, this only lists it.
const ALIVE_MS = 45_000, FORGET_MS = 10 * 60_000, RECHECK_MS = 60_000, PER_IP = 3;
const LAN = /^http:\/\/(10\.\d{1,3}|172\.(1[6-9]|2\d|3[01])|192\.168)\.\d{1,3}\.\d{1,3}:\d{1,5}$/;

const json = (body, status = 200) => new Response(JSON.stringify(body), {
  status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
});
const clientIp = req => (req.headers.get('cf-connecting-ip') || '').trim();
const text = (value, max) => String(value ?? '').replace(/[\u0000-\u001f\u007f<>]/g, '').trim().slice(0, max);
const count = (value, min, max, fallback) => {
  const n = Math.floor(Number(value));
  return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
};
const hex = buf => [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('');
const hash = async token => hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(token)));
// One person can hold a whole IPv6 /64, so the per-host room limit counts that block instead of a single address.
function ipKey(ip) {
  if (!ip.includes(':')) return ip;
  const [head, tail = ''] = ip.toLowerCase().split('::');
  const a = head ? head.split(':') : [], b = tail ? tail.split(':') : [];
  const groups = ip.includes('::') ? [...a, ...Array(Math.max(0, 8 - a.length - b.length)).fill('0'), ...b] : a;
  return groups.slice(0, 4).map(g => (parseInt(g, 16) || 0).toString(16)).join(':');
}
const address = (ip, port) => `http://${ip.includes(':') ? `[${ip}]` : ip}:${port}`;

// Opens the host's public address from outside its network and expects the room's HTTP answer.
async function reachable(ip, port) {
  let socket;
  try {
    socket = connect({ hostname: ip, port });
    const attempt = (async () => {
      await socket.opened;
      const writer = socket.writable.getWriter();
      await writer.write(new TextEncoder().encode('HEAD / HTTP/1.0\r\nHost: room\r\n\r\n'));
      writer.releaseLock();
      const { value } = await socket.readable.getReader().read();
      return !!value && new TextDecoder().decode(value).startsWith('HTTP/');
    })();
    const timeout = new Promise(resolve => setTimeout(() => resolve(false), 3000));
    return await Promise.race([attempt, timeout]);
  } catch {
    return false;
  } finally {
    try { socket?.close(); } catch {}
  }
}

async function owned(env, req, id) {
  const token = (req.headers.get('authorization') || '').replace(/^Bearer\s+/i, '');
  if (!token) return null;
  return env.DB.prepare('SELECT * FROM rooms WHERE id = ? AND token_hash = ?').bind(id, await hash(token)).first();
}

async function list(env, req) {
  const now = Date.now(), you = clientIp(req);
  const { results } = await env.DB.prepare(
    'SELECT * FROM rooms WHERE updated_at > ? ORDER BY reachable DESC, players DESC, created_at ASC LIMIT 50',
  ).bind(now - ALIVE_MS).all();
  return json({
    you,
    rooms: results.map(r => ({
      id: r.id, name: r.name, host: r.host, players: r.players, max: 16,
      address: address(r.ip, r.port), lan: r.lan, sameNetwork: !!you && you === r.ip,
      reachable: !!r.reachable, upnp: !!r.upnp, since: r.created_at,
    })),
  });
}

async function create(env, req) {
  const ip = clientIp(req);
  if (!ip) return json({ error: '주소를 확인하지 못했어요.' }, 400);
  const body = await req.json().catch(() => ({}));
  const port = Number(body.port);
  if (!Number.isInteger(port) || port < 1 || port > 65535) return json({ error: '포트가 올바르지 않아요.' }, 400);
  const host = text(body.host, 16) || 'Player';
  const name = text(body.name, 24) || `${host}의 방`;
  const lan = LAN.test(String(body.lan || '')) ? String(body.lan) : '';
  const now = Date.now();
  await env.DB.prepare('DELETE FROM rooms WHERE updated_at < ?').bind(now - FORGET_MS).run();
  const { results: live } = await env.DB.prepare('SELECT ip FROM rooms WHERE updated_at > ?').bind(now - ALIVE_MS).all();
  const key = ipKey(ip);
  if (live.filter(r => ipKey(r.ip) === key).length >= PER_IP) return json({ error: '한 곳에서 동시에 올릴 수 있는 방은 3개까지예요.' }, 429);
  const id = crypto.randomUUID(), token = hex(crypto.getRandomValues(new Uint8Array(24)));
  const open = await reachable(ip, port);
  await env.DB.prepare(
    `INSERT INTO rooms (id, token_hash, name, host, ip, port, lan, players, upnp, reachable, checked_at, created_at, updated_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
  ).bind(id, await hash(token), name, host, ip, port, lan, count(body.players, 0, 16, 1), body.upnp ? 1 : 0, open ? 1 : 0, now, now, now).run();
  return json({ id, token, address: address(ip, port), reachable: open }, 201);
}

async function beat(env, req, id) {
  const room = await owned(env, req, id);
  if (!room) return json({ error: '목록에서 사라진 방이에요.' }, 404);
  const body = await req.json().catch(() => ({}));
  const now = Date.now();
  // The host's address can change (router restart, mobile network): follow it, and test the new one at once.
  const ip = clientIp(req) || room.ip, moved = ip !== room.ip;
  let open = !moved && !!room.reachable, checked = moved ? 0 : room.checked_at;
  // Re-test periodically in both directions so a closed port or a lost port-forward does not stay "reachable".
  if (now - checked > RECHECK_MS) { open = await reachable(ip, room.port); checked = now; }
  await env.DB.prepare('UPDATE rooms SET ip = ?, players = ?, reachable = ?, checked_at = ?, updated_at = ? WHERE id = ?')
    .bind(ip, count(body.players, 0, 16, room.players), open ? 1 : 0, checked, now, id).run();
  return json({ ok: true, reachable: open });
}

async function remove(env, req, id) {
  const room = await owned(env, req, id);
  if (room) await env.DB.prepare('DELETE FROM rooms WHERE id = ?').bind(id).run();
  return json({ ok: true });
}

export default {
  async fetch(req, env) {
    const { pathname } = new URL(req.url);
    if (!pathname.startsWith('/api/')) return env.ASSETS.fetch(req);
    try {
      if (pathname === '/api/rooms') {
        if (req.method === 'GET') return await list(env, req);
        if (req.method === 'POST') return await create(env, req);
      }
      const match = pathname.match(/^\/api\/rooms\/([0-9a-f-]{36})(\/beat)?$/);
      if (match) {
        if (req.method === 'POST' && match[2]) return await beat(env, req, match[1]);
        if (req.method === 'DELETE' && !match[2]) return await remove(env, req, match[1]);
      }
      return json({ error: 'not found' }, 404);
    } catch (e) {
      console.error(e);
      return json({ error: '잠시 후 다시 시도해 주세요.' }, 500);
    }
  },
};
