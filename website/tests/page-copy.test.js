import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { parse } from 'parse5';
import { renderPageText } from '../scripts/lib/page-text.mjs';
const root = resolve(import.meta.dirname, '..');
const routes = JSON.parse(readFileSync(resolve(root, 'content/pages.json'), 'utf8'));

test('every page renders from its own JSON without leftover placeholders or hardcoded visible text', () => {
  for (const route of routes) {
    const template = readFileSync(resolve(root, route.template), 'utf8');
    const copy = JSON.parse(readFileSync(resolve(root, route.copy), 'utf8'));
    assert.equal(copy.page, route.path ? '/' + route.path : '/');
    const output = renderPageText(template, copy, { total: 1234 });
    assert.doesNotMatch(output, /\{\{[\w.-]+\}\}/);
    function visit(node) {
      if (['script', 'style'].includes(node.tagName)) return;
      if (node.nodeName === '#text' && node.value.trim())
        assert.match(
          node.value,
          /\{\{[\w.-]+\}\}/,
          `Uneditable text in ${route.template}: ${node.value}`,
        );
      for (const child of node.childNodes || []) visit(child);
    }
    visit(parse(template));
  }
});

test('wording changes render as safe plain text, and required fields cannot disappear silently', () => {
  const route = routes.find((p) => p.path === '');
  const template = readFileSync(resolve(root, route.template), 'utf8');
  const copy = JSON.parse(readFileSync(resolve(root, route.copy), 'utf8'));
  copy.text.hero.intro = 'Your galaxy & your words <script>alert(1)</script>';
  const output = renderPageText(template, copy, null);
  assert.match(output, /Your galaxy &amp; your words &lt;script&gt;alert\(1\)&lt;\/script&gt;/);
  delete copy.text.hero.intro;
  assert.throws(() => renderPageText(template, copy, null), /Missing page text: text.hero.intro/);
});
