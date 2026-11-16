import test from 'node:test';
import assert from 'node:assert/strict';
import { buildDocs, slugify, docRoutes } from '../scripts/render-docs.mjs';
import { highlight } from '../scripts/lib/highlight.mjs';

const { files, problems, pages, sections, groups, index, zips } = buildDocs();

test('modding docs build with no broken links, missing fields or unplaced pages', () => {
  assert.deepEqual(problems, []);
  assert.ok(pages.length > 100, 'expected the full set of modding pages');
});

test('every section has pages, and every page has one heading, a description and unique IDs', () => {
  for (const section of sections) assert.ok(section.pages.length, `${section.id} has no pages`);
  assert.ok(groups.length >= 2);
  for (const [path, html] of files) {
    if (html.includes('http-equiv="refresh"')) continue;
    assert.equal((html.match(/<h1\b/g) || []).length, 1, path);
    assert.match(html, /<meta name="description" content="[^"]+"/, path);
    const ids = [...html.matchAll(/<[a-z][^<>]*?\sid="([^"]+)"/g)].map((m) => m[1]);
    assert.equal(new Set(ids).size, ids.length, `Duplicate ID in ${path}`);
  }
});

test('in-page links point at headings that exist', () => {
  for (const [path, html] of files) {
    const ids = new Set([...html.matchAll(/<[a-z][^<>]*?\sid="([^"]+)"/g)].map((m) => m[1]));
    for (const [, anchor] of html.matchAll(/href="#([^"]+)"/g))
      assert.ok(ids.has(anchor), `${path} links to missing #${anchor}`);
  }
});

test('published pages contain no internal notes or repository paths', () => {
  const internal =
    /INBOX-|agents\/luma|PLANS-UNSTARTED|Nebula-L\d|DECISIONS \d|State: generated from|contracts\/docs\.md|<!--|\]\(\.\.\/guides\/|^Not yet:|\bD\d+\.\d+\b|A\d\d[A-Z]-\d|`(?:language|format|lumalink|content)\/[a-z-]+\.md`/m;
  for (const page of pages) assert.doesNotMatch(page.body, internal, page.key);
});

test('old addresses redirect to pages that exist', () => {
  const routes = new Set(docRoutes());
  for (const [path, html] of files) {
    const target = /http-equiv="refresh" content="0; url=([^"]+)"/.exec(html)?.[1];
    if (!target) continue;
    assert.ok(routes.has(path.replace(/index\.html$/, '')), path);
    const resolved = new URL(target, `https://x/${path}`).pathname.slice(1);
    assert.ok(files.has(`${resolved}index.html`), `${path} redirects to missing ${resolved}`);
  }
});

test('example mods are packed as valid zip files', () => {
  assert.ok(zips.size >= 10);
  for (const [name, zip] of zips) {
    assert.equal(zip.readUInt32LE(0), 0x04034b50, name);
    assert.equal(zip.readUInt32LE(zip.length - 22), 0x06054b50, name);
  }
});

test('search index covers every page', () => {
  assert.equal(index.length, pages.length);
  for (const entry of index) assert.ok(entry.t && entry.u && entry.d, entry.u);
});

test('anchors and highlighting are stable and safe', () => {
  assert.equal(slugify('Timers and `counters`'), 'timers-and-counters');
  assert.equal(slugify('A mod’s life'), 'a-mods-life');
  assert.equal(slugify('`find_for`'), 'find_for');
  assert.equal(
    highlight('let x = "<b>" // hi', 'lumascript'),
    '<span class="tk-kw">let</span> x = <span class="tk-st">"&lt;b&gt;"</span> <span class="tk-cm">// hi</span>',
  );
  assert.equal(highlight('<script>', 'unknown'), '&lt;script&gt;');
});
