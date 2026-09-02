<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

import { AppButton, EmptyState, InlineAlert, LoadingSkeleton, StatusBadge } from '@/components/ui';
import { loadChangelog } from '@/services/changelog';
import type { ChangelogEntry } from '@/utils/changelog';
import { parseChangelogVersion } from '@/utils/changelog';

type FilterMode = 'current' | 'line' | 'all';

const { t } = useI18n();
const releases = ref<ChangelogEntry[]>([]);
const installedVersion = ref('0.0.0');
const latestAvailable = ref<ChangelogEntry | null>(null);
const githubError = ref<string | null>(null);
const bundledOnly = ref(false);
const loading = ref(true);
const refreshing = ref(false);
const filter = ref<FilterMode>('line');

const installedInfo = computed(() => parseChangelogVersion(installedVersion.value));

const filteredReleases = computed(() => {
  if (filter.value === 'all') return releases.value;
  if (filter.value === 'line') {
    return releases.value.filter(
      (release) => release.releaseLine === installedInfo.value.releaseLine,
    );
  }
  return releases.value.filter((release) => release.coreVersion === installedInfo.value.coreVersion);
});

function isInstalled(release: ChangelogEntry): boolean {
  return (
    release.tag.toLowerCase() === installedVersion.value.replace(/^v/i, '').toLowerCase() ||
    release.tag.toLowerCase() === installedVersion.value.toLowerCase()
  );
}

function isLatest(release: ChangelogEntry): boolean {
  return !!latestAvailable.value && release.tag === latestAvailable.value.tag;
}

function channelTone(release: ChangelogEntry): 'success' | 'warning' {
  return release.channel === 'stable' ? 'success' : 'warning';
}

async function refresh(): Promise<void> {
  if (refreshing.value) return;
  refreshing.value = true;
  try {
    const result = await loadChangelog();
    releases.value = result.releases;
    installedVersion.value = result.installedVersion;
    latestAvailable.value = result.latestAvailable;
    githubError.value = result.githubError;
    bundledOnly.value = result.bundledOnly;
  } finally {
    refreshing.value = false;
    loading.value = false;
  }
}

onMounted(() => {
  void refresh();
});
</script>

<template>
  <section class="overview-panel changelog-panel" aria-labelledby="changelog-title">
    <div class="overview-panel__heading">
      <div>
        <h2 id="changelog-title">{{ t('ui.overview.changelog.title') }}</h2>
        <p>
          {{
            t('ui.overview.changelog.description', { version: installedVersion })
          }}
        </p>
      </div>
      <div class="changelog-panel__badges">
        <StatusBadge
          :label="t('ui.overview.changelog.installedBadge', { version: installedVersion })"
          tone="info"
          compact
        />
        <StatusBadge
          v-if="latestAvailable"
          :label="t('ui.overview.changelog.latestBadge', { version: latestAvailable.tag })"
          :tone="isInstalled(latestAvailable) ? 'success' : 'warning'"
          compact
        />
        <StatusBadge
          v-if="bundledOnly"
          :label="t('ui.overview.changelog.bundledBadge')"
          tone="neutral"
          compact
        />
      </div>
    </div>

    <InlineAlert v-if="githubError" tone="warning" :title="t('ui.overview.changelog.githubError')">
      {{ t('ui.overview.changelog.githubErrorDetail') }}
    </InlineAlert>

    <div class="changelog-panel__controls">
      <div class="changelog-filter" role="group" :aria-label="t('ui.overview.changelog.filterLabel')">
        <button
          v-for="mode in ['current', 'line', 'all'] as const"
          :key="mode"
          type="button"
          class="changelog-filter__option"
          :class="{ 'changelog-filter__option--active': filter === mode }"
          :aria-pressed="filter === mode"
          @click="filter = mode"
        >
          {{ t(`ui.overview.changelog.filter_${mode}`) }}
        </button>
      </div>
      <AppButton
        icon="refresh"
        :label="t('_common.refresh')"
        variant="tertiary"
        size="compact"
        :busy="refreshing"
        :busy-label="t('ui.overview.refreshing')"
        @click="refresh()"
      />
    </div>

    <LoadingSkeleton
      v-if="loading"
      variant="block"
      height="180px"
      :label="t('ui.overview.changelog.loading')"
    />
    <EmptyState
      v-else-if="filteredReleases.length === 0"
      compact
      icon="download"
      :title="t('ui.overview.changelog.noReleases')"
    />
    <ol v-else class="changelog-list">
      <li
        v-for="release in filteredReleases"
        :key="release.tag"
        class="changelog-entry"
        :class="{
          'changelog-entry--installed': isInstalled(release),
          'changelog-entry--latest': isLatest(release),
        }"
      >
        <article class="changelog-card">
          <header class="changelog-card__header">
            <div class="changelog-card__titlewrap">
              <h3>{{ release.name || release.tag }}</h3>
              <p>
                <span class="changelog-card__tag">{{ release.tag }}</span>
                <template v-if="release.date"> &middot; {{ release.date }}</template>
              </p>
            </div>
            <div class="changelog-card__badges">
              <StatusBadge
                v-if="isInstalled(release)"
                :label="t('ui.overview.changelog.currentBadge')"
                tone="info"
                compact
              />
              <StatusBadge
                v-if="isLatest(release)"
                :label="t('ui.overview.changelog.latestBadgeShort')"
                tone="success"
                compact
              />
              <StatusBadge
                :label="t(`ui.overview.changelog.channel_${release.channel}`)"
                :tone="channelTone(release)"
                compact
              />
            </div>
          </header>

          <div class="changelog-card__body">
            <template v-if="release.sections.length > 0">
              <section v-for="section in release.sections" :key="section.heading">
                <h4>{{ section.heading }}</h4>
                <p v-for="paragraph in section.body" :key="paragraph">{{ paragraph }}</p>
                <ul v-if="section.bullets.length">
                  <li v-for="bullet in section.bullets" :key="bullet">{{ bullet }}</li>
                </ul>
              </section>
            </template>
            <pre v-else>{{ release.body }}</pre>
          </div>

          <footer v-if="release.url" class="changelog-card__footer">
            <a :href="release.url" target="_blank" rel="noopener noreferrer">
              {{ t('ui.overview.changelog.openGithub') }}
            </a>
          </footer>
        </article>
      </li>
    </ol>
  </section>
</template>

<style scoped>
.changelog-panel {
  grid-column: 1 / -1;
}

.changelog-panel__badges {
  display: flex;
  flex-wrap: wrap;
  gap: var(--vs-space-8);
}

.changelog-panel__controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  gap: var(--vs-space-8);
  margin-bottom: var(--vs-space-12);
}

.changelog-filter {
  display: inline-flex;
  border: var(--vs-border-width) solid var(--vs-color-border-subtle);
  border-radius: var(--vs-radius-pill, 999px);
  overflow: hidden;
}

.changelog-filter__option {
  appearance: none;
  border: none;
  background: transparent;
  color: var(--vs-color-text-muted);
  font: inherit;
  font-size: var(--vs-type-size-metadata);
  padding: var(--vs-space-4) var(--vs-space-12);
  cursor: pointer;
}

.changelog-filter__option--active {
  background: var(--vs-color-accent-subtle, var(--vs-color-bg-hover, rgba(127, 127, 127, 0.15)));
  color: var(--vs-color-text);
  font-weight: 600;
}

.changelog-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: var(--vs-space-12);
  max-height: min(32rem, 60vh);
  overflow-y: auto;
}

.changelog-card {
  border: var(--vs-border-width) solid var(--vs-color-border-subtle);
  border-radius: var(--vs-radius-card);
  padding: var(--vs-space-12) var(--vs-space-16);
}

.changelog-entry--installed .changelog-card {
  border-color: var(--vs-color-accent, var(--vs-color-border-strong));
}

.changelog-card__header {
  display: flex;
  flex-wrap: wrap;
  align-items: flex-start;
  justify-content: space-between;
  gap: var(--vs-space-8);
}

.changelog-card__titlewrap h3 {
  margin: 0;
  font-size: var(--vs-type-size-body);
  font-weight: 600;
}

.changelog-card__titlewrap p {
  margin: var(--vs-space-2) 0 0;
  font-size: var(--vs-type-size-metadata);
  color: var(--vs-color-text-muted);
}

.changelog-card__tag {
  font-family: var(--vs-font-family-mono, monospace);
}

.changelog-card__badges {
  display: flex;
  flex-wrap: wrap;
  gap: var(--vs-space-8);
}

.changelog-card__body {
  margin-top: var(--vs-space-12);
  font-size: var(--vs-type-size-body);
  line-height: 1.5;
}

.changelog-card__body h4 {
  margin: var(--vs-space-8) 0 var(--vs-space-4);
  font-size: var(--vs-type-size-metadata);
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--vs-color-text-muted);
}

.changelog-card__body p {
  margin: 0 0 var(--vs-space-4);
  white-space: pre-wrap;
}

.changelog-card__body ul {
  margin: 0 0 var(--vs-space-4);
  padding-left: var(--vs-space-20);
}

.changelog-card__body li {
  margin-bottom: var(--vs-space-2);
}

.changelog-card__body pre {
  margin: 0;
  white-space: pre-wrap;
  font-family: inherit;
}

.changelog-card__footer {
  margin-top: var(--vs-space-8);
  padding-top: var(--vs-space-8);
  border-top: 1px dashed var(--vs-color-border-subtle);
}

.changelog-card__footer a {
  color: var(--vs-color-accent);
  font-size: var(--vs-type-size-metadata);
  text-decoration: none;
}

.changelog-card__footer a:hover {
  text-decoration: underline;
}
</style>
