import { createRouter, createWebHashHistory } from 'vue-router'
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
  // {
  //   path: '/calibrate',
  //   name: 'Calibrate',
  //   component: CalibrateView,
  //   meta: {
  //     title: 'Vybes DSP - Calibration'
  //   }
  // },
  {
    path: '/tools',
    name: 'Tools',
    component: ToolsView,
    meta: {
      title: 'Tone generator etc'
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