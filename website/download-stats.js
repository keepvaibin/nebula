// In the build, a GitHub token (GITHUB_TOKEN, which GitHub Actions provides) lifts the API's rate limit.
// Browsers never have one.
function githubHeaders() {
  const headers = { Accept: 'application/vnd.github+json' };
  const token = typeof process !== 'undefined' ? process.env?.GITHUB_TOKEN : undefined;
  if (token) headers.Authorization = `Bearer ${token}`;
  return headers;
}

// The download counter adds up the downloads of the game's packages, for every system. A package is what a
// person downloads to install or unpack the game, known by what its file name ends in: an installer, an app
// package, a disk image or an archive. The files beside a package are not counted: signatures, manifests and
// checksum lists. The game's updater fetches them with the package it installs, so counting them would count
// one download twice. A name that ends in anything else is not counted either: a new kind of package is added
// to PACKAGE_ENDINGS, and until then it cannot add a false count.
const PACKAGE_ENDINGS = {
  windows: ['exe', 'msi', 'msix', 'msixbundle', 'appx', 'appxbundle'],
  android: ['apk', 'apks', 'aab', 'xapk'],
  apple: ['dmg', 'pkg', 'ipa'],
  linux: ['deb', 'rpm', 'appimage', 'flatpak', 'snap'],
  archives: ['zip', '7z', 'tar', 'tgz', 'tbz2', 'txz', 'tar.gz', 'tar.bz2', 'tar.xz', 'tar.zst'],
};
const PACKAGE_SUFFIXES = Object.values(PACKAGE_ENDINGS).flat().map((ending) => `.${ending}`);

/** True when a release file, by its name, is a package of the game (see above). */
export function isPackage(name) {
  if (typeof name !== 'string') return false;
  const lower = name.toLowerCase();
  return PACKAGE_SUFFIXES.some((suffix) => lower.endsWith(suffix));
}

export async function fetchDownloadStats(fetcher = fetch) {
  let total = 0,
    releases = 0;
  for (let page = 1; ; page++) {
    const response = await fetcher(
      `https://api.github.com/repos/keepvaibin/nebula/releases?per_page=100&page=${page}`,
      {
        headers: githubHeaders(),
        signal: AbortSignal.timeout(10000),
      },
    );
    if (!response.ok) throw new Error(`GitHub releases returned ${response.status}`);
    const batch = await response.json();
    if (!Array.isArray(batch)) throw new Error('Invalid GitHub releases response');
    for (const release of batch) {
      if (release.draft) continue;
      releases++;
      for (const asset of release.assets || []) {
        if (!Number.isSafeInteger(asset.download_count) || asset.download_count < 0)
          throw new Error('Invalid download count');
        if (isPackage(asset.name)) total += asset.download_count;
      }
    }
    if (batch.length < 100) break;
  }
  return { total, releases, checkedAt: new Date().toISOString() };
}

export async function fetchRepoStars(fetcher = fetch) {
  const response = await fetcher('https://api.github.com/repos/keepvaibin/nebula', {
    headers: githubHeaders(),
    signal: AbortSignal.timeout(10000),
  });
  if (!response.ok) throw new Error(`GitHub repository returned ${response.status}`);
  const repo = await response.json();
  if (!Number.isSafeInteger(repo?.stargazers_count) || repo.stargazers_count < 0)
    throw new Error('Invalid star count');
  return repo.stargazers_count;
}
