// Nebula's diagnostics drop (runtime/include/galaxy/diag_report.h): one POST route that stores sealed reports. It cannot read
// them (they are sealed to the developer's key) and has no route to read, list or delete them. The client's address
// is seen while a report is received and used only as the rate limiter's key, as an HMAC-SHA256 under the deployment's
// secret ADDRESS_SALT (an unsalted hash of an IPv4 address is reversed by trying all 2^32). The minute limiter keeps
// that key in memory for 60 s; the day's counter keeps it with a count until it deletes itself two days later. Nothing
// stores or logs the address itself.

const PATH = "/v1/report";
const CONTENT_TYPE = "application/x-nebula-report";
const MAX_BYTES = 256 * 1024;
const MIN_BYTES = 48 + 16; // header + tag
const MAGIC = [0x4e, 0x42, 0x44, 0x52, 0x01]; // "NBDR", envelope version 1
const DAY_MS = 86400 * 1000;
// Stored reports per address per UTC day (PER_ADDRESS_DAY in wrangler.toml overrides it).
const PER_DAY = 3;
// Workers KV instead of R2: { expirationTtl: 180 * 86400 } (tools/telemetry/README.md).
const PUT_OPTIONS = {};

const answer = (status) => new Response(null, { status });

async function readCapped(request) {
  const declared = Number(request.headers.get("content-length") ?? "0");
  if (declared > MAX_BYTES) return null;
  if (request.body === null) return new Uint8Array(0);
  const reader = request.body.getReader();
  const parts = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > MAX_BYTES) {
      await reader.cancel();
      return null;
    }
    parts.push(value);
  }
  const body = new Uint8Array(size);
  let at = 0;
  for (const part of parts) {
    body.set(part, at);
    at += part.byteLength;
  }
  return body;
}

// The deployment's salt: a secret of at least 32 characters (`wrangler secret put ADDRESS_SALT`, README.md).
const MIN_SALT = 32;

async function addressKey(request, salt) {
  const address = request.headers.get("cf-connecting-ip") ?? "";
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey("raw", encoder.encode(salt), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const digest = await crypto.subtle.sign("HMAC", key, encoder.encode(address));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method !== "POST" || url.pathname !== PATH || url.search !== "") return answer(404);
    if ((request.headers.get("content-type") ?? "").toLowerCase() !== CONTENT_TYPE) return answer(415);
    // No salt, no service: the limiter is never keyed by something an address can be recovered from.
    const salt = typeof env.ADDRESS_SALT === "string" ? env.ADDRESS_SALT : "";
    if (salt.length < MIN_SALT) return answer(503);
    const who = await addressKey(request, salt);
    const { success } = await env.PER_ADDRESS.limit({ key: who });
    if (!success) return answer(429);
    const body = await readCapped(request);
    if (body === null) return answer(413);
    if (body.byteLength < MIN_BYTES || MAGIC.some((byte, i) => body[i] !== byte)) return answer(400);
    const date = new Date().toISOString().slice(0, 10);
    const per = Number(env.PER_ADDRESS_DAY) || PER_DAY;
    const cap = await env.DAILY.get(env.DAILY.idFromName(date))
      .fetch(`https://cap/take?cap=${Number(env.DAILY_CAP) || 1000}&per=${per}&who=${who}`);
    if (cap.status === 429) return answer(429);
    if (cap.status !== 204) return answer(503);
    await env.REPORTS.put(`${date}/${crypto.randomUUID()}.nbdr`, body, PUT_OPTIONS);
    return answer(204);
  },
};

// One object per UTC day counts that day's stored reports, in total and per address key; it deletes itself two days later.
export class DailyCap {
  constructor(state) {
    this.state = state;
  }

  async fetch(request) {
    const params = new URL(request.url).searchParams;
    const cap = Number(params.get("cap")) || 1000;
    const per = Number(params.get("per")) || PER_DAY;
    const who = params.get("who") ?? "";
    if (!/^[0-9a-f]{64}$/.test(who)) return answer(400);
    const used = (await this.state.storage.get("used")) ?? 0;
    if (used >= cap) return answer(503);
    const mine = (await this.state.storage.get(`a:${who}`)) ?? 0;
    if (mine >= per) return answer(429);
    if (used === 0) await this.state.storage.setAlarm(Date.now() + 2 * DAY_MS);
    await this.state.storage.put("used", used + 1);
    await this.state.storage.put(`a:${who}`, mine + 1);
    return answer(204);
  }

  async alarm() {
    await this.state.storage.deleteAll();
  }
}
