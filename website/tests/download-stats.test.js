import test from 'node:test';
import assert from 'node:assert/strict';
import { fetchDownloadStats, isPackage } from '../download-stats.js';

test('download total includes every page and beta assets, excluding drafts', async () => {
  const calls = [];
  const stats = await fetchDownloadStats(async (url) => {
    calls.push(url);
    const rows =
      calls.length === 1
        ? Array.from({ length: 100 }, () => ({
            assets: [{ name: 'Nebula-Setup.exe', download_count: 2 }],
          }))
        : [
            { prerelease: true, assets: [{ name: 'Nebula-Beta-Android.apk', download_count: 3 }] },
            { draft: true, assets: [{ name: 'Nebula-Setup.exe', download_count: 10 }] },
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

// The releases of keepvaibin/nebula as the API gives them. The package counts were read on 2026-11-23. The beta's
// eleven files are its real ones: four packages, the signatures of the installer and the app, the manifests and
// manifest signatures of the Linux and macOS archives, and one checksum list. The counts of the files that must not
// count are made up and large, so a counter that adds one of them is far off.
const file = (name, download_count) => ({ name, download_count });
const releases = [
  {
    tag_name: 'v2.0.0-beta.1',
    prerelease: true,
    assets: [
      file('Nebula-Beta-Setup.exe', 21),
      file('Nebula-Beta-Setup.exe.sig', 5000),
      file('Nebula-Beta-Android.apk', 12),
      file('Nebula-Beta-Android.apk.sig', 5001),
      file('Nebula-Beta-Linux-x64.tar.gz', 13),
      file('Nebula-Beta-Linux-x64.tar.gz.manifest', 6002),
      file('Nebula-Beta-Linux-x64.tar.gz.manifest.sig', 7002),
      file('Nebula-Beta-macOS.zip', 8),
      file('Nebula-Beta-macOS.zip.manifest', 6003),
      file('Nebula-Beta-macOS.zip.manifest.sig', 7003),
      file('SHA256SUMS', 8000),
    ],
  },
  { tag_name: 'v1.0.2', assets: [file('Nebula-Setup.exe', 17750), file('Nebula-Setup.exe.sig', 651)] },
  { tag_name: 'v1.0.1', assets: [file('Nebula-Setup.exe', 14110), file('Nebula-Setup.exe.sig', 1073)] },
  { tag_name: 'v1.0.0', assets: [file('Nebula-Setup.exe', 9300), file('Nebula-Setup.exe.sig', 279)] },
];
const serve = (rows) => async () => ({ ok: true, json: async () => rows });

test('the counter sums the packages of every system over every published release', async () => {
  // 17750 + 14110 + 9300 = 41160 for the three old releases, and 21 + 12 + 13 + 8 = 54 for the beta's four packages.
  const stats = await fetchDownloadStats(serve(releases));
  assert.equal(stats.total, 41160 + 54);
  assert.equal(stats.releases, 4);
});

test('the three old releases alone give the number the site showed for the Windows installer', async () => {
  const stats = await fetchDownloadStats(serve(releases.slice(1)));
  assert.equal(stats.total, 41160);
  assert.equal(stats.releases, 3);
});

test('a draft release adds nothing, whatever it holds', async () => {
  const draft = { tag_name: 'v9', draft: true, assets: [file('Nebula-Beta-Setup.exe', 1000)] };
  const stats = await fetchDownloadStats(serve([draft, ...releases]));
  assert.equal(stats.total, 41160 + 54);
  assert.equal(stats.releases, 4);
});

test('signatures, manifests and checksum lists are not packages, a package of any system or processor is', () => {
  for (const name of [
    'Nebula-Setup.exe',
    'Nebula-Beta-Android.apk',
    'Nebula-Beta-Linux-x64.tar.gz',
    'Nebula-Beta-Linux-arm64.tar.gz',
    'Nebula-Beta-macOS.zip',
    'Nebula-Beta-Windows-arm64-Setup.exe',
    'Nebula-Beta-x86_64.AppImage',
    'Nebula-Beta.dmg',
    'Nebula-Beta.deb',
    'NEBULA-BETA-SETUP.EXE',
  ])
    assert.equal(isPackage(name), true, name);
  for (const name of [
    'Nebula-Setup.exe.sig',
    'Nebula-Beta-Android.apk.sig',
    'Nebula-Beta-Setup.exe.sig',
    'Nebula-Beta-macOS.zip.manifest',
    'Nebula-Beta-macOS.zip.manifest.sig',
    'Nebula-Beta-Linux-x64.tar.gz.manifest',
    'Nebula-Beta-Linux-x64.tar.gz.manifest.sig',
    'Nebula-Beta-macOS.zip.sha256',
    'SHA256SUMS',
    'SHA256SUMS.sig',
    'update-check.json',
    'notes.txt',
    'Nebula-Beta.unknown-kind',
    'exe',
    '',
    undefined,
  ])
    assert.equal(isPackage(name), false, String(name));
});

test('a file of a kind the site has not seen adds nothing until it is named', async () => {
  const stats = await fetchDownloadStats(
    serve([{ assets: [file('Nebula-Setup.exe', 5), file('Nebula-Future.newkind', 999)] }]),
  );
  assert.equal(stats.total, 5);
});
