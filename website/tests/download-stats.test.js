import test from 'node:test';
import assert from 'node:assert/strict';
import { fetchDownloadStats } from '../download-stats.js';

test('download total includes every page and beta assets, excluding drafts', async () => {
  const calls = [];
  const stats = await fetchDownloadStats(async (url) => {
    calls.push(url);
    const rows =
      calls.length === 1
        ? Array.from({ length: 100 }, () => ({ assets: [{ download_count: 2 }] }))
        : [
            { prerelease: true, assets: [{ download_count: 3 }] },
            { draft: true, assets: [{ download_count: 10 }] },
          ];
    return { ok: true, json: async () => rows };
  });
  assert.equal(stats.total, 203);
  assert.equal(stats.releases, 101);
  assert.equal(calls.length, 2);
  assert.match(calls[1], /page=2$/);
});

test('a GitHub failure rejects instead of showing an incomplete total', async () => {
  await assert.rejects(
    fetchDownloadStats(async () => ({ ok: false, status: 403 })),
    /403/,
  );
});
