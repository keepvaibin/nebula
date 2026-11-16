import { readFile, writeFile } from 'node:fs/promises';
import { shareTags, pageUrl, site } from './lib/site.mjs';

/** A page's file: its folder's index.html, or the file it names (404.html). */
export const pageFile = (path) => (path.endsWith('.html') ? path : `${path}index.html`);
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { format } from 'prettier';
import { fetchDownloadStats, fetchRepoStars } from '../download-stats.js';
import { renderPageText } from './lib/page-text.mjs';
import { renderDocs, docPages } from './render-docs.mjs';
import { renderBlog, blogRoutes } from './render-blog.mjs';

const root = resolve(import.meta.dirname, '..');

export async function renderPages({ refreshDownloads = true } = {}) {
  const routes = JSON.parse(await readFile(resolve(root, 'content/pages.json'), 'utf8'));
  const formatOptions = JSON.parse(await readFile(resolve(root, '.prettierrc.json'), 'utf8'));
  let downloads = null;
  try {
    downloads = JSON.parse(await readFile(resolve(root, 'content/downloads.json'), 'utf8'));
  } catch {
    /* A missing cache should not prevent content edits. */
  }
  if (refreshDownloads) {
    try {
      const stars = await fetchRepoStars().catch(() => downloads?.stars);
      downloads = { ...(await fetchDownloadStats()), ...(stars === undefined ? {} : { stars }) };
      await writeFile(
        resolve(root, 'content/downloads.json'),
        JSON.stringify(downloads, null, 2) + '\n',
      );
    } catch {
      console.warn('GitHub downloads unavailable; using the saved count.');
    }
  }
  for (const route of routes) {
    const copy = JSON.parse(await readFile(resolve(root, route.copy), 'utf8'));
    const template = await readFile(resolve(root, route.template), 'utf8');
    let html = renderPageText(template, copy, downloads);
    // Pages kept out of search (the 404 page) get no canonical link or share card.
    if (!html.includes('content="noindex"'))
      html = html.replace('</head>', `${shareTags(route.path)}\n</head>`);
    await writeFile(
      resolve(root, pageFile(route.path)),
      await format(html, { ...formatOptions, parser: 'html' }),
    );
  }
  console.log(`Rendered ${routes.length} player pages from their text JSON files.`);
  await renderDocs();
  await renderBlog();
  await writeSiteFiles(routes);
}

/** The sitemap, robots.txt and the custom-domain file, from content/site.json and every page's address. */
async function writeSiteFiles(routes) {
  const paths = [
    ...routes.map((route) => route.path).filter((path) => !path.endsWith('.html')),
    ...docPages(),
    ...blogRoutes(),
  ];
  const urls = paths.map((path) => `  <url><loc>${pageUrl(path)}</loc></url>`).join('\n');
  await writeFile(
    resolve(root, 'public/sitemap.xml'),
    `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls}\n</urlset>\n`,
  );
  await writeFile(
    resolve(root, 'public/robots.txt'),
    `User-agent: *\nAllow: /\n\nSitemap: ${pageUrl('sitemap.xml')}\n`,
  );
  // GitHub Pages reads the custom domain from this file when the site is deployed from a branch.
  await writeFile(resolve(root, 'public/CNAME'), `${new URL(site.url).hostname}\n`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await renderPages();
}
