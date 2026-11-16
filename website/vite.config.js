import { renderPages } from './scripts/render-pages.mjs';
import { renderDocs, docRoutes } from './scripts/render-docs.mjs';
import { renderBlog, blogRoutes } from './scripts/render-blog.mjs';
import { defineConfig } from 'vite';
import { resolve, sep } from 'node:path';
import { readFileSync } from 'node:fs';

const pages = JSON.parse(readFileSync(new URL('./content/pages.json', import.meta.url), 'utf8'));

export default defineConfig(({ mode }) => {
  // `npm run blog:drafts` previews posts marked draft: true. They are never built for the site.
  if (mode === 'drafts') process.env.BLOG_DRAFTS = '1';
  const routes = [...pages.map((p) => p.path), ...docRoutes(), ...blogRoutes()];
  return {
    base: './',
    plugins: [
      {
        name: 'page-text-preview',
        async configureServer(server) {
          // The modding docs sit outside this folder, so Vite does not watch them on its own.
          server.watcher.add(resolve(import.meta.dirname, '../docs/modding'));
          if (mode === 'drafts') await renderBlog({ strict: false });
        },
        async handleHotUpdate({ file, server }) {
          const normalized = file.split(sep).join('/');
          const rerender = async (render) => {
            try {
              await render();
            } catch (error) {
              server.config.logger.error(error.message);
            }
            server.ws.send({ type: 'full-reload' });
            return [];
          };
          if (normalized.includes('/docs/modding/')) return rerender(renderDocs);
          if (normalized.includes('/content/blog/') || normalized.includes('/templates/blog/'))
            return rerender(() => renderBlog({ strict: false }));
          if (!normalized.includes('/content/pages/') && !normalized.includes('/templates/pages/'))
            return;
          return rerender(() => renderPages({ refreshDownloads: false }));
        },
      },
    ],
    build: {
      rollupOptions: {
        input: Object.fromEntries(
          routes.map((path) => [
            path.replace(/(\/|\.html)$/, '').replaceAll('/', '-') || 'home',
            resolve(import.meta.dirname, path.endsWith('.html') ? path : `${path}index.html`),
          ]),
        ),
      },
    },
  };
});
