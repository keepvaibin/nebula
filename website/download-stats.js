// In the build, a GitHub token (GITHUB_TOKEN, which GitHub Actions provides) lifts the API's rate limit.
// Browsers never have one.
function githubHeaders() {
  const headers = { Accept: 'application/vnd.github+json' };
  const token = typeof process !== 'undefined' ? process.env?.GITHUB_TOKEN : undefined;
  if (token) headers.Authorization = `Bearer ${token}`;
  return headers;
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
        total += asset.download_count;
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
