import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'
import viteCompression from 'vite-plugin-compression'

// Inline the built stylesheet AND script into index.html, so a cold page load
// is a single request.
//
// The device's HTTPS listener affords 2 TLS sockets at ~40KB each, and as of
// 2026-08-24 it no longer evicts to make room (lru_purge_enable is false on
// that listener - see ESP/esp-web-server/web_server.cpp): a connection beyond
// the limit is refused before its handshake instead of admitted into an
// allocation that can wedge the chip. Refusal only works as a strategy if the
// page does not need many connections, which is what this plugin buys. One
// keep-alive connection carries the document and every API call after it, and
// the second socket is the live-updates websocket - exactly the budget.
//
// The cost is that the 89KB script stops being separately cacheable and rides
// every document load. On a LAN device that is the cheaper half of the trade;
// losing the socket race used to mean a blank or unstyled page.
function inlineAssets() {
  return {
    name: 'inline-assets',
    apply: 'build',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const html = bundle['index.html']
      if (!html) return
      const escapeName = (name) => name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')

      for (const [name, asset] of Object.entries(bundle)) {
        if (!name.endsWith('.css')) continue
        const link = new RegExp(`<link[^>]+href="/${escapeName(name)}"[^>]*>`)
        if (link.test(html.source)) {
          // Function replacement: asset contents are not replacement patterns,
          // so a literal $& in the CSS must not be expanded.
          html.source = html.source.replace(link, () => `<style>${asset.source}</style>`)
          delete bundle[name]
        }
      }

      for (const [name, chunk] of Object.entries(bundle)) {
        if (!name.endsWith('.js') || chunk.type !== 'chunk') continue
        const escaped = escapeName(name)
        const script = new RegExp(`<script[^>]*\\ssrc="/${escaped}"[^>]*>\\s*</script>`)
        if (!script.test(html.source)) continue
        // A literal </script> inside a string would close the tag early.
        const code = chunk.code.replace(/<\/script/gi, '<\\/script')
        html.source = html.source.replace(script, () => `<script type="module">${code}</script>`)
        // The preload hint would fetch a file that no longer exists, wasting
        // exactly the socket this plugin is trying to save.
        html.source = html.source.replace(
          new RegExp(`<link[^>]+rel="modulepreload"[^>]*href="/${escaped}"[^>]*>`, 'g'), '')
        delete bundle[name]
      }
    },
  }
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    vue(),
    tailwindcss(),
    inlineAssets(),
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
