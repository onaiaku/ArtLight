import { useSystemStore } from '@/stores/system';
import {
  type BundledChangelogAsset,
  type ChangelogEntry,
  type GitHubReleaseLike,
  compareChangelogTags,
  githubReleaseToChangelogEntry,
  mergeChangelogEntries,
  sortChangelogEntries,
} from '@/utils/changelog';

export interface LoadChangelogResult {
  releases: ChangelogEntry[];
  bundledOnly: boolean;
  githubError: string | null;
  latestAvailable: ChangelogEntry | null;
  installedVersion: string;
}

const CHANGELOG_ASSET_URL = './assets/changelog.json';
const GITHUB_RELEASES_URL = 'https://api.github.com/repos/onaiaku/ArtLight/releases';
// Cache GitHub releases in localStorage so repeated dashboard visits don't burn
// the unauthenticated 60 req/hour IP-wide API limit (the reason the changelog
// panel usually showed stale/bundled data until a manual refresh).
const GITHUB_CACHE_KEY = 'artlight.changelog.github';
const GITHUB_CACHE_TTL_MS = 30 * 60 * 1000;

interface GithubCachePayload {
  fetchedAt: number;
  releases: GitHubReleaseLike[];
}

function readGithubCache(): GitHubReleaseLike[] | null {
  try {
    const raw = localStorage.getItem(GITHUB_CACHE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as GithubCachePayload;
    if (!Array.isArray(parsed?.releases)) return null;
    if (typeof parsed.fetchedAt !== 'number' || Date.now() - parsed.fetchedAt > GITHUB_CACHE_TTL_MS) {
      return null;
    }
    return parsed.releases;
  } catch {
    return null;
  }
}

function writeGithubCache(releases: GitHubReleaseLike[]): void {
  try {
    const payload: GithubCachePayload = { fetchedAt: Date.now(), releases };
    localStorage.setItem(GITHUB_CACHE_KEY, JSON.stringify(payload));
  } catch {
    // storage unavailable (private mode etc.) — caching is best-effort only
  }
}

function isChangelogEntry(value: unknown): value is ChangelogEntry {
  return !!value && typeof value === 'object' && typeof (value as ChangelogEntry).tag === 'string';
}

export async function getInstalledVersion(): Promise<string> {
  try {
    const systemStore = useSystemStore();
    if (!systemStore.metadata) await systemStore.refreshHost();
    const version = systemStore.metadata?.version;
    return typeof version === 'string' && version.trim() ? version.trim() : '0.0.0';
  } catch {
    return '0.0.0';
  }
}

export async function loadBundledChangelog(): Promise<ChangelogEntry[]> {
  const response = await fetch(CHANGELOG_ASSET_URL, { cache: 'no-cache' });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const data = (await response.json()) as Partial<BundledChangelogAsset>;
  const releases = Array.isArray(data.releases) ? data.releases.filter(isChangelogEntry) : [];
  return sortChangelogEntries(releases);
}

export async function loadGithubChangelog(forceRefresh = false): Promise<ChangelogEntry[]> {
  if (!forceRefresh) {
    const cached = readGithubCache();
    if (cached) {
      return cached
        .map(githubReleaseToChangelogEntry)
        .filter((entry): entry is ChangelogEntry => entry !== null);
    }
  }
  const response = await fetch(GITHUB_RELEASES_URL, {
    headers: { Accept: 'application/vnd.github+json' },
  });
  if (!response.ok) {
    // Surface the actual reason (most commonly 403 = unauthenticated rate limit)
    // so the UI can say "rate limited, showing bundled" instead of looking broken.
    let detail = `HTTP ${response.status}`;
    if (response.status === 403 || response.status === 429) {
      const resetHeader = response.headers.get('x-ratelimit-reset');
      if (resetHeader) {
        const resetAt = Number(resetHeader) * 1000;
        if (Number.isFinite(resetAt)) {
          const mins = Math.max(1, Math.round((resetAt - Date.now()) / 60000));
          detail += ` (GitHub rate limit — resets in ~${mins} min)`;
        } else {
          detail += ' (GitHub rate limit exceeded)';
        }
      }
    }
    throw new Error(detail);
  }
  const releases = (await response.json()) as GitHubReleaseLike[];
  if (!Array.isArray(releases)) return [];
  writeGithubCache(releases);
  return releases
    .map(githubReleaseToChangelogEntry)
    .filter((entry): entry is ChangelogEntry => entry !== null);
}

export async function loadChangelog(
  forceRefresh = false,
): Promise<LoadChangelogResult> {
  const [installedVersion, bundled] = await Promise.all([
    getInstalledVersion(),
    loadBundledChangelog().catch(() => []),
  ]);

  let github: ChangelogEntry[] = [];
  let githubError: string | null = null;
  try {
    github = await loadGithubChangelog(forceRefresh);
  } catch (error) {
    githubError = error instanceof Error ? error.message : String(error);
  }

  const releases = mergeChangelogEntries(bundled, github);
  const latestAvailable =
    releases.reduce<ChangelogEntry | null>((latest, release) => {
      if (!latest) return release;
      return compareChangelogTags(release.tag, latest.tag) > 0 ? release : latest;
    }, null) ?? null;
  return {
    releases,
    bundledOnly: github.length === 0,
    githubError,
    latestAvailable,
    installedVersion,
  };
}
