import { createRouter, createWebHashHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import ToolsView from '../views/ToolsView.vue'
import AnalyzerView from '../views/AnalyzerView.vue'
import PresetView from '../views/PresetEditorView.vue'

const routes = [
  {
    path: '/',
    name: 'Home',
    component: HomeView,
    meta: {
      title: 'Vybes DSP - Home'
    }
  },
  {
    path: '/tools',
    name: 'Tools',
    component: ToolsView,
    meta: {
      title: 'Tone generator etc'
    }
  },
  {
    path: '/analyzer',
    name: 'Analyzer',
    component: AnalyzerView,
    meta: {
      title: 'Real-time analyzer'
    }
  },
  {
    path: '/preset/:name',
    name: 'Preset',
    component: PresetView,
    props: true,
    meta: {
      title: 'Preset Configuration'
    }
  },
  {
    // Catch all route - redirect to home
    path: '/:pathMatch(.*)*',
    redirect: '/'
  }
]

const router = createRouter({
  history: createWebHashHistory(),
  routes
})

// Update document title based on route meta
router.beforeEach((to, from, next) => {
  if (to.meta.title) {
    document.title = to.meta.title
  }
  next()
})

export default router