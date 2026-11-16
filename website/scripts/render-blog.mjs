/**
 * Renders the Developer Blog from content/blog/ into blog/.
 *
 * Each post is a folder named after its slug, holding index.md and any images or videos it
 * uses: content/blog/SLUG/index.md becomes blog/SLUG/index.html, and the folder's media files
 * are copied beside it, so a post refers to them with plain relative paths ("screenshot.png").
 * The index (blog/index.html) lists every published post, newest first.
 *
 * Front matter: title, date (YYYY-MM-DD) and summary are required; draft: true keeps a post
 * out of the site unless BLOG_DRAFTS=1. See content/blog/README.md for the authoring guide.
 */
import {
  readFileSync,
  readdirSync,
  existsSync,
  mkdirSync,
  writeFileSync,
  rmSync,
  statSync,
  copyFileSync,
} from 'node:fs';
import { resolve, dirname, posix, extname, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Marked, Renderer } from 'marked';
import { highlight } from './lib/highlight.mjs';
import { renderPageText } from './lib/page-text.mjs';
import { shareTags } from './lib/site.mjs';
import { docRoutes as listDocRoutes } from './render-docs.mjs';

const root = resolve(import.meta.dirname, '..');
const blogDir = resolve(root, 'content/blog');
const outDir = resolve(root, 'blog');
const templateDir = resolve(root, 'templates/blog');

export const IMAGE_TYPES = ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.avif', '.svg'];
export const VIDEO_TYPES = ['.mp4', '.webm', '.ogv', '.m4v'];
const MEDIA_TYPES = [...IMAGE_TYPES, ...VIDEO_TYPES, '.vtt'];

const escapeHtml = (value) =>
  String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');

const unescapeHtml = (value) =>
  String(value)
    .replaceAll('&lt;', '<')
    .replaceAll('&gt;', '>')
    .replaceAll('&quot;', '"')
    .replaceAll('&#39;', "'")
    .replaceAll('&amp;', '&');

/** Heading anchors follow GitHub's rule. */
export const slugify = (text) =>
  unescapeHtml(String(text).replace(/<[^>]+>/g, ''))
    .toLowerCase()
    .replace(/[^\p{L}\p{M}\p{N}\p{Pc} -]/gu, '')
    .replace(/ /g, '-') || 'section';

function parseFrontMatter(source, file) {
  const match = /^---\r?\n([\s\S]*?)\r?\n---\r?\n?/.exec(source);
  if (!match) throw new Error(`${file}: missing front matter (--- title: ... ---)`);
  const data = {};
  for (const line of match[1].split(/\r?\n/)) {
    if (!line.trim() || line.trim().startsWith('#')) continue;
    const colon = line.indexOf(':');
    if (colon < 0) throw new Error(`${file}: front matter line without a colon: ${line}`);
    let value = line.slice(colon + 1).trim();
    if (/^(['"]).*\1$/.test(value)) value = value.slice(1, -1);
    data[line.slice(0, colon).trim()] = value;
  }
  return { data, body: source.slice(match[0].length) };
}

/* ---------- Media ---------- */

/** Width and height of a PNG, JPEG, GIF or WebP, read from its header. */
export function imageSize(buffer) {
  if (buffer.length > 24 && buffer.readUInt32BE(0) === 0x89504e47)
    return { width: buffer.readUInt32BE(16), height: buffer.readUInt32BE(20) };
  if (buffer.length > 10 && buffer.toString('ascii', 0, 3) === 'GIF')
    return { width: buffer.readUInt16LE(6), height: buffer.readUInt16LE(8) };
  if (
    buffer.length > 30 &&
    buffer.toString('ascii', 0, 4) === 'RIFF' &&
    buffer.toString('ascii', 8, 12) === 'WEBP'
  ) {
    const kind = buffer.toString('ascii', 12, 16);
    if (kind === 'VP8 ')
      return { width: buffer.readUInt16LE(26) & 0x3fff, height: buffer.readUInt16LE(28) & 0x3fff };
    if (kind === 'VP8L') {
      const bits = buffer.readUInt32LE(21);
      return { width: (bits & 0x3fff) + 1, height: ((bits >> 14) & 0x3fff) + 1 };
    }
    if (kind === 'VP8X')
      return { width: buffer.readUIntLE(24, 3) + 1, height: buffer.readUIntLE(27, 3) + 1 };
  }
  if (buffer.length > 4 && buffer[0] === 0xff && buffer[1] === 0xd8) {
    let offset = 2;
    while (offset + 9 < buffer.length) {
      if (buffer[offset] !== 0xff) return null;
      const marker = buffer[offset + 1];
      const length = buffer.readUInt16BE(offset + 2);
      if (marker >= 0xc0 && marker <= 0xcf && ![0xc4, 0xc8, 0xcc].includes(marker))
        return { width: buffer.readUInt16BE(offset + 7), height: buffer.readUInt16BE(offset + 5) };
      offset += 2 + length;
    }
  }
  return null;
}

/** A YouTube or Vimeo page URL as an embeddable player URL, or null. */
export function videoEmbed(href) {
  let url;
  try {
    url = new URL(href);
  } catch {
    return null;
  }
  const host = url.hostname.replace(/^(www\.|m\.)/, '');
  let id;
  if (host === 'youtu.be') id = url.pathname.slice(1);
  else if (host === 'youtube.com' || host === 'youtube-nocookie.com') {
    if (url.pathname === '/watch') id = url.searchParams.get('v');
    else id = /^\/(?:embed|shorts|live)\/([\w-]+)/.exec(url.pathname)?.[1];
  }
  if (id && /^[\w-]{6,}$/.test(id)) {
    const start = Number.parseInt(
      url.searchParams.get('t') || url.searchParams.get('start') || '',
      10,
    );
    return {
      provider: 'YouTube',
      src: `https://www.youtube-nocookie.com/embed/${id}${start > 0 ? `?start=${start}` : ''}`,
    };
  }
  if (host === 'vimeo.com' || host === 'player.vimeo.com') {
    const vimeo = /(\d{6,})/.exec(url.pathname)?.[1];
    if (vimeo) return { provider: 'Vimeo', src: `https://player.vimeo.com/video/${vimeo}?dnt=1` };
  }
  return null;
}

const mediaKind = (href) => {
  if (videoEmbed(href)) return 'embed';
  const ext = extname(href.split(/[?#]/)[0]).toLowerCase();
  if (VIDEO_TYPES.includes(ext)) return 'video';
  return 'image';
};

/* ---------- Posts ---------- */

/** Every post folder, read and checked. Synchronous so vite.config.js can list the routes. */
export function loadPosts({ drafts = process.env.BLOG_DRAFTS === '1' } = {}) {
  const problems = [];
  const posts = [];
  if (!existsSync(blogDir)) return { posts, problems };
  for (const entry of readdirSync(blogDir, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const slug = entry.name;
    const file = `content/blog/${slug}/index.md`;
    if (!existsSync(resolve(root, file))) {
      problems.push(`content/blog/${slug}/ has no index.md`);
      continue;
    }
    if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(slug))
      problems.push(
        `${file}: the folder name is the post's address; use lower-case letters, numbers and hyphens`,
      );
    let parsed;
    try {
      parsed = parseFrontMatter(readFileSync(resolve(root, file), 'utf8'), file);
    } catch (error) {
      problems.push(error.message);
      continue;
    }
    const { data, body } = parsed;
    for (const field of ['title', 'date', 'summary'])
      if (!data[field]) problems.push(`${file}: front matter needs "${field}"`);
    if (
      data.date &&
      (!/^\d{4}-\d{2}-\d{2}$/.test(data.date) || Number.isNaN(Date.parse(`${data.date}T00:00:00Z`)))
    )
      problems.push(`${file}: date must be written YYYY-MM-DD, for example 2026-11-15`);
    const draft = data.draft === 'true';
    if (draft && !drafts) continue;
    posts.push({
      slug,
      file,
      folder: resolve(blogDir, slug),
      title: data.title || slug,
      date: data.date || '1970-01-01',
      summary: data.summary || '',
      author: data.author || '',
      image: data.image || '',
      draft,
      body,
      path: `blog/${slug}/`,
    });
  }
  posts.sort((a, b) => b.date.localeCompare(a.date) || a.title.localeCompare(b.title));
  return { posts, problems };
}

/** Output folders (relative to the site root) for vite.config.js. */
export function blogRoutes() {
  return ['blog/', ...loadPosts().posts.map((post) => post.path)];
}

const formatDate = (iso) =>
  new Date(`${iso}T12:00:00Z`).toLocaleDateString('en-US', {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
    timeZone: 'UTC',
  });

const readingMinutes = (markdown) =>
  Math.max(
    1,
    Math.round(
      markdown
        .replace(/```[\s\S]*?```/g, '')
        .split(/\s+/)
        .filter(Boolean).length / 220,
    ),
  );

/* ---------- Markdown ---------- */

// Authors may use these tags inside Markdown; everything else is shown as text.
const INLINE_TAG =
  /^<\/?(small|kbd|sup|sub|mark|u|br)\s*\/?>$|^<span class="(text-small|text-large|text-xlarge|text-muted)">$|^<\/span>$/i;

function makeMarked(context) {
  const marked = new Marked({ gfm: true, breaks: false });
  const base = new Renderer();
  const state = {
    post: null,
    ids: new Map(),
    media: new Set(),
    problems: context.problems,
    figures: 0,
  };

  const localMedia = (href) => {
    const clean = decodeURIComponent(href.split(/[?#]/)[0]);
    const key = posix.normalize(clean).replace(/^\.\//, '');
    if (key.startsWith('../') || key.startsWith('/')) {
      state.problems.push(`${state.post.file}: media "${href}" must be inside the post's folder`);
      return null;
    }
    const file = resolve(state.post.folder, key);
    if (!existsSync(file)) {
      state.problems.push(
        `${state.post.file}: no file named "${href}" in content/blog/${state.post.slug}/`,
      );
      return null;
    }
    const ext = extname(file).toLowerCase();
    if (!MEDIA_TYPES.includes(ext))
      state.problems.push(`${state.post.file}: "${href}" is not a supported image or video type`);
    state.media.add(key);
    return { key, file };
  };

  const isExternal = (href) => /^(https?:)?\/\//i.test(href);

  /** A link or media address as it should appear in the generated page. */
  const resolveHref = (href) => {
    if (!href) return '';
    if (/^(mailto:|tel:)/i.test(href) || href.startsWith('#')) return href;
    if (isExternal(href)) return href.startsWith('//') ? `https:${href}` : href;
    if (/^[a-z][a-z0-9+.-]*:/i.test(href)) {
      state.problems.push(`${state.post.file}: links of the form "${href}" are not allowed`);
      return '#';
    }
    if (href.startsWith('/')) {
      // A site address, written from the site root: /play/mods/ or /blog/other-post/.
      const [path, anchor] = href.slice(1).split('#');
      if (
        !context.siteRoutes.has(path.replace(/index\.html$/, '')) &&
        !existsSync(resolve(root, 'public', path))
      )
        state.problems.push(`${state.post.file}: no page on the site at "${href}"`);
      return `${'../'.repeat(2)}${path}${anchor ? `#${anchor}` : ''}`;
    }
    // A relative link: another post (../other-post/ or ../other-post/index.md) or a file in this folder.
    const [path, anchor] = href.split('#');
    const target = posix
      .normalize(posix.join(`blog/${state.post.slug}/`, path))
      .replace(/index\.md$/, '');
    const otherPost = /^blog\/([a-z0-9-]+)\/?$/.exec(target);
    if (otherPost) {
      if (!context.slugs.has(otherPost[1]))
        state.problems.push(`${state.post.file}: no post named "${otherPost[1]}" (link "${href}")`);
      return `../${otherPost[1]}/${anchor ? `#${anchor}` : ''}`;
    }
    if (target === 'blog' || target === 'blog/') return `../${anchor ? `#${anchor}` : ''}`;
    if (target.startsWith(`blog/${state.post.slug}/`) && path) return localMedia(path)?.key ?? href;
    state.problems.push(
      `${state.post.file}: cannot resolve the link "${href}"; use ../SLUG/ for a post or /PATH/ for a site page`,
    );
    return href;
  };

  const figure = (inner, caption) =>
    `<figure class="post-media">${inner}${caption ? `<figcaption>${caption}</figcaption>` : ''}</figure>\n`;

  /** One image or video, from Markdown image syntax: ![alt text](address "optional caption"). */
  const media = ({ href, title, text }, block) => {
    const alt = unescapeHtml(text || '');
    const caption = title ? marked.parseInline(unescapeHtml(title)) : '';
    const kind = mediaKind(href);
    state.figures++;
    if (kind === 'embed') {
      const embed = videoEmbed(href);
      const html = `<div class="post-embed"><iframe src="${escapeHtml(embed.src)}" title="${escapeHtml(alt || `${embed.provider} video`)}" loading="lazy" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe></div>`;
      return block ? figure(html, caption) : html;
    }
    const local = !isExternal(href) && !href.startsWith('/') ? localMedia(href) : null;
    const src = local ? local.key : resolveHref(href);
    if (kind === 'video') {
      const ext = extname(href.split(/[?#]/)[0]).toLowerCase();
      const type = {
        '.mp4': 'video/mp4',
        '.m4v': 'video/mp4',
        '.webm': 'video/webm',
        '.ogv': 'video/ogg',
      }[ext];
      const html = `<video class="post-video" controls preload="metadata" playsinline${alt ? ` aria-label="${escapeHtml(alt)}"` : ''}><source src="${escapeHtml(src)}" type="${type}" />${escapeHtml(alt || 'Your browser cannot play this video.')}</video>`;
      return block ? figure(html, caption) : html;
    }
    let size = '';
    const dims = local && imageSize(readFileSync(local.file));
    if (dims) size = ` width="${dims.width}" height="${dims.height}"`;
    if (!alt)
      state.problems.push(
        `${state.post.file}: image "${href}" needs alt text: ![describe the image](${href})`,
      );
    const html = `<img src="${escapeHtml(src)}" alt="${escapeHtml(alt)}"${size} loading="lazy" decoding="async" />`;
    return block ? figure(html, caption) : html;
  };

  marked.use({
    renderer: {
      heading({ tokens, depth, text }) {
        const level = Math.min(Math.max(depth, 2), 4);
        let id = slugify(text);
        const seen = state.ids.get(id) ?? 0;
        state.ids.set(id, seen + 1);
        if (seen) id = `${id}-${seen}`;
        return `<h${level} id="${id}">${this.parser.parseInline(tokens)}</h${level}>\n`;
      },
      paragraph(token) {
        // A paragraph that only holds images or videos becomes figures between paragraphs.
        const parts = token.tokens.filter(
          (t) => !(t.type === 'text' && !t.text.trim()) && t.type !== 'br',
        );
        if (parts.length && parts.every((t) => t.type === 'image'))
          return parts.map((t) => media(t, true)).join('');
        return `<p>${this.parser.parseInline(token.tokens)}</p>\n`;
      },
      image(token) {
        return media(token, false);
      },
      link({ href, title, tokens }) {
        const target = resolveHref(href);
        const external = /^https?:/i.test(target);
        return `<a href="${escapeHtml(target)}"${title ? ` title="${escapeHtml(title)}"` : ''}${external ? ' rel="noopener"' : ''}>${this.parser.parseInline(tokens)}</a>`;
      },
      code({ text, lang }) {
        const language = (lang || '').trim().split(/\s+/)[0] || '';
        return `<div class="post-code"><pre tabindex="0"><code class="language-${escapeHtml(language || 'text')}">${highlight(text.replace(/\n$/, ''), language)}</code></pre></div>\n`;
      },
      table(token) {
        return `<div class="post-table" tabindex="0" role="region" aria-label="Table">${base.table.call(this, token)}</div>\n`;
      },
      blockquote({ tokens }) {
        return `<blockquote>\n${this.parser.parse(tokens)}</blockquote>\n`;
      },
      hr() {
        return '<hr />\n';
      },
      html({ text }) {
        const trimmed = text.trim();
        if (/^<!--[\s\S]*-->$/.test(trimmed)) return '';
        return INLINE_TAG.test(trimmed) ? trimmed : escapeHtml(text);
      },
    },
  });
  return { marked, state };
}

/* ---------- Pages ---------- */

const ROOT = '@@ROOT@@';
const CONTENT = '@@CONTENT@@';
const HEAD = '@@HEAD@@';

function shell(template, copy, depth, content, head = '') {
  return renderPageText(template, copy, null)
    .replace(HEAD, () => head)
    .replace(CONTENT, () => content)
    .replaceAll(ROOT, '../'.repeat(depth));
}

/** Builds every blog page in memory. Returns { files, media, posts, problems }. */
export function buildBlog({ drafts } = {}) {
  const { posts, problems } = loadPosts({ drafts });
  const copy = JSON.parse(readFileSync(resolve(blogDir, 'blog.json'), 'utf8'));
  const text = copy.text.blog;
  const indexTemplate = readFileSync(resolve(templateDir, 'index.html'), 'utf8');
  const postTemplate = readFileSync(resolve(templateDir, 'post.html'), 'utf8');
  const pages = JSON.parse(readFileSync(resolve(root, 'content/pages.json'), 'utf8')).map(
    (p) => p.path,
  );
  // Asked from the docs renderer, so this works before the docs are generated (as in CI).
  const docRoutes = listDocRoutes();
  const context = {
    problems,
    slugs: new Set(posts.map((p) => p.slug)),
    siteRoutes: new Set(['', ...pages, 'blog/', ...posts.map((p) => p.path), ...docRoutes]),
  };
  const files = new Map();
  const media = new Map();

  for (const [i, post] of posts.entries()) {
    const { marked, state } = makeMarked(context);
    state.post = post;
    const body = marked.parse(post.body.replace(/^\s*#\s+.*\n/, ''));
    for (const key of state.media) media.set(`${post.path}${key}`, resolve(post.folder, key));
    const newer = posts[i - 1];
    const older = posts[i + 1];
    const pager = [
      older
        ? `<a class="post-pager-older" href="../${older.slug}/" rel="prev"><span>${escapeHtml(text.older)}</span><strong>${escapeHtml(older.title)}</strong></a>`
        : '<span></span>',
      newer
        ? `<a class="post-pager-newer" href="../${newer.slug}/" rel="next"><span>${escapeHtml(text.newer)}</span><strong>${escapeHtml(newer.title)}</strong></a>`
        : '<span></span>',
    ].join('');
    const meta = [
      `<time datetime="${post.date}">${formatDate(post.date)}</time>`,
      post.author ? escapeHtml(post.author) : '',
      escapeHtml(text.readingTime.replace('{n}', readingMinutes(post.body))),
    ]
      .filter(Boolean)
      .join('<span aria-hidden="true"> · </span>');
    const content = `<article class="blog-post" aria-labelledby="post-title">
      <div class="page-heading blog-post-heading">
        <p class="breadcrumb-trail"><a class="breadcrumb" href="${ROOT}">${escapeHtml(text.home)}</a> <span aria-hidden="true">/</span> <a class="breadcrumb" href="../">${escapeHtml(text.title)}</a></p>
        <p class="eyebrow">${escapeHtml(text.eyebrow)}</p>
        <h1 id="post-title">${escapeHtml(post.title)}</h1>
        <p class="blog-post-summary">${escapeHtml(post.summary)}</p>
        <p class="blog-post-meta">${meta}</p>
      </div>
      <div class="content blog-post-body">
        <div class="section-body post-prose">
${body}
        </div>
      </div>
      <nav class="post-pager" aria-label="${escapeHtml(text.pagerLabel)}">${pager}</nav>
      <p class="post-back"><a class="text-link" href="../">${escapeHtml(text.back)}</a></p>
    </article>`;
    const pageCopy = {
      ...copy,
      metadata: { title: `${post.title} — ${text.title}`, description: post.summary },
    };
    const head = [
      shareTags(post.path, { type: 'article', image: post.image && `${post.path}${post.image}` }),
      `<meta property="article:published_time" content="${post.date}" />`,
    ]
      .filter(Boolean)
      .join('\n    ');
    files.set(`${post.path}index.html`, shell(postTemplate, pageCopy, 2, content, head));
  }

  const list = posts.length
    ? posts
        .map(
          (post) => `<li><article class="blog-entry">
          <p class="blog-entry-date"><time datetime="${post.date}">${formatDate(post.date)}</time></p>
          <h2><a href="${post.slug}/">${escapeHtml(post.title)}</a></h2>
          <p>${escapeHtml(post.summary)}</p>
          <a class="text-link blog-entry-more" href="${post.slug}/" aria-hidden="true" tabindex="-1">${escapeHtml(text.readMore)}</a>
        </article></li>`,
        )
        .join('\n')
    : `<li><p>${escapeHtml(text.empty)}</p></li>`;
  const indexContent = `<div class="page-heading">
        <a class="breadcrumb" href="${ROOT}">${escapeHtml(text.home)} /</a>
        <p class="eyebrow">${escapeHtml(text.eyebrow)}</p>
        <h1>${escapeHtml(text.title)}</h1>
        <p>${escapeHtml(text.intro)}</p>
      </div>
      <section class="content blog-list-section" aria-labelledby="posts-heading">
        <div class="section-label" id="posts-heading">${escapeHtml(text.listLabel)}</div>
        <div class="section-body"><ol class="blog-list" reversed>${list}</ol></div>
      </section>`;
  const indexCopy = {
    ...copy,
    metadata: { title: `${text.title} — Nebula`, description: text.intro },
  };
  files.set(
    'blog/index.html',
    shell(indexTemplate, indexCopy, 1, indexContent, shareTags('blog/')),
  );
  return { files, media, posts, problems };
}

export async function renderBlog({ strict = true } = {}) {
  const { files, media, posts, problems } = buildBlog();
  if (problems.length && strict)
    throw new Error(`Blog has ${problems.length} problem(s):\n  ${problems.join('\n  ')}`);
  if (problems.length)
    console.warn(`Blog: ${problems.length} problem(s):\n  ${problems.join('\n  ')}`);
  const wanted = new Set([
    ...[...files.keys()].map((p) => resolve(root, p)),
    ...[...media.keys()].map((p) => resolve(root, p)),
  ]);
  const removeStale = (dir) => {
    if (!existsSync(dir)) return;
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = resolve(dir, entry.name);
      if (entry.isDirectory()) {
        removeStale(path);
        if (!readdirSync(path).length) rmSync(path, { recursive: true });
      } else if (!wanted.has(path)) rmSync(path);
    }
  };
  removeStale(outDir);
  let written = 0;
  for (const [path, html] of files) {
    const file = resolve(root, path);
    if (existsSync(file) && readFileSync(file, 'utf8') === html) continue;
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, html);
    written++;
  }
  for (const [path, source] of media) {
    const file = resolve(root, path);
    if (
      existsSync(file) &&
      statSync(file).size === statSync(source).size &&
      statSync(file).mtimeMs >= statSync(source).mtimeMs
    )
      continue;
    mkdirSync(dirname(file), { recursive: true });
    copyFileSync(source, file);
    written++;
  }
  console.log(
    `Rendered the blog: ${posts.length} post(s), ${media.size} media file(s) (${written} files changed).`,
  );
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await renderBlog();
}
