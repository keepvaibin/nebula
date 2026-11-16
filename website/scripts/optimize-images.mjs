// Offline conversion: PNG sources in assets/texture-sources/ become lossless WebP files in public/.
// Every output is decoded and compared with its source; any pixel difference stops the run.
import sharp from 'sharp';
import { readdir, mkdir, writeFile, stat } from 'node:fs/promises';
import { resolve, relative, dirname, join } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const sources = resolve(root, 'assets/texture-sources');

async function* pngs(dir) {
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) yield* pngs(path);
    else if (entry.name.endsWith('.png')) yield path;
  }
}

let before = 0;
let after = 0;
for await (const file of pngs(sources)) {
  const original = await sharp(file).ensureAlpha().raw().toBuffer();
  // `exact` keeps the colour under transparent pixels, which texture filtering and alpha tests read.
  const webp = await sharp(file).webp({ lossless: true, effort: 6, exact: true }).toBuffer();
  const decoded = await sharp(webp).ensureAlpha().raw().toBuffer();
  if (!original.equals(decoded)) throw new Error(`${file}: WebP output differs from the PNG`);
  const out = resolve(root, 'public', relative(sources, file).replace(/\.png$/, '.webp'));
  await mkdir(dirname(out), { recursive: true });
  await writeFile(out, webp);
  before += (await stat(file)).size;
  after += webp.length;
}
console.log(`Textures: ${before} → ${after} bytes, pixel-identical.`);
