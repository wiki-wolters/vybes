/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // Blues collapsed to one hue family (previously #0088ff and #3b82f6
        // competed). vybes-blue/light-blue kept as aliases so existing
        // classes still work.
        'vybes-primary': '#3b82f6',
        'vybes-primary-dark': '#2563eb',
        'vybes-primary-hover': '#2563eb',
        'vybes-blue': '#3b82f6',
        'vybes-light-blue': '#60a5fa',
        'vybes-accent': '#f59e0b',
        'vybes-accent-light': '#fbbf24',
        'vybes-accent-hover': '#fbbf24',
        'vybes-dark': '#111827',
        'vybes-dark-bg': '#111827',
        'vybes-dark-element': '#1f2937',
        'vybes-dark-card': '#374151',
        'vybes-dark-input': '#4b5563',
        'vybes-dark-border': '#111',
        'vybes-border': '#6b7280',
        'vybes-text-primary': '#f9fafb',
        'vybes-text-secondary': '#d1d5db',
      },
      fontFamily: {
        sans: ['Segoe UI', 'system-ui', 'sans-serif'],
      }
    },
  },
  plugins: [],
}
