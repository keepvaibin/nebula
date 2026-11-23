import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import * as blog from '../scripts/render-blog.mjs';
import { site } from '../scripts/lib/site.mjs';
import makeConfig from '../vite.config.js';

// A post's `image:` is its link-preview picture (content/blog/README.md). Chat and social sites fetch the
// address the page's share tags name, so the finished build must hold a file at exactly that address.

const published = blog.buildBlog({ drafts: false });
const withImage = published.posts.filter((post) => post.image);

/** The files the build is told to emit, as the site's Vite plugin emits them. */
function emittedByBuild() {
  const config = makeConfig({ mode: 'production', command: 'build' });
  const plugin = config.plugins.find((p) => p.name === 'blog-preview-images');
  assert.ok(plugin, 'vite.config.js has the plugin that emits the posts’ preview pictures');
  const emitted = new Map();
  plugin.generateBundle.call({
    emitFile: ({ type, fileName, source }) => {
      assert.equal(type, 'asset');
      emitted.set(fileName, Buffer.from(source));
    },
  });
  return emitted;
}

test('the site has posts with a preview picture to check', () => {
  assert.ok(withImage.length >= 2, 'the v2 post and the beta post both name an image');
});

test('every post with an image has its preview file at the address its share tags name', () => {
  assert.equal(typeof blog.blogPreviewFiles, 'function');
  const files = new Map(blog.blogPreviewFiles({ drafts: false }).map((f) => [f.fileName, f]));
  for (const post of withImage) {
    const html = published.files.get(`${post.path}index.html`);
    const tag = /<meta property="og:image" content="([^"]+)"/.exec(html);
    assert.ok(tag, `${post.slug} has an og:image tag`);
    const address = tag[1];
    assert.ok(address.startsWith(site.url), `${post.slug}: the address is absolute, on the site`);
    const path = address.slice(site.url.length);
    assert.equal(path, `${post.path}${post.image}`, `${post.slug}: the file's own name, not a hashed one`);
    const preview = files.get(path);
    assert.ok(preview, `${post.slug}: the build is told to emit ${path}`);
    assert.equal(preview.url, address);
    assert.deepEqual(readFileSync(preview.source), readFileSync(`${post.folder}/${post.image}`));
  }
});

test('the build emits each of those files under exactly that name, with the source file’s bytes', () => {
  const emitted = emittedByBuild();
  for (const post of withImage) {
    const html = published.files.get(`${post.path}index.html`);
    const path = /<meta property="og:image" content="([^"]+)"/.exec(html)[1].slice(site.url.length);
    assert.ok(emitted.has(path), `${post.slug}: the build holds ${path}`);
    assert.ok(emitted.get(path).equals(readFileSync(`${post.folder}/${post.image}`)), post.slug);
  }
});

test('a post without an image keeps the site icon, and no preview file is emitted for it', () => {
  const none = published.posts.filter((post) => !post.image);
  for (const post of none) {
    const html = published.files.get(`${post.path}index.html`);
    assert.match(html, new RegExp(`og:image" content="${site.url}${site.image}"`), post.slug);
  }
  const emitted = emittedByBuild();
  assert.equal(emitted.size, withImage.length);
});
