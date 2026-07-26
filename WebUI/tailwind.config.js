/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // Mirrors the CSS variables in src/style.css — that file is the
        // source of truth, these are the Tailwind class aliases. Blues are
        // one hue family (interactive); amber is "currently in effect".
        // vybes-blue/light-blue/*-hover kept as aliases so existing
        // classes still work.
        'vybes-primary': '#3b82f6',
        'vybes-primary-dark': '#2563eb',
        'vybes-primary-hover': '#2563eb',
        'vybes-blue': '#3b82f6',
        'vybes-light-blue': '#60a5fa',
        'vybes-accent': '#f5c04e',
        'vybes-accent-light': '#f8d488',
        'vybes-accent-hover': '#f8d488',
        'vybes-dark': '#10141a',
        'vybes-dark-bg': '#10141a',
        'vybes-dark-element': '#161b22',
        'vybes-dark-card': '#1a2029',
        'vybes-dark-input': '#222a35',
        'vybes-dark-float': '#1f2836',
        'vybes-dark-border': '#0b0e13',
        'vybes-border': 'rgba(148, 168, 196, 0.16)',
        'vybes-border-solid': '#3a4451',
        'vybes-text-primary': '#e5ebf3',
        'vybes-text-secondary': '#8b96a8',
        'vybes-live': '#22c55e',
      },
      fontFamily: {
        sans: ['system-ui', '-apple-system', 'Segoe UI', 'sans-serif'],
      }
    },
  },
  plugins: [],
}
