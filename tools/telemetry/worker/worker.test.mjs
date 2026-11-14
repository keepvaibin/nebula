// node --test tools/telemetry/worker/worker.test.mjs: the Worker against stand-ins of its bucket, limiter and Durable Object.
import assert from "node:assert/strict";
import { test } from "node:test";

import worker, { DailyCap } from "./worker.mjs";

function environment({ limit = 3, cap = "1000" } = {}) {
  const stored = new Map();
  const hits = new Map();
  const days = new Map();
  return {
    stored,
    hits,
    DAILY_CAP: cap,
    REPORTS: { put: async (key, body) => void stored.set(key, body) },
    PER_ADDRESS: {
      limit: async ({ key }) => {
        hits.set(key, (hits.get(key) ?? 0) + 1);
        return { success: hits.get(key) <= limit };
      },
    },
    DAILY: {
      idFromName: (name) => name,
      get: (id) => {
        if (!days.has(id)) {
          const data = new Map();
          const storage = {
            get: async (k) => data.get(k),
            put: async (k, v) => void data.set(k, v),
            setAlarm: async () => {},
            deleteAll: async () => data.clear(),
          };
          const cap = new DailyCap({ storage });
          days.set(id, { fetch: (input) => cap.fetch(new Request(input)) });
        }
        return days.get(id);
      },
    },
  };
}

const envelope = (size = 200) => {
  const body = new Uint8Array(size);
  body.set([0x4e, 0x42, 0x44, 0x52, 0x01, 0x01]);
  return body;
};

const post = (body, { path = "/v1/report", type = "application/x-nebula-report", address = "203.0.113.7", method = "POST" } = {}) =>
  new Request(`https://nebula-diag.example.workers.dev${path}`, {
    method,
    headers: { "content-type": type, "cf-connecting-ip": address },
    body: method === "GET" || method === "HEAD" ? undefined : body,
  });

test("a sealed report is stored under the UTC date with a random name, 204", async () => {
  const env = environment();
  const response = await worker.fetch(post(envelope()), env);
  assert.equal(response.status, 204);
  assert.equal(env.stored.size, 1);
  const [key, body] = [...env.stored][0];
  assert.match(key, /^\d{4}-\d{2}-\d{2}\/[0-9a-f-]{36}\.nbdr$/);
  assert.equal(key.slice(0, 10), new Date().toISOString().slice(0, 10));
  assert.deepEqual(new Uint8Array(body), envelope());
  assert.equal(response.headers.get("access-control-allow-origin"), null);
});

test("every other method or path is 404 and stores nothing", async () => {
  const env = environment();
  for (const method of ["GET", "PUT", "DELETE", "OPTIONS", "HEAD"]) {
    assert.equal((await worker.fetch(post(envelope(), { method }), env)).status, 404, method);
  }
  for (const path of ["/", "/v1/report/x", "/v1/reports", "/v1/report?list=1"]) {
    assert.equal((await worker.fetch(post(envelope(), { path }), env)).status, 404, path);
  }
  assert.equal(env.stored.size, 0);
});

test("content type, size and magic are checked", async () => {
  const env = environment({ limit: 100 });
  assert.equal((await worker.fetch(post(envelope(), { type: "application/json" }), env)).status, 415);
  assert.equal((await worker.fetch(post(envelope(256 * 1024 + 1)), env)).status, 413);
  assert.equal((await worker.fetch(post(envelope(256 * 1024)), env)).status, 204);
  assert.equal((await worker.fetch(post(envelope(63)), env)).status, 400);
  const wrong = envelope();
  wrong[4] = 2;
  assert.equal((await worker.fetch(post(wrong), env)).status, 400);
  assert.equal(env.stored.size, 1);
});

test("the per-address limit is keyed by a hash, never the address", async () => {
  const env = environment({ limit: 3 });
  const statuses = [];
  for (let i = 0; i < 5; i++) statuses.push((await worker.fetch(post(envelope()), env)).status);
  assert.deepEqual(statuses, [204, 204, 204, 429, 429]);
  assert.equal((await worker.fetch(post(envelope(), { address: "198.51.100.1" }), env)).status, 204);
  for (const key of env.hits.keys()) {
    assert.match(key, /^[0-9a-f]{64}$/);
    assert.ok(!key.includes("203.0.113.7"));
  }
  for (const key of env.stored.keys()) assert.ok(!key.includes("203") && !key.includes("198"));
});

test("the daily cap counts stored reports over every address", async () => {
  const env = environment({ limit: 100, cap: "2" });
  const statuses = [];
  for (let i = 0; i < 4; i++) statuses.push((await worker.fetch(post(envelope(), { address: `192.0.2.${i}` }), env)).status);
  assert.deepEqual(statuses, [204, 204, 503, 503]);
  assert.equal(env.stored.size, 2);
});

test("junk does not count against the daily cap", async () => {
  const env = environment({ limit: 100, cap: "1" });
  assert.equal((await worker.fetch(post(envelope(10)), env)).status, 400);
  assert.equal((await worker.fetch(post(envelope()), env)).status, 204);
});
