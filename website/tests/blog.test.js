import test from 'node:test';
import assert from 'node:assert/strict';
import { buildBlog, videoEmbed, imageSize, slugify } from '../scripts/render-blog.mjs';

const published = buildBlog({ drafts: false });
const withDrafts = buildBlog({ drafts: true });

test('the blog builds with no broken links, missing fields or missing media', () => {
  assert.deepEqual(published.problems, []);
  assert.deepEqual(withDrafts.problems, []);
  assert.ok(published.posts.length >= 1);
});

test('drafts stay off the site, and the index lists posts newest first', () => {
  assert.ok(published.posts.every((post) => !post.draft));
  assert.ok(withDrafts.posts.some((post) => post.draft));
  const dates = published.posts.map((post) => post.date);
  assert.deepEqual(dates, [...dates].sort().reverse());
  const index = published.files.get('blog/index.html');
  let last = -1;
  for (const post of published.posts) {
    const at = index.indexOf(`href="${post.slug}/"`);
    assert.ok(at > last, `${post.slug} is listed in date order`);
    last = at;
  }
});

test('every page has one title, a description, the blog in the navigation and no leftover placeholders', () => {
  for (const [path, html] of withDrafts.files) {
    assert.equal((html.match(/<h1\b/g) || []).length, 1, path);
    assert.match(html, /<meta name="description" content="[^"]+"/, path);
    assert.match(html, /aria-current="page" href="(\.\.\/)+blog\/"/, path);
    assert.doesNotMatch(html, /\{\{|@@[A-Z]+@@/, path);
  }
});

test('the sample post renders every supported kind of content', () => {
  const html = withDrafts.files.get('blog/sample-post/index.html');
  assert.match(html, /<div class="post-table"[^>]*><table>/);
  assert.match(html, /<div class="post-code"><pre[^>]*><code class="language-lumascript">/);
  assert.match(
    html,
    /<figure class="post-media"><img src="sample-image\.jpg" alt="[^"]+" width="1600" height="900"/,
  );
  assert.match(html, /<figcaption>An image from this post/);
  assert.match(
    html,
    /<video class="post-video" controls[^>]*><source src="sample-video\.mp4" type="video\/mp4"/,
  );
  assert.match(html, /<iframe src="https:\/\/www\.youtube-nocookie\.com\/embed\/aGvhnf7gfMI"/);
  assert.match(html, /<a href="\.\.\/nebula-v2\/">/);
  assert.match(html, /<a href="\.\.\/\.\.\/play\/mods\/">/);
  assert.match(html, /<span class="text-large">larger text<\/span>/);
  assert.ok(withDrafts.media.has('blog/sample-post/sample-video.mp4'));
});

test('unsafe HTML in a post is shown as text', () => {
  const html = withDrafts.files.get('blog/nebula-v2/index.html');
  assert.doesNotMatch(html, /<script>(?!location)/);
});

test('media helpers recognise providers and image sizes', () => {
  assert.equal(
    videoEmbed('https://youtu.be/aGvhnf7gfMI?t=30').src,
    'https://www.youtube-nocookie.com/embed/aGvhnf7gfMI?start=30',
  );
  assert.equal(videoEmbed('https://www.youtube.com/watch?v=aGvhnf7gfMI').provider, 'YouTube');
  assert.equal(
    videoEmbed('https://vimeo.com/123456789').src,
    'https://player.vimeo.com/video/123456789?dnt=1',
  );
  assert.equal(videoEmbed('https://example.com/clip.mp4'), null);
  const png = Buffer.alloc(32);
  png.writeUInt32BE(0x89504e47, 0);
  png.writeUInt32BE(640, 16);
  png.writeUInt32BE(360, 20);
  assert.deepEqual(imageSize(png), { width: 640, height: 360 });
  assert.equal(slugify('So, what was built?'), 'so-what-was-built');
});
