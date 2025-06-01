import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import ToolsView from '../views/ToolsView.vue'
import PresetView from '../views/PresetEditorView.vue'
import CalibrateView from '../views/CalibrationView.vue'

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
    path: '/calibrate',
    name: 'Calibrate',
    component: CalibrateView,
    meta: {
      title: 'Vybes DSP - Calibration'
    }
  },
  {
    path: '/tools',
    name: 'Tools',
    component: ToolsView,
    meta: {
      title: 'Vybes DSP - Tools'
    }
  },
  {
    path: '/preset/:name',
    name: 'Preset',
    component: PresetView,
    props: true,
    meta: {
      title: 'Vybes DSP - Preset Configuration'
    }
  },
  {
    // Catch all route - redirect to home
    path: '/:pathMatch(.*)*',
    redirect: '/'
  }
]

const router = createRouter({
  history: createWebHistory(),
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