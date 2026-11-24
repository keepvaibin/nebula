import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { resolve, dirname, relative, sep } from 'node:path';
import { pageFile } from '../scripts/render-pages.mjs';
import { docRoutes as listDocRoutes } from '../scripts/render-docs.mjs';
import { blogRoutes as listBlogRoutes } from '../scripts/render-blog.mjs';
const root = resolve(import.meta.dirname, '..');
const docRoutes = new Set(listDocRoutes());
const blogRoutes = new Set(listBlogRoutes());
const pages = JSON.parse(readFileSync(resolve(root, 'content/pages.json'), 'utf8'));
const documents = new Map(
  pages.map((p) => [
    resolve(root, pageFile(p.path)),
    readFileSync(resolve(root, pageFile(p.path)), 'utf8').replace(/\s+/g, ' '),
  ]),
);
test('player pages have valid local links, anchors and unique IDs', () => {
  for (const [file, html] of documents) {
    const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
    assert.equal(new Set(ids).size, ids.length, `Duplicate ID in ${file}`);
    for (const match of html.matchAll(/\bhref="([^"]+)"/g)) {
      const href = match[1];
      if (/^(https?:|mailto:)/.test(href)) continue;
      const [path, anchor] = href.split('#');
      // The 404 page links from the site root, since it is served at any address.
      let target = !path
        ? file
        : path.startsWith('/')
          ? resolve(root, '.' + path)
          : resolve(dirname(file), path);
      // Modding docs are generated at build time; tests/modding-docs.test.js checks them.
      const fromRoot = relative(root, target).split(sep).join('/');
      if (fromRoot === 'modding/docs' || fromRoot.startsWith('modding/docs/')) {
        assert.ok(
          docRoutes.has(fromRoot.replace(/\/?$/, '/')),
          `Missing docs page ${href} in ${file}`,
        );
        continue;
      }
      // The blog is generated at build time too; tests/blog.test.js checks its pages.
      if (fromRoot === 'blog' || fromRoot.startsWith('blog/')) {
        assert.ok(
          blogRoutes.has(fromRoot.replace(/\/?$/, '/')),
          `Missing blog page ${href} in ${file}`,
        );
        continue;
      }
      // Files in public/ are served from the site root.
      if (!existsSync(target) && existsSync(resolve(root, 'public', fromRoot)))
        target = resolve(root, 'public', fromRoot);
      assert.ok(existsSync(target), `Missing link ${href} in ${file}`);
      if (statSync(target).isDirectory()) target = resolve(target, 'index.html');
      assert.ok(existsSync(target), `Missing index ${href} in ${file}`);
      if (anchor)
        assert.ok(
          documents.get(target)?.includes(`id="${anchor}"`),
          `Missing anchor ${href} in ${file}`,
        );
    }
  }
});
test('player pages exclude draft instructions and developer-only navigation', () => {
  for (const [file, html] of documents) {
    assert.doesNotMatch(
      html,
      /\[CONFIRM|\[PLACEHOLDER|FAN CONCEPT|unofficial design demo|href="[^"]*\/dev\//,
      file,
    );
    assert.equal((html.match(/<h1\b/g) || []).length, 1, file);
    assert.match(html, /<meta\s+name="description"\s+content="[^"]+"/);
  }
});
test('all twenty-one error codes and current download links remain present', () => {
  const errors = documents.get(resolve(root, 'help/error-codes/index.html'));
  assert.equal((errors.match(/class="error-entry"/g) || []).length, 21);
  assert.match(
    documents.get(resolve(root, 'download/index.html')),
    /releases\/tag\/v2\.0\.0-beta\.1"/,
  );
  assert.match(documents.get(resolve(root, 'index.html')), /https:\/\/discord.gg\/2fJEdhAw2f/);
});
