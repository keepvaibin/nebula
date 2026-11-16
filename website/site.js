import { pageMessage } from './page-copy.js';
import { fetchDownloadStats, fetchRepoStars } from './download-stats.js';
let customCursor = document.querySelector('.star-cursor');
if (!customCursor) {
  customCursor = document.createElement('div');
  customCursor.className = 'star-cursor';
  customCursor.textContent = '✧';
  customCursor.setAttribute('aria-hidden', 'true');
  document.body.appendChild(customCursor);
}
const hideCustomCursor = () => {
  document.body.classList.remove('custom-pointer-active');
  customCursor.style.opacity = '0';
};
window.addEventListener(
  'pointermove',
  (event) => {
    if (event.pointerType !== 'mouse') {
      hideCustomCursor();
      return;
    }
    // A transform moves the cursor on the compositor; left/top would relayout on every move.
    customCursor.style.transform = `translate3d(${event.clientX}px, ${event.clientY}px, 0) translate(-50%, -50%)`;
    customCursor.style.opacity = '1';
    document.body.classList.add('custom-pointer-active');
  },
  { passive: true },
);
window.addEventListener('pointerout', (event) => {
  if (!event.relatedTarget) hideCustomCursor();
});
window.addEventListener('blur', hideCustomCursor);
const downloadCount = document.querySelector('[data-download-count]');
if (downloadCount) {
  const loadStats = async () => {
    const cacheKey = 'nebula-download-stats';
    try {
      const cached = JSON.parse(sessionStorage.getItem(cacheKey));
      if (cached && Number.isSafeInteger(cached.total) && Date.now() - cached.savedAt < 900000)
        return cached;
    } catch {
      /* Storage may be disabled. */
    }
    const stats = await fetchDownloadStats();
    try {
      sessionStorage.setItem(cacheKey, JSON.stringify({ ...stats, savedAt: Date.now() }));
    } catch {
      /* The build-time count is still available without storage. */
    }
    return stats;
  };
  loadStats()
    .then((stats) => {
      downloadCount.textContent = stats.total.toLocaleString('en-US');
      downloadCount.title = pageMessage('downloadCountTitle', { releases: stats.releases });
    })
    .catch(() => {}); // Keep the build-time count if GitHub is unavailable or rate-limited.
}
const starCount = document.querySelector('[data-star-count]');
if (starCount) {
  const cacheKey = 'nebula-star-count';
  let cached;
  try {
    cached = JSON.parse(sessionStorage.getItem(cacheKey));
  } catch {
    /* Storage may be disabled. */
  }
  const show = (stars) => (starCount.textContent = stars.toLocaleString('en-US'));
  if (cached && Number.isSafeInteger(cached.stars) && Date.now() - cached.savedAt < 900000)
    show(cached.stars);
  else
    fetchRepoStars()
      .then((stars) => {
        show(stars);
        try {
          sessionStorage.setItem(cacheKey, JSON.stringify({ stars, savedAt: Date.now() }));
        } catch {
          /* The build-time count is still shown without storage. */
        }
      })
      .catch(() => {}); // Keep the build-time count if GitHub is unavailable or rate-limited.
}
const toggle = document.querySelector('.nav-toggle');
const closeMenu = () => {
  toggle?.setAttribute('aria-expanded', 'false');
  document.querySelector('#main-navigation')?.classList.remove('is-open');
};
toggle?.addEventListener('click', () => {
  const open = toggle.getAttribute('aria-expanded') !== 'true';
  toggle.setAttribute('aria-expanded', String(open));
  document.querySelector('#main-navigation').classList.toggle('is-open', open);
});
document.addEventListener('click', (event) => {
  if (event.target.closest('#main-navigation a') || !event.target.closest('.site-header'))
    closeMenu();
});
matchMedia('(min-width: 701px)').addEventListener('change', closeMenu);
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && toggle?.getAttribute('aria-expanded') === 'true') {
    toggle.setAttribute('aria-expanded', 'false');
    document.querySelector('#main-navigation').classList.remove('is-open');
    toggle.focus();
  }
});
for (const input of document.querySelectorAll('[data-filter]')) {
  const entries = [...document.querySelectorAll(input.dataset.filter)];
  const empty = input.parentElement.querySelector('.search-empty');
  input.addEventListener('input', () => {
    const query = input.value.trim().toLowerCase();
    let found = 0;
    for (const item of entries) {
      item.hidden = !item.textContent.toLowerCase().includes(query);
      if (!item.hidden) found++;
    }
    if (empty) empty.hidden = found > 0;
  });
}
if (location.hash) {
  const target = document.getElementById(decodeURIComponent(location.hash.slice(1)));
  if (target?.tagName === 'DETAILS') target.open = true;
}
