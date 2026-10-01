import fs from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath, URL } from 'node:url';

import vue from '@vitejs/plugin-vue';
import { defineConfig, type Plugin } from 'vite';
import {
  parseBundledReleaseNote,
  sortChangelogEntries,
  type ChangelogEntry,
} from './utils/changelog';

const configuredOutputDirectory = process.env.SUNSHINE_WEB_OUTPUT_DIR;
const CONFIG_DIR = fileURLToPath(new URL('.', import.meta.url));

// Find the repo root by walking up until a CMakeLists.txt is found (best-effort, capped depth)
function findRepoRoot(startDir: string): string {
  let dir = startDir;
  for (let i = 0; i < 8; i++) {
    if (fs.existsSync(resolve(dir, 'CMakeLists.txt'))) return dir;
    const parent = resolve(dir, '..');
    if (parent === dir) break;
    dir = parent;
  }
  return startDir;
}

// Titles an ArtLight release note must begin with to be bundled. See the filter
// inside bundledChangelogPlugin for why this exists.
const PRODUCT_NAME = 'artlight';

// Emit assets/changelog.json at build time from server/release_notes/*.md so the
// Overview changelog panel has bundled release notes to show when GitHub is
// unreachable (offline, rate-limited, or CSP-blocked). Ported from web-legacy.
function bundledChangelogPlugin(repoRoot: string): Plugin {
  return {
    name: 'artlightserver-bundled-changelog',
    generateBundle() {
      const releaseNotesDir = resolve(repoRoot, 'release_notes');
      const releases = fs.existsSync(releaseNotesDir)
        ? fs
            .readdirSync(releaseNotesDir)
            .filter((name) => name.toLowerCase().endsWith('.md'))
            .map((name) => {
              const path = resolve(releaseNotesDir, name);
              return parseBundledReleaseNote(name, fs.readFileSync(path, 'utf-8'));
            })
            .filter((entry): entry is ChangelogEntry => entry !== null)
            // release_notes/ also carries upstream's own notes, which arrive
            // with an upstream merge and describe releases this product never
            // made -- their titles name the other product. Bundling them made
            // ArtLight's changelog list upstream's versions under upstream's
            // name. Keep only notes that are ours; a note with no '# ' title
            // still qualifies, since it defaults to the product name.
            .filter((entry) => entry.name.toLowerCase().startsWith(PRODUCT_NAME))
        : [];

      this.emitFile({
        type: 'asset',
        fileName: 'assets/changelog.json',
        source: JSON.stringify(
          {
            generatedAt: new Date().toISOString(),
            releases: sortChangelogEntries(releases),
          },
          null,
          2,
        ),
      });
    },
  };
}

export default defineConfig({
  plugins: [vue(), bundledChangelogPlugin(findRepoRoot(CONFIG_DIR))],
  base: '/v2/',
  resolve: {
    dedupe: ['vue', 'vue-i18n'],
    alias: {
      '@': fileURLToPath(new URL('.', import.meta.url)),
    },
  },
  build: {
    outDir: configuredOutputDirectory
      ? resolve(configuredOutputDirectory)
      : fileURLToPath(new URL('../../../../build/assets/web/v2', import.meta.url)),
    emptyOutDir: true,
    target: 'es2022',
  },
  server: {
    host: '127.0.0.1',
    port: 5173,
    proxy: {
      '/api': {
        target: 'https://127.0.0.1:47990',
        changeOrigin: true,
        secure: false,
      },
      '/covers': {
        target: 'https://127.0.0.1:47990',
        changeOrigin: true,
        secure: false,
      },
    },
  },
});
