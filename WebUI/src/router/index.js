import { createRouter, createWebHashHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import AnalyzerView from '../views/AnalyzerView.vue'
import PresetView from '../views/PresetEditorView.vue'
import FirWizardView from '../views/FirWizardView.vue'

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
    path: '/fir-wizard',
    name: 'FirWizard',
    component: FirWizardView,
    meta: {
      title: 'Auto-FIR Wizard'
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