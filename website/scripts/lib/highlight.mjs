/**
 * Build-time syntax highlighting for the modding docs. Pages ship coloured HTML,
 * so visitors download no highlighter and nothing re-renders after load.
 */
const escapeHtml = (value) =>
  String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');

const words = (list) => new RegExp(`\\b(?:${list.trim().split(/\s+/).join('|')})\\b`, 'y');

const lumascriptKeywords = words(`
  use pub let var const fn return if else while for in break continue match struct enum class
  override hook before after wrap replace proceed skip state states go wait until frames seconds
  task stop on option as is and or not extern export defer test expect guard self result`);
const lumascriptTypes = words(`
  bool i8 i16 i32 i64 u8 u16 u32 u64 int f32 f64 float vec2 vec3 vec4 quat mat34 color text bytes
  list map set ref`);
const literals = words('true false none null');
const cKeywords = words(`
  auto break case char const continue default do double else enum extern float for goto if inline
  int long register return short signed sizeof static struct switch typedef union unsigned void
  volatile while bool uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t size_t`);

const number = /(?:0x[\da-fA-F_]+|0b[01_]+|\d[\d_]*(?:\.\d+)?(?:e[+-]?\d+)?(?:f32|f64|f)?)\b/y;

const grammars = {
  lumascript: [
    ['cm', /\/\/[^\n]*/y],
    ['cm', /\/\*[\s\S]*?\*\//y],
    ['st', /(?:r|b)?"(?:\\.|[^"\\])*"/y],
    ['gm', /`[^`\n]*`/y],
    ['nu', /#[\da-fA-F]{6}(?:[\da-fA-F]{2})?\b/y],
    ['kw', lumascriptKeywords],
    ['ty', lumascriptTypes],
    ['li', literals],
    ['gm', /[A-Za-z_]\w*(?:::[A-Za-z_~]\w*)+/y],
    ['fn', /[A-Za-z_]\w*(?=\s*\()/y],
    ['nu', number],
  ],
  c: [
    ['cm', /\/\/[^\n]*/y],
    ['cm', /\/\*[\s\S]*?\*\//y],
    ['pp', /#\s*\w+[^\n]*/y],
    ['st', /"(?:\\.|[^"\\])*"/y],
    ['st', /'(?:\\.|[^'\\])'/y],
    ['kw', cKeywords],
    ['li', literals],
    ['fn', /[A-Za-z_]\w*(?=\s*\()/y],
    ['nu', number],
  ],
  json: [
    ['ky', /"(?:\\.|[^"\\])*"(?=\s*:)/y],
    ['st', /"(?:\\.|[^"\\])*"/y],
    ['li', literals],
    ['nu', /-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/y],
  ],
  sh: [
    ['cm', /(?<=^|\s)#[^\n]*/my],
    ['st', /"(?:\\.|[^"\\])*"|'[^']*'/y],
    ['fl', /(?<=\s)--?[\w-]+/y],
    ['fn', /(?<=^|&&\s|\|\s)[\w./-]+/my],
  ],
};
grammars.lum = grammars.lumascript;
grammars.cpp = grammars.c;
grammars.h = grammars.c;
grammars.bash = grammars.sh;
grammars.shell = grammars.sh;
grammars.console = grammars.sh;
grammars.jsonc = grammars.json;

export function highlight(code, language) {
  const rules = grammars[(language || '').toLowerCase()];
  if (!rules) return escapeHtml(code);
  let html = '';
  let plain = '';
  let index = 0;
  const flush = () => {
    html += escapeHtml(plain);
    plain = '';
  };
  outer: while (index < code.length) {
    // Identifiers are matched whole so a keyword inside a longer name is not coloured.
    for (const [kind, pattern] of rules) {
      pattern.lastIndex = index;
      const match = pattern.exec(code);
      if (match && match[0].length) {
        flush();
        html += `<span class="tk-${kind}">${escapeHtml(match[0])}</span>`;
        index += match[0].length;
        continue outer;
      }
    }
    const word = /[A-Za-z_]\w*/y;
    word.lastIndex = index;
    const skipped = word.exec(code);
    const step = skipped ? skipped[0].length : 1;
    plain += code.slice(index, index + step);
    index += step;
  }
  flush();
  return html;
}

export const highlightedLanguages = Object.keys(grammars);
