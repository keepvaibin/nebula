# Nebula website

The source of [nebula.keepvaibin.com](https://nebula.keepvaibin.com/): the player site (home page, v2, download,
setup guide, player guide, help, credits, legal), the developer blog at `/blog/` and the modding documentation
at `/modding/docs/`. It is a static site built with [Vite](https://vite.dev/); nothing runs on a server.

## Run it locally

You need Node.js 22 or later. From this folder:

```sh
npm install
npm run dev
```

Open the address Vite prints. Saving a page's text, a template, a blog post or a modding docs page regenerates
the preview. Other commands:

| Command               | What it does                                            |
| --------------------- | ------------------------------------------------------- |
| `npm test`            | Checks every page, link, docs page, blog post and model |
| `npm run build`       | Builds the finished site into `dist/`                   |
| `npm run preview`     | Serves `dist/` to check a build                         |
| `npm run blog:drafts` | Previews blog posts marked `draft: true`                |
| `npm run format`      | Formats the source with Prettier                        |

## Where things are

| Path                                                                       | What it holds                                                              |
| -------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `content/site.json`                                                        | The site's name and address, used for every canonical link and the sitemap |
| `content/pages/*.json`                                                     | The text of each page; see [editing page text](content/pages/README.md)    |
| `templates/pages/*.html`                                                   | Page layout and link destinations, with placeholders for the text          |
| `content/pages.json`                                                       | Each page's address, template and text file                                |
| `content/blog/`                                                            | Blog posts; see [writing a post](content/blog/README.md)                   |
| `../docs/modding/`                                                         | The modding docs; see [writing the docs](../docs/modding/README.md)        |
| `scripts/`                                                                 | The page, docs and blog renderers, and the model and image optimizers      |
| `style.css`, `reference-ui.css`, `player-site.css`, `blog.css`, `docs.css` | Styles                                                                     |
| `main.js`, `star-physics.js`                                               | The home page's 3D scene: Star Bits, planets and collisions                |
| `site.js`, `docs.js`                                                       | Menus, help search and download counts; the docs sidebar and search        |
| `public/`                                                                  | Files served as they are: icons, fonts, models and textures                |

The `index.html` files beside the templates, and the `blog/` and `docs/` folders, are generated. Edit the text,
template, post or Markdown instead, then run `npm run content` (the dev server and the build do this for you).

## Publishing

`.github/workflows/website.yml` at the repository root builds and publishes the site to GitHub Pages whenever a
change under `website/` or `docs/modding/` reaches `main`. It can also be run by hand from the Actions tab. See
the comments at the top of that file for the one-time setup and for publishing from a branch instead.

## Home page assets

The home page alone loads Three.js. The Star Bits and the Honeyhive and Good Egg planets are optimized GLB
files in `public/models/` and `public/environment/`; their COLLADA and PNG originals are in `assets/`. After
changing an original, run `npm run models:optimize` or `npm run images:optimize` and commit the results. The
image optimizer checks that every pixel of each WebP matches its PNG.

The Delfino font, models and textures belong to their respective owners, from
[The Models Resource](https://models.spriters-resource.com/wii/supermariogalaxy/). Nebula is not affiliated
with Nintendo.
