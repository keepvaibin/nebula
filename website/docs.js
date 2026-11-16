/** Modding docs: sidebar drawer, search, copy buttons and the "On this page" highlight. */
const sidebar = document.getElementById('docs-sidebar');
const openers = document.querySelectorAll('.docs-menu-toggle');
const setDrawer = (open) => {
  if (!sidebar) return;
  sidebar.classList.toggle('is-open', open);
  for (const button of openers) button.setAttribute('aria-expanded', String(open));
  document.documentElement.style.overflow = open ? 'hidden' : '';
  // Focus the close button: focusing the search box would raise a phone's keyboard.
  if (open) sidebar.querySelector('.docs-close')?.focus({ preventScroll: true });
};
for (const button of openers) button.addEventListener('click', () => setDrawer(true));
sidebar?.querySelector('.docs-close')?.addEventListener('click', () => {
  setDrawer(false);
  openers[0]?.focus();
});
document.addEventListener('click', (event) => {
  if (
    sidebar?.classList.contains('is-open') &&
    !event.target.closest('#docs-sidebar, .docs-menu-toggle')
  )
    setDrawer(false);
});
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && sidebar?.classList.contains('is-open')) {
    setDrawer(false);
    openers[0]?.focus();
  }
});
matchMedia('(min-width: 961px)').addEventListener('change', () => setDrawer(false));
sidebar?.addEventListener('click', (event) => {
  if (event.target.closest('a') && sidebar.classList.contains('is-open')) setDrawer(false);
});

// Keep the current page visible in a long sidebar.
const currentLink = sidebar?.querySelector('[aria-current="page"]');
if (currentLink && currentLink.offsetParent === sidebar)
  sidebar.scrollTop = currentLink.offsetTop - sidebar.clientHeight / 2;

/* Copy buttons are added here so pages without scripts show no dead controls. */
const copyLabel = 'Copy';
for (const block of document.querySelectorAll('.code-block')) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'copy-code';
  button.textContent = copyLabel;
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(block.querySelector('code').textContent);
      button.textContent = 'Copied';
    } catch {
      button.textContent = 'Select and copy';
    }
    setTimeout(() => (button.textContent = copyLabel), 1600);
  });
  block.appendChild(button);
}

/* "In this article": open beside the article on wide screens, folded above it on narrow ones. */
const tocDetails = document.querySelector('.docs-toc details');
if (tocDetails) {
  const wide = matchMedia('(min-width: 1281px)');
  const fit = () => (tocDetails.open = wide.matches);
  fit();
  wide.addEventListener('change', fit);
  for (const link of tocDetails.querySelectorAll('a'))
    link.addEventListener('click', () => {
      if (!wide.matches) tocDetails.open = false;
    });
}

/* "In this article": highlight the heading being read. */
const tocLinks = new Map(
  [...document.querySelectorAll('.docs-toc a')].map((a) => [a.getAttribute('href').slice(1), a]),
);
if (tocLinks.size && 'IntersectionObserver' in window) {
  let active;
  const observer = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        active?.classList.remove('is-active');
        active = tocLinks.get(entry.target.id);
        active?.classList.add('is-active');
      }
    },
    { rootMargin: '0px 0px -75% 0px' },
  );
  for (const id of tocLinks.keys()) {
    const heading = document.getElementById(id);
    if (heading) observer.observe(heading);
  }
}

/* Search: the index loads only when someone starts searching. */
let indexPromise;
const loadIndex = (url) =>
  (indexPromise ||= fetch(url)
    .then((response) => (response.ok ? response.json() : []))
    .catch(() => {
      indexPromise = undefined;
      return [];
    }));

const escapeHtml = (value) =>
  String(value).replace(
    /[&<>"]/g,
    (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c],
  );
const mark = (text, terms) => {
  let html = escapeHtml(text);
  for (const term of terms)
    html = html.replace(
      new RegExp(`(${term.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'ig'),
      '<mark>$1</mark>',
    );
  return html;
};

function search(index, query) {
  const terms = query.toLowerCase().split(/\s+/).filter(Boolean);
  const results = [];
  for (const page of index) {
    const title = (page.t + ' ' + (page.n || '')).toLowerCase();
    const description = page.d.toLowerCase();
    const keywords = page.k.toLowerCase();
    let score = 0;
    let heading;
    for (const term of terms) {
      let best = 0;
      if (title.includes(term)) best = title.startsWith(term) ? 12 : 9;
      const hit = page.h.find(([text]) => text.toLowerCase().includes(term));
      if (hit && best < 6) {
        best = 6;
        heading ||= hit;
      }
      if (best < 4 && keywords.includes(term)) best = 4;
      if (best < 3 && description.includes(term)) best = 3;
      if (best < 1 && page.s.toLowerCase().includes(term)) best = 1;
      if (!best) {
        score = 0;
        break;
      }
      score += best;
    }
    if (!score) continue;
    results.push({ page, score, heading });
  }
  return results.sort((a, b) => b.score - a.score).slice(0, 12);
}

for (const box of document.querySelectorAll('.docs-search')) {
  const input = box.querySelector('input');
  const list = box.querySelector('.docs-results');
  const empty = box.querySelector('.docs-results-empty');
  const docs = box.dataset.docs;
  let selected = -1;
  const close = () => {
    list.hidden = true;
    empty.hidden = true;
    selected = -1;
  };
  const select = (i) => {
    const links = [...list.querySelectorAll('a')];
    if (!links.length) return;
    selected = (i + links.length) % links.length;
    links.forEach((a, n) => a.setAttribute('aria-selected', String(n === selected)));
    links[selected].scrollIntoView({ block: 'nearest' });
  };
  const run = async () => {
    const query = input.value.trim();
    if (query.length < 2) return close();
    const index = await loadIndex(box.dataset.index);
    if (input.value.trim() !== query) return;
    const terms = query.split(/\s+/);
    const results = search(index, query);
    selected = -1;
    list.innerHTML = results
      .map(({ page, heading }) => {
        const href = `${docs}${page.u}${heading ? '#' + heading[1] : ''}`;
        const where = heading ? `${page.s} › ${page.t} › ${heading[0]}` : page.s;
        return `<li><a href="${href}"><strong>${mark(heading ? heading[0] : page.t, terms)}</strong><small>${escapeHtml(where)}</small><small>${mark(page.d, terms)}</small></a></li>`;
      })
      .join('');
    list.hidden = !results.length;
    empty.hidden = results.length > 0;
  };
  input.addEventListener('focus', () => loadIndex(box.dataset.index), { once: true });
  input.addEventListener('input', run);
  input.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      select(selected + 1);
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      select(selected - 1);
    } else if (event.key === 'Enter') {
      const links = list.querySelectorAll('a');
      const target = links[selected >= 0 ? selected : 0];
      if (target && !list.hidden) {
        event.preventDefault();
        location.href = target.href;
      }
    } else if (event.key === 'Escape') {
      close();
    }
  });
  document.addEventListener('click', (event) => {
    if (!box.contains(event.target)) close();
  });
}

document.addEventListener('keydown', (event) => {
  if (event.key !== '/' || event.ctrlKey || event.metaKey || event.altKey) return;
  if (event.target.closest('input, textarea, [contenteditable]')) return;
  const input = [...document.querySelectorAll('.docs-search input')].find(
    (el) => el.offsetParent !== null,
  );
  if (!input) return;
  event.preventDefault();
  input.focus();
});
