import { defineConfig } from 'vitest/config'

// Standalone vitest config: the tests are plain node (API contract + pure
// logic), so they don't need the Vue/Tailwind/compression plugins from
// vite.config.js. Vitest prefers this file over vite.config.js.
export default defineConfig({
  test: {
    environment: 'node',
    // Contract tests may run against a real device over WiFi
    testTimeout: 15000,
    hookTimeout: 60000,
  },
})
