/**
 * Renders the modding docs (docs/modding/**.md at the repository root) into static pages under
 * modding/docs/. The docs were first published at docs/; every page there forwards to its new address.
 *
 * The source folder keeps the layout of the documentation repository: get-started/, concepts/,
 * how-tos/, tutorials/ and reference/ (with reference/lumascript/, reference/lumakit/,
 * reference/services/ and reference/lumalink-commands/). sections.json maps those folders to
 * sidebar sections. Links between pages are relative Markdown links, as on GitHub; every link
 * and heading anchor is checked, and a broken one stops the build.
 */
import { readFileSync, readdirSync, existsSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { resolve, dirname, posix } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateRawSync } from 'node:zlib';
import { Marked, Renderer } from 'marked';
import { shareTags } from './lib/site.mjs';
import { highlight } from './lib/highlight.mjs';

const root = resolve(import.meta.dirname, '..');
// The modding docs live in the repository's docs/modding/, beside the code they describe.
const docsDir = resolve(root, '../docs/modding');
// Where the docs sit on the site, and where they sat first.
export const DOCS = 'modding/docs/';
const FIRST_DOCS = 'docs/';
const DOCS_DEPTH = DOCS.split('/').filter(Boolean).length;
const outDirs = ['modding', 'docs'].map((dir) => resolve(root, dir));
const publicDir = resolve(root, 'public', DOCS);
const indexFile = resolve(publicDir, 'search-index.json');
const examplesSource = resolve(docsDir, 'examples');
const examplesOut = resolve(publicDir, 'examples');

const escapeHtml = (value) =>
  String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');

function plainText(markdown) {
  return String(markdown)
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/<\/?(kbd|br|sup|sub)\s*\/?>/gi, '')
    .replace(/[`*]/g, '')
    .replace(/\\([\\`*_{}[\]()#+\-.!<>|])/g, '$1')
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .trim();
}

/** Heading anchors follow GitHub's rule, so links written for GitHub work here too. */
export const slugify = (text) =>
  plainText(text)
    .toLowerCase()
    .replace(/[^\p{L}\p{M}\p{N}\p{Pc} -]/gu, '')
    .replace(/ /g, '-') || 'section';

function uniqueId(ids, base) {
  const seen = ids.get(base) ?? 0;
  ids.set(base, seen + 1);
  return seen ? `${base}-${seen}` : base;
}

function parseFrontMatter(source, file) {
  const match = /^---\r?\n([\s\S]*?)\r?\n---\r?\n?/.exec(source);
  if (!match) throw new Error(`${file}: missing front matter (--- title: ... ---)`);
  const data = {};
  let listKey = null;
  for (const line of match[1].split(/\r?\n/)) {
    if (!line.trim() || line.trim().startsWith('#')) continue;
    const item = /^\s+-\s+(.*)$/.exec(line);
    if (item && listKey) {
      data[listKey].push(item[1].trim());
      continue;
    }
    const colon = line.indexOf(':');
    if (colon < 0) throw new Error(`${file}: front matter line without a colon: ${line}`);
    const key = line.slice(0, colon).trim();
    let value = line.slice(colon + 1).trim();
    if (!value) {
      data[key] = [];
      listKey = key;
      continue;
    }
    listKey = null;
    if (/^(['"]).*\1$/.test(value)) value = value.slice(1, -1);
    data[key] = value;
  }
  return { data, body: source.slice(match[0].length) };
}

const readDoc = (key) => parseFrontMatter(readFileSync(resolve(docsDir, key), 'utf8'), key);

/** Links in a landing page's body, in order: the reading order of that section. */
function linkedPages(body, dir) {
  const order = [];
  for (const [, href] of body.matchAll(/\]\(([^)#\s]+\.md)(?:#[^)]*)?\)/g)) {
    const key = posix.normalize(posix.join(dir, href));
    if (posix.dirname(key) === dir && !key.endsWith('/index.md') && !order.includes(key))
      order.push(key);
  }
  return order;
}

/** Reads every section and page. Synchronous so vite.config.js can list the routes. */
export function loadDocs() {
  const config = JSON.parse(readFileSync(resolve(docsDir, 'sections.json'), 'utf8'));
  const site = JSON.parse(readFileSync(resolve(docsDir, 'site.json'), 'utf8'));
  const problems = [];
  const pages = [];
  const groups = config.groups.map((g) => ({ ...g, sections: [] }));
  const groupById = new Map(groups.map((g) => [g.id, g]));

  for (const group of groups) {
    if (!group.landing) continue;
    const { data, body } = readDoc(group.landing);
    Object.assign(group, {
      key: group.landing,
      title: data.title,
      navTitle: group.title,
      description: data.intro || '',
      body,
      path: `${DOCS}${group.id}/`,
      depth: DOCS_DEPTH + 1,
      kind: 'group',
    });
  }

  const claimed = new Set();
  for (const spec of config.sections) {
    const group = groupById.get(spec.group);
    if (!group) {
      problems.push(`sections.json: ${spec.id} names unknown group ${spec.group}`);
      continue;
    }
    const indexKey = `${spec.dir}/index.md`;
    const section = {
      ...spec,
      group,
      kind: 'section',
      path: `${DOCS}${spec.id}/`,
      depth: DOCS_DEPTH + 1,
      pages: [],
      body: '',
    };
    if (spec.pages) {
      section.title = spec.title;
      section.navTitle = spec.shortTitle || spec.title;
      section.description = spec.intro;
      section.body = spec.about ? `${spec.about}\n` : '';
      section.order = spec.pages.map((p) => `${spec.dir}/${p}`);
    } else {
      const { data, body } = readDoc(indexKey);
      section.key = indexKey;
      section.title = data.title;
      section.navTitle = spec.shortTitle || data.shortTitle || data.title;
      section.description = data.intro || '';
      section.body = body;
      section.order = [
        ...(data.children || []).map((c) => `${spec.dir}${c}.md`),
        ...linkedPages(body, spec.dir),
      ];
      section.order = [...new Set(section.order)];
    }
    const folder = resolve(docsDir, spec.dir);
    const inFolder = readdirSync(folder)
      .filter((f) => f.endsWith('.md') && f !== 'index.md')
      .map((f) => `${spec.dir}/${f}`);
    let keys;
    if (spec.pages) {
      keys = section.order;
    } else {
      const rest = inFolder.filter((k) => !section.order.includes(k) && !claimed.has(k));
      keys = [...section.order.filter((k) => inFolder.includes(k)), ...rest.sort()];
    }
    for (const key of keys) {
      if (claimed.has(key)) {
        problems.push(`${key} belongs to two sections`);
        continue;
      }
      claimed.add(key);
      try {
        const { data, body } = readDoc(key);
        for (const field of ['title', 'intro'])
          if (!data[field]) problems.push(`${key}: front matter needs "${field}"`);
        const slug = posix.basename(key, '.md');
        const page = {
          key,
          kind: 'page',
          section,
          slug,
          title: data.title || slug,
          navTitle: data.shortTitle || data.title || slug,
          description: data.intro || '',
          contentType: data.contentType || 'reference',
          example: data.example,
          body,
          path: `${DOCS}${spec.id}/${slug}/`,
          depth: DOCS_DEPTH + 2,
        };
        if (page.example && !existsSync(resolve(examplesSource, page.example)))
          problems.push(`${key}: example "${page.example}" is not in docs/modding/examples`);
        section.pages.push(page);
      } catch (error) {
        problems.push(error.message);
      }
    }
    group.sections.push(section);
  }

  // Every Markdown file must have a place in the sidebar.
  const landings = new Set([
    'index.md',
    ...groups.filter((g) => g.key).map((g) => g.key),
    ...groups
      .flatMap((g) => g.sections)
      .filter((s) => s.key)
      .map((s) => s.key),
  ]);
  const walk = (dir) =>
    readdirSync(resolve(docsDir, dir), { withFileTypes: true }).flatMap((e) =>
      e.isDirectory()
        ? e.name === 'examples' && !dir
          ? []
          : walk(dir ? `${dir}/${e.name}` : e.name)
        : e.name.endsWith('.md') && e.name !== 'README.md'
          ? [dir ? `${dir}/${e.name}` : e.name]
          : [],
    );
  for (const key of walk(''))
    if (!claimed.has(key) && !landings.has(key)) problems.push(`${key} is in no section`);

  const sections = groups.flatMap((g) => g.sections);
  for (const group of groups) {
    const chain = group.sections.flatMap((s) => s.pages);
    chain.forEach((page, i) => {
      page.previous = chain[i - 1];
      page.next = chain[i + 1];
    });
    pages.push(...chain);
  }
  const hubDoc = readDoc('index.md');
  const hub = {
    key: 'index.md',
    kind: 'hub',
    title: hubDoc.data.title,
    description: hubDoc.data.intro,
    body: hubDoc.body,
    path: DOCS,
    depth: DOCS_DEPTH,
  };
  return { groups, sections, site, pages, hub, problems };
}

/** Forwarding pages, from address to address (both from the site root): the moves listed in
 *  redirects.json, every page's first address under docs/, and modding/ to the docs' front page. */
function redirects(real) {
  const file = resolve(docsDir, 'redirects.json');
  const moved = existsSync(file) ? JSON.parse(readFileSync(file, 'utf8')) : {};
  const list = Object.entries(moved).map(([from, to]) => [`${DOCS}${from}`, `${DOCS}${to}`]);
  for (const path of real) list.push([`${FIRST_DOCS}${path.slice(DOCS.length)}`, path]);
  list.push([`${DOCS.split('/')[0]}/`, DOCS]);
  return list.filter(([from]) => !real.includes(from));
}

/** Output folders (relative to the site root) for vite.config.js. */
/** Every real docs page's address, without the redirect pages. */
export function docPages() {
  const { groups, sections, pages } = loadDocs();
  return [
    ...new Set([
      DOCS,
      ...groups.filter((g) => g.path && g.key).map((g) => g.path),
      ...sections.map((s) => s.path),
      ...pages.map((p) => p.path),
    ]),
  ];
}

/** Every address the docs build writes: the pages, then the redirects from old addresses. */
export function docRoutes() {
  const real = docPages();
  return [...real, ...redirects(real).map(([from]) => from)];
}

const rel = (depth) => '../'.repeat(depth);
const INLINE_HTML = /^<\/?(kbd|br|sup|sub)\s*\/?>$/i;

function makeMarked() {
  const marked = new Marked({ gfm: true });
  const base = new Renderer();
  const state = { doc: null, ids: null, terms: null, resolveLink: null };
  marked.use({
    renderer: {
      heading({ tokens, depth, text }) {
        const inner = this.parser.parseInline(tokens);
        const id = uniqueId(state.ids, slugify(text));
        const level = Math.min(depth === 1 ? 2 : depth, 6);
        return `<h${level} id="${id}"><a class="anchor" href="#${id}" aria-label="Link to this section" tabindex="-1">#</a>${inner}</h${level}>\n`;
      },
      code({ text, lang }) {
        const [language = '', ...rest] = (lang || '').trim().split(/\s+/);
        const caption = rest.join(' ');
        const names = {
          lumascript: 'LumaScript',
          sh: 'Shell',
          shell: 'Shell',
          c: 'C',
          cpp: 'C++',
          json: 'JSON',
          text: 'Text',
          hlsl: 'HLSL',
          xml: 'XML',
          toml: 'TOML',
          lua: 'Lua',
        };
        const label = caption || names[language.toLowerCase()] || language;
        return `<figure class="code-block">${label ? `<figcaption>${escapeHtml(label)}</figcaption>` : ''}<pre tabindex="0"><code class="language-${escapeHtml(language || 'text')}">${highlight(text.replace(/\n$/, ''), language)}</code></pre></figure>\n`;
      },
      codespan({ text }) {
        const raw = text
          .replace(/&amp;/g, '&')
          .replace(/&lt;/g, '<')
          .replace(/&gt;/g, '>')
          .replace(/&quot;/g, '"')
          .replace(/&#39;/g, "'");
        if (state.terms && raw.length > 2 && raw.length < 48) state.terms.add(raw);
        return `<code>${escapeHtml(raw)}</code>`;
      },
      blockquote({ tokens }) {
        let html = this.parser.parse(tokens);
        const alert = /^<p>\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION|PLANNED)\]\s*/i.exec(html);
        if (!alert) return `<blockquote>\n${html}</blockquote>\n`;
        const kind = alert[1].toLowerCase();
        html = html.slice(alert[0].length).replace(/^<\/p>\s*/, '');
        if (!html.startsWith('<')) html = `<p>${html}`;
        const titles = {
          note: 'Note',
          tip: 'Tip',
          important: 'Important',
          warning: 'Warning',
          caution: 'Caution',
          planned: 'Planned',
        };
        return `<div class="callout callout-${kind}" role="note"><p class="callout-title">${titles[kind]}</p>${html}</div>\n`;
      },
      table(token) {
        return `<div class="table-scroll" tabindex="0" role="region" aria-label="Table">${base.table.call(this, token)}</div>\n`;
      },
      link({ href, title, tokens }) {
        const inner = this.parser.parseInline(tokens);
        const target = state.resolveLink(href);
        const external = /^https?:/.test(target);
        const download = /\.zip$/.test(target) ? ' download' : '';
        return `<a href="${escapeHtml(target)}"${title ? ` title="${escapeHtml(title)}"` : ''}${external ? ' rel="noopener"' : ''}${download}>${inner}</a>`;
      },
      html({ text }) {
        return INLINE_HTML.test(text.trim()) ? text.trim() : escapeHtml(text);
      },
    },
  });
  return { marked, state };
}

function collectHeadings(marked, body) {
  const ids = new Map();
  const headings = [];
  const visit = (tokens) => {
    for (const token of tokens) {
      if (token.type === 'heading') {
        headings.push({
          depth: token.depth,
          id: uniqueId(ids, slugify(token.text)),
          text: plainText(token.text),
        });
      }
    }
  };
  visit(marked.lexer(body));
  return headings;
}

/* ---------- Page chrome ---------- */

function topbar(site, depth) {
  const r = rel(depth);
  const h = site.header;
  return `<a class="docs-skip" href="#main-content">${escapeHtml(h.skip)}</a>
<header class="docs-topbar"><button class="docs-menu-toggle" type="button" aria-expanded="false" aria-controls="docs-sidebar" aria-label="${escapeHtml(site.docs.browse)}"><svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true"><path d="M3 6h18M3 12h18M3 18h18" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></button><div class="docs-brand"><a class="docs-brand-home" href="${r}" aria-label="${escapeHtml(h.homeLink)}"><img class="docs-brand-logo" src="${r}brand-logo.png" alt="" width="26" height="26" /> ${escapeHtml(h.brand)}</a><a class="docs-brand-sub" href="${r}${DOCS}">${escapeHtml(h.brandSub)}</a></div>${searchBox(site, depth, 'top')}<a class="docs-topbar-link" href="${r}">${escapeHtml(h.homeLink)}</a></header>`;
}

function documentShell({ site, depth, title, description, path, bodyClass, sidebarHtml, content }) {
  const r = rel(depth);
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
<meta name="theme-color" content="#070d1f" />
<link rel="icon" href="${r}favicon.ico" sizes="any" />
<link rel="icon" type="image/png" href="${r}favicon-32.png" />
<link rel="apple-touch-icon" href="${r}apple-touch-icon.png" />
<title>${escapeHtml(title)}</title>
<meta name="description" content="${escapeHtml(description)}" />
${shareTags(path, { type: path === DOCS ? 'website' : 'article' })}
<meta property="og:title" content="${escapeHtml(title)}" />
<meta property="og:description" content="${escapeHtml(description)}" />
<link rel="preload" href="${r}fonts/Delfino.woff2" as="font" type="font/woff2" crossorigin />
<link rel="stylesheet" href="${r}docs.css" />
</head>
<body class="docs ${bodyClass}">
${topbar(site, depth)}
<div class="docs-layout">
${sidebarHtml}
<div class="docs-main">
${content}
<footer class="docs-footer"><p>${escapeHtml(site.footer.legal)}</p></footer>
</div>
</div>
<script type="module" src="${r}docs.js"></script>
</body>
</html>
`;
}

function searchBox(site, depth, id) {
  const r = rel(depth);
  return `<div class="docs-search docs-search-${id}" role="search" data-index="${r}${DOCS}search-index.json" data-docs="${r}${DOCS}"><label class="sr-only" for="docs-search-${id}">${escapeHtml(site.docs.searchLabel)}</label><input id="docs-search-${id}" type="search" placeholder="${escapeHtml(site.docs.searchPlaceholder)}" autocomplete="off" spellcheck="false" enterkeyhint="go" aria-controls="docs-results-${id}" aria-autocomplete="list" /><kbd class="docs-search-key" aria-hidden="true">/</kbd><ul class="docs-results" id="docs-results-${id}" role="listbox" aria-label="${escapeHtml(site.docs.searchResults)}" hidden></ul><p class="docs-results-empty" role="status" hidden>${escapeHtml(site.docs.searchEmpty)}</p></div>`;
}

function sidebar({ site, groups, depth, current }) {
  const r = rel(depth);
  const here = (doc) => (doc === current ? ' aria-current="page"' : '');
  const currentSection =
    current?.kind === 'page' ? current.section : current?.kind === 'section' ? current : null;
  const nav = groups
    .map((group) => {
      const heading = group.key
        ? `<a class="docs-nav-heading" href="${r}${group.path}"${here(group)}>${escapeHtml(group.navTitle)}</a>`
        : `<span class="docs-nav-heading">${escapeHtml(group.title)}</span>`;
      const items = group.sections
        .map((s) => {
          const open = s === currentSection ? ' open' : '';
          const links = s.pages
            .map((p) => `<li><a href="${r}${p.path}"${here(p)}>${escapeHtml(p.navTitle)}</a></li>`)
            .join('');
          return `<details class="docs-nav-group"${open}><summary><span class="docs-nav-icon" aria-hidden="true">${escapeHtml(s.icon || '✦')}</span><span>${escapeHtml(s.navTitle)}</span></summary><ul><li><a class="docs-nav-overview" href="${r}${s.path}"${here(s)}>${escapeHtml(site.docs.sectionOverview)}</a></li>${links}</ul></details>`;
        })
        .join('');
      return `<div class="docs-nav-block">${heading}${items}</div>`;
    })
    .join('');
  return `<aside class="docs-sidebar" id="docs-sidebar" aria-label="${escapeHtml(site.docs.sidebarLabel)}">
<div class="docs-sidebar-top"><a class="docs-home-link" href="${r}${DOCS}"${current?.kind === 'hub' ? ' aria-current="page"' : ''}>${escapeHtml(site.docs.home)}</a><button class="docs-close" type="button" aria-controls="docs-sidebar" aria-label="${escapeHtml(site.docs.close)}"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></button></div>
<nav class="docs-nav" aria-label="${escapeHtml(site.docs.sidebarLabel)}">${nav}</nav>
</aside>`;
}

function breadcrumb(site, doc) {
  const r = rel(doc.depth);
  const crumbs = [[site.docs.home, DOCS]];
  const section = doc.kind === 'page' ? doc.section : doc.kind === 'section' ? doc : null;
  const group = section?.group || (doc.kind === 'group' ? doc : null);
  if (group?.key && doc !== group) crumbs.push([group.navTitle, group.path]);
  if (section && doc !== section) crumbs.push([section.navTitle, section.path]);
  const links = crumbs.map(
    ([text, path]) => `<li><a href="${r}${path}">${escapeHtml(text)}</a></li>`,
  );
  links.push(`<li><span aria-current="page">${escapeHtml(doc.navTitle || doc.title)}</span></li>`);
  return `<nav class="docs-breadcrumb" aria-label="Breadcrumb"><ol>${links.join('')}</ol></nav>`;
}

function card(depth, doc, extra = '') {
  return `<a class="docs-card" href="${rel(depth)}${doc.path}"><strong>${escapeHtml(doc.title)}</strong><span class="docs-card-text">${escapeHtml(doc.description)}</span>${extra}</a>`;
}

function exampleLink(site, depth, name, size = 0) {
  const kb = size
    ? ` <span class="docs-download-size">${size > 1048576 ? `${(size / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.round(size / 1024))} KB`}</span>`
    : '';
  return `<a class="docs-download" href="${rel(depth)}${DOCS}examples/${escapeHtml(name)}.zip" download><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M12 4v11m0 0l-4.5-4.5M12 15l4.5-4.5M5 19h14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>${escapeHtml(site.docs.downloadExample)}${kb}</a>`;
}

function toc(site, headings) {
  let list = headings.filter((h) => h.depth === 2 || h.depth === 3);
  if (list.length > 48) list = list.filter((h) => h.depth === 2);
  if (list.length < 2) return '';
  return `<nav class="docs-toc" aria-labelledby="toc-title"><details open><summary class="docs-toc-title" id="toc-title">${escapeHtml(site.docs.onThisPage)}</summary><ol>${list.map((h) => `<li class="toc-${h.depth}"><a href="#${h.id}">${escapeHtml(h.text)}</a></li>`).join('')}</ol></details></nav>`;
}

function renderBody(ctx, doc, body) {
  const { marked, state } = ctx;
  state.doc = doc;
  state.ids = new Map();
  state.terms = new Set();
  const html = marked.parse(body);
  doc.terms = [...state.terms];
  return html;
}

function renderPage(ctx, page) {
  const { site, groups } = ctx;
  const depth = page.depth;
  const html = renderBody(ctx, page, page.body);
  const tocHtml = toc(site, page.headings);
  const pager = [
    page.previous
      ? `<a class="docs-pager-prev" href="${rel(depth)}${page.previous.path}" rel="prev"><span>${escapeHtml(site.docs.previous)}</span><strong>${escapeHtml(page.previous.navTitle)}</strong></a>`
      : '<span></span>',
    page.next
      ? `<a class="docs-pager-next" href="${rel(depth)}${page.next.path}" rel="next"><span>${escapeHtml(site.docs.next)}</span><strong>${escapeHtml(page.next.navTitle)}</strong></a>`
      : '<span></span>',
  ].join('');
  const content = `<div class="docs-page-grid${tocHtml ? '' : ' no-toc'}">
<div class="docs-head">${breadcrumb(site, page)}<h1>${escapeHtml(page.title)}</h1><p class="docs-lead">${escapeHtml(page.description)}</p>${page.example ? `<p class="docs-meta">${exampleLink(site, depth, page.example, ctx.zips.get(page.example)?.length)}</p>` : ''}</div>
${tocHtml}
<main id="main-content" class="docs-article" tabindex="-1">
${html}
</main>
<nav class="docs-pager" aria-label="${escapeHtml(site.docs.pagerLabel)}">${pager}</nav>
</div>`;
  return documentShell({
    site,
    depth,
    title: page.title + site.metadata.titleSuffix,
    description: page.description,
    path: page.path,
    bodyClass: `docs-article-page docs-section-${page.section.id} docs-type-${page.contentType}`,
    sidebarHtml: sidebar({ site, groups, depth, current: page }),
    content,
  });
}

/**
 * A landing page: its own introduction, then its articles as cards. A table whose first column is
 * "Article", "Tutorial" or "Section" lists child pages, and becomes a grid of cards in that order;
 * other tables stay as they are. Children the body does not link to are added at the end.
 */
const CARD_TABLE = /^(articles?|tutorials?|pages?|sections?)$/i;
function renderLanding(ctx, doc, children) {
  const { site, groups, marked, state, landingsByKey, byKey } = ctx;
  const depth = doc.depth;
  const from = doc.key ? posix.dirname(doc.key) : doc.dir;
  const lookup = (href) => {
    const key = posix.normalize(posix.join(from, href.split('#')[0]));
    return byKey.get(key) || landingsByKey.get(key);
  };
  const grid = (list) =>
    `<div class="docs-card-grid">${list.map((c) => card(depth, c)).join('')}</div>\n`;
  state.doc = doc;
  state.ids = new Map();
  state.terms = new Set();
  const linked = new Set();
  for (const [, href] of (doc.body || '').matchAll(/\]\(([^)\s]+\.md)(?:#[^)]*)?\)/g)) {
    const target = lookup(href);
    if (target) linked.add(target);
  }
  const tablePattern = /(^|\n)(\|[^\n]*\|)\n\|[-:| ]+\|\n((?:\|[^\n]*\|(?:\n|$))+)/g;
  let html = '';
  let last = 0;
  let cardTables = 0;
  const body = doc.body || '';
  for (const match of body.matchAll(tablePattern)) {
    const firstColumn = match[2].split('|')[1].trim();
    if (!CARD_TABLE.test(firstColumn)) continue;
    html += marked.parse(body.slice(last, match.index));
    const rows = [...match[3].matchAll(/\]\(([^)\s]+\.md)(?:#[^)]*)?\)/g)]
      .map((m) => lookup(m[1]))
      .filter(Boolean);
    html += grid(rows);
    cardTables++;
    last = match.index + match[0].length;
  }
  let rest = body.slice(last);
  const [beforeFurther, further = ''] = rest.split(/\n(?=## Further reading)/);
  html += marked.parse(beforeFurther);
  const unlinked = children.filter((c) => !linked.has(c));
  if (unlinked.length) {
    const heading = cardTables
      ? site.docs.moreArticles
      : doc.kind === 'group'
        ? site.docs.sectionsHeading
        : site.docs.articlesHeading;
    html += `<h2 id="${slugify(heading)}">${escapeHtml(heading)}</h2>\n`;
    html += grid(unlinked);
  }
  if (further) html += marked.parse(further);
  doc.terms = [...state.terms];
  const content = `<div class="docs-page-grid no-toc">
<div class="docs-head">${breadcrumb(site, doc)}<h1>${escapeHtml(doc.title)}</h1><p class="docs-lead">${escapeHtml(doc.description)}</p></div>
<main id="main-content" class="docs-article docs-landing" tabindex="-1">
${html}
</main>
</div>`;
  return documentShell({
    site,
    depth,
    title: doc.title + site.metadata.titleSuffix,
    description: doc.description,
    path: doc.path,
    bodyClass: `docs-landing-page docs-section-${doc.id}`,
    sidebarHtml: sidebar({ site, groups, depth, current: doc }),
    content,
  });
}

function renderHub(ctx) {
  const { site, groups, hub, byKey } = ctx;
  const depth = hub.depth;
  const r = rel(depth);
  const text = site.hub;
  const start = groups.flatMap((g) => g.sections).find((s) => s.id === text.startSection);
  const startCards = (start?.pages || [])
    .slice(0, 3)
    .map(
      (p, i) =>
        `<li><a class="docs-step" href="${r}${p.path}"><span class="docs-step-number" aria-hidden="true">${i + 1}</span><span><strong>${escapeHtml(p.navTitle)}</strong><span class="docs-card-text">${escapeHtml(p.description)}</span></span></a></li>`,
    )
    .join('');
  const sectionCard = (s) =>
    `<article class="docs-section-card"><a class="docs-section-link" href="${r}${s.path}"><span class="docs-section-icon" aria-hidden="true">${escapeHtml(s.icon)}</span><strong>${escapeHtml(s.navTitle)}</strong><span class="docs-card-text">${escapeHtml(s.description)}</span></a><ul>${s.pages
      .slice(0, 4)
      .map((p) => `<li><a href="${r}${p.path}">${escapeHtml(p.navTitle)}</a></li>`)
      .join(
        '',
      )}<li><a class="docs-more" href="${r}${s.path}">${escapeHtml(site.docs.viewAll.replace('{n}', s.pages.length))}</a></li></ul></article>`;
  const bands = groups
    .map((g) => {
      const list = g.sections.filter((s) => s.id !== text.startSection);
      const heading = text.groups[g.id] || { heading: g.title, intro: '' };
      return `<section class="docs-band" aria-labelledby="band-${g.id}"><h2 id="band-${g.id}">${escapeHtml(heading.heading)}</h2>${heading.intro ? `<p class="docs-band-intro">${escapeHtml(heading.intro)}</p>` : ''}<div class="docs-section-grid">${list.map(sectionCard).join('')}</div>${g.key ? `<p class="docs-band-more"><a class="docs-more-link" href="${r}${g.path}">${escapeHtml(text.groups[g.id]?.more || g.title)}</a></p>` : ''}</section>`;
    })
    .join('');
  const popular = text.popular
    .map((key) => byKey.get(key))
    .filter(Boolean)
    .map(
      (p) =>
        `<li><a href="${r}${p.path}">${escapeHtml(p.navTitle.length > 12 ? p.navTitle : p.title)}</a></li>`,
    )
    .join('');
  const content = `<main id="main-content" class="docs-hub" tabindex="-1">
<section class="docs-hero"><h1>${escapeHtml(hub.title)}</h1><p class="docs-lead">${escapeHtml(hub.description)}</p>
${searchBox(site, depth, 'hub')}
${popular ? `<div class="docs-popular"><h2 id="popular">${escapeHtml(text.popularLabel)}</h2><ul>${popular}</ul></div>` : ''}
<p class="docs-notice"><strong>${escapeHtml(site.docs.preview)}</strong> ${escapeHtml(site.docs.previewText)} <a href="${r}play/mods/">${escapeHtml(text.playerTipLink)}</a></p></section>
${startCards ? `<section class="docs-band" aria-labelledby="band-start"><h2 id="band-start">${escapeHtml(text.startHeading)}</h2><p class="docs-band-intro">${escapeHtml(text.startIntro)}</p><ol class="docs-steps">${startCards}</ol></section>` : ''}
${bands}
</main>`;
  return documentShell({
    site,
    depth,
    title: site.metadata.title,
    description: site.metadata.description,
    path: DOCS,
    bodyClass: 'docs-hub-page',
    sidebarHtml: sidebar({ site, groups, depth, current: hub }),
    content,
  });
}

function renderRedirect(site, from, to) {
  const depth = from.split('/').filter(Boolean).length;
  const target = `${rel(depth)}${to}`;
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>${escapeHtml(site.docs.moved)}</title>
<meta name="robots" content="noindex" />
<link rel="icon" href="${rel(depth)}favicon.ico" sizes="any" />
<meta http-equiv="refresh" content="0; url=${escapeHtml(target)}" />
<link rel="stylesheet" href="${rel(depth)}docs.css" />
</head>
<body class="docs docs-redirect">
<main class="docs-moved"><p>${escapeHtml(site.docs.moved)}</p><p><a href="${escapeHtml(target)}">${escapeHtml(site.docs.movedLink)}</a></p></main>
<script>location.replace(${JSON.stringify(target)} + location.hash);</script>
</body>
</html>
`;
}

/* ---------- Example mods, as .zip files ---------- */

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** A small, deterministic ZIP writer: the same files always give the same bytes. */
export function zipFolder(folder, prefix) {
  const entries = [];
  const walk = (dir, name) => {
    for (const e of readdirSync(dir, { withFileTypes: true }).sort((a, b) =>
      a.name.localeCompare(b.name),
    )) {
      const path = resolve(dir, e.name);
      const entryName = `${name}/${e.name}`;
      if (e.isDirectory()) walk(path, entryName);
      else entries.push({ name: entryName, data: readFileSync(path) });
    }
  };
  walk(folder, prefix);
  const local = [];
  const central = [];
  let offset = 0;
  const DOS_TIME = 0;
  const DOS_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1;
  for (const { name, data } of entries) {
    const nameBytes = Buffer.from(name, 'utf8');
    const deflated = deflateRawSync(data, { level: 9 });
    const store = deflated.length >= data.length;
    const body = store ? data : deflated;
    const crc = crc32(data);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50, 0);
    header.writeUInt16LE(20, 4);
    header.writeUInt16LE(0x0800, 6);
    header.writeUInt16LE(store ? 0 : 8, 8);
    header.writeUInt16LE(DOS_TIME, 10);
    header.writeUInt16LE(DOS_DATE, 12);
    header.writeUInt32LE(crc, 14);
    header.writeUInt32LE(body.length, 18);
    header.writeUInt32LE(data.length, 22);
    header.writeUInt16LE(nameBytes.length, 26);
    header.writeUInt16LE(0, 28);
    local.push(header, nameBytes, body);
    const entry = Buffer.alloc(46);
    entry.writeUInt32LE(0x02014b50, 0);
    entry.writeUInt16LE(20, 4);
    entry.writeUInt16LE(20, 6);
    entry.writeUInt16LE(0x0800, 8);
    entry.writeUInt16LE(store ? 0 : 8, 10);
    entry.writeUInt16LE(DOS_TIME, 12);
    entry.writeUInt16LE(DOS_DATE, 14);
    entry.writeUInt32LE(crc, 16);
    entry.writeUInt32LE(body.length, 20);
    entry.writeUInt32LE(data.length, 24);
    entry.writeUInt16LE(nameBytes.length, 28);
    entry.writeUInt32LE(offset, 42);
    central.push(entry, nameBytes);
    offset += 30 + nameBytes.length + body.length;
  }
  const centralSize = central.reduce((n, b) => n + b.length, 0);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(centralSize, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...local, ...central, end]);
}

function exampleZips() {
  if (!existsSync(examplesSource)) return new Map();
  return new Map(
    readdirSync(examplesSource, { withFileTypes: true })
      .filter((e) => e.isDirectory())
      .map((e) => [e.name, zipFolder(resolve(examplesSource, e.name), e.name)]),
  );
}

/* ---------- Build ---------- */

/** Builds every docs page in memory. Returns { files, index, problems, pages, sections }. */
export function buildDocs() {
  const { groups, sections, site, pages, hub, problems } = loadDocs();
  const { marked, state } = makeMarked();
  const landingsByKey = new Map();
  for (const g of groups) if (g.key) landingsByKey.set(g.key, g);
  for (const s of sections) if (s.key) landingsByKey.set(s.key, s);
  landingsByKey.set('index.md', hub);
  const byKey = new Map(pages.map((p) => [p.key, p]));
  const docs = [hub, ...groups.filter((g) => g.key), ...sections, ...pages];
  for (const doc of docs) doc.headings = collectHeadings(marked, doc.body || '');
  const zips = exampleZips();
  const sitePaths = new Set([
    '',
    ...JSON.parse(readFileSync(resolve(root, 'content/pages.json'), 'utf8')).map((p) => p.path),
  ]);

  state.resolveLink = (href) => {
    const doc = state.doc;
    const from = doc.key ? posix.dirname(doc.key) : doc.kind === 'section' ? doc.dir : '.';
    if (/^(https?:|mailto:)/.test(href)) return href;
    if (href.startsWith('#')) {
      if (!doc.headings.some((h) => h.id === href.slice(1)))
        problems.push(`${doc.key || doc.id}: no heading for link ${href}`);
      return href;
    }
    const [path, anchor] = href.split('#');
    if (path.startsWith('/')) {
      const sitePath = path.slice(1);
      if (sitePaths.has(sitePath))
        return `${rel(doc.depth)}${sitePath}${anchor ? `#${anchor}` : ''}`;
      problems.push(`${doc.key || doc.id}: unknown site link ${href}`);
      return href;
    }
    const key = posix.normalize(posix.join(from, path));
    if (key.endsWith('.zip') && key.startsWith('examples/')) {
      const name = posix.basename(key, '.zip');
      if (!zips.has(name)) problems.push(`${doc.key || doc.id}: no example named ${name}`);
      return `${rel(doc.depth)}${DOCS}examples/${name}.zip`;
    }
    const target = byKey.get(key) || landingsByKey.get(key);
    if (!target) {
      problems.push(`${doc.key || doc.id}: link to missing page ${href}`);
      return '#';
    }
    if (anchor && !target.headings.some((h) => h.id === anchor))
      problems.push(`${doc.key || doc.id}: link ${href} has no heading #${anchor} in ${key}`);
    return `${rel(doc.depth)}${target.path}${anchor ? `#${anchor}` : ''}`;
  };

  const ctx = { site, groups, sections, hub, marked, state, byKey, zips, landingsByKey };
  const files = new Map();
  for (const page of pages) files.set(`${page.path}index.html`, renderPage(ctx, page));
  for (const section of sections)
    files.set(`${section.path}index.html`, renderLanding(ctx, section, section.pages));
  for (const group of groups.filter((g) => g.key))
    files.set(`${group.path}index.html`, renderLanding(ctx, group, group.sections));
  for (const key of site.hub.popular)
    if (!byKey.has(key)) problems.push(`site.json: hub.popular lists missing page ${key}`);
  files.set(`${DOCS}index.html`, renderHub(ctx));
  const real = [...files.keys()].map((file) => file.slice(0, -'index.html'.length));
  for (const [from, to] of redirects(real)) {
    if (!files.has(`${to}index.html`))
      problems.push(`redirects.json: ${from.slice(DOCS.length)} points at missing ${to.slice(DOCS.length)}`);
    files.set(`${from}index.html`, renderRedirect(site, from, to));
  }

  const index = pages.map((p) => ({
    t: p.title,
    n: p.navTitle !== p.title ? p.navTitle : undefined,
    s: p.section.navTitle,
    u: p.path.slice(DOCS.length),
    d: p.description,
    h: p.headings
      .filter((h) => h.depth <= 3)
      .slice(0, 80)
      .map((h) => [h.text, h.id]),
    k: (p.terms || []).slice(0, 300).join(' '),
  }));
  return { files, index, zips, problems, pages, sections, groups };
}

export async function renderDocs({ strict = !process.env.DOCS_DRAFT } = {}) {
  const { files, index, zips, problems } = buildDocs();
  if (problems.length && !strict)
    console.warn(
      `Modding docs: ${problems.length} problem(s) ignored (DOCS_DRAFT).\n  ${problems.slice(0, 40).join('\n  ')}`,
    );
  else if (problems.length)
    throw new Error(`Modding docs have ${problems.length} problem(s):\n  ${problems.join('\n  ')}`);
  // Write only what changed and remove only pages that no longer exist: the dev server
  // watches these files, and rewriting or deleting all of them at once would flood it.
  const wanted = new Set([
    ...[...files.keys()].map((path) => resolve(root, path)),
    ...[...zips.keys()].map((name) => resolve(examplesOut, `${name}.zip`)),
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
  for (const dir of outDirs) removeStale(dir);
  removeStale(examplesOut);
  let written = 0;
  const writeIfChanged = (file, data) => {
    if (existsSync(file) && readFileSync(file).equals(Buffer.from(data))) return;
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, data);
    written++;
  };
  for (const [name, zip] of zips) writeIfChanged(resolve(examplesOut, `${name}.zip`), zip);
  for (const [path, html] of files) writeIfChanged(resolve(root, path), html);
  writeIfChanged(indexFile, JSON.stringify(index));
  console.log(
    `Rendered ${files.size} modding docs pages and ${zips.size} example mods (${written} files changed).`,
  );
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await renderDocs();
}
