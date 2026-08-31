import { createPinia } from 'pinia';
import { createApp } from 'vue';

import App from '@/App.vue';
import { createArtLightServerI18n } from '@/i18n';
import router from '@/router';
import '@/styles/tokens.css';
import '@/styles/base.css';
import '@/styles/components.css';
import '@/styles/shell.css';
import '@/styles/views.css';

const i18n = await createArtLightServerI18n();

router.afterEach((route) => {
  const key = typeof route.meta.titleKey === 'string' ? route.meta.titleKey : 'ui.nav.overview';
  const title = i18n.global.t(key);
  document.title = route.path === '/' ? 'ArtLight Server' : `${title} · ArtLight Server`;
});

createApp(App).use(createPinia()).use(router).use(i18n).mount('#app');
