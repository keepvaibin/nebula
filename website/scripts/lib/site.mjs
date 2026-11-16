/**
 * The site's name and public address (content/site.json), and the link-preview tags every page carries.
 * Change the address there if the site moves; every canonical link, share card and the sitemap follow it.
 */
import { readFileSync } from 'node:fs';

export const site = JSON.parse(
  readFileSync(new URL('../../content/site.json', import.meta.url), 'utf8'),
);

const escape = (value) =>
  String(value).replace(
    /[&<>"]/g,
    (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c],
  );

/** The full address of a page, from its path relative to the site root ("" is the home page). */
export const pageUrl = (path = '') => new URL(path, site.url).href;

/**
 * Canonical link and Open Graph tags for a page.
 * `image` is a path from the site root or a full address; without one the site icon is used.
 */
export function shareTags(path, { type = 'website', image } = {}) {
  const imageUrl = new URL(image || site.image, site.url).href;
  return [
    `<link rel="canonical" href="${escape(pageUrl(path))}" />`,
    `<meta property="og:site_name" content="${escape(site.name)}" />`,
    `<meta property="og:type" content="${escape(type)}" />`,
    `<meta property="og:url" content="${escape(pageUrl(path))}" />`,
    `<meta property="og:image" content="${escape(imageUrl)}" />`,
    `<meta name="twitter:card" content="${image ? 'summary_large_image' : 'summary'}" />`,
  ].join('\n');
}
