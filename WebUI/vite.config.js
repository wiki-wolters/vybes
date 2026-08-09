import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'
import viteCompression from 'vite-plugin-compression'

// Inline the built stylesheet into index.html. The device's HTTPS listener
// only affords a handful of TLS sockets, so a browser's parallel asset
// fetches race for connections and losers fail outright - styling must ship
// with the HTML rather than gamble on a socket of its own.
function inlineCss() {
  return {
    name: 'inline-css',
    apply: 'build',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const html = bundle['index.html']
      if (!html) return
      for (const [name, asset] of Object.entries(bundle)) {
        if (!name.endsWith('.css')) continue
        const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
        const link = new RegExp(`<link[^>]+href="/${escaped}"[^>]*>`)
        if (link.test(html.source)) {
          html.source = html.source.replace(link, () => `<style>${asset.source}</style>`)
          delete bundle[name]
        }
      }
    },
  }
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    vue(),
    tailwindcss(),
    inlineCss(),
    viteCompression({
      algorithm: 'gzip',
      ext: '.gz',
      deleteOriginFile: true,
    }),
  ],
  build: {
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
      },
      format: {
        comments: false,
      },
    },
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        manualChunks(id) {
          return 'vendor';
        }
      },
    }
  }
})
