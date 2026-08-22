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
        // source of truth, these are the Tailwind class aliases. One teal
        // family (docs/brand) carries brand + interactive: 500 for fills,
        // 300 (vybes-brand) for text/lines on dark; amber is "currently in
        // effect". vybes-blue/light-blue/*-hover kept as aliases (teal now)
        // so existing classes still work.
        'vybes-primary': '#17808D',
        'vybes-primary-dark': '#136B77',
        'vybes-primary-hover': '#136B77',
        'vybes-blue': '#17808D',
        'vybes-light-blue': '#45AEBC',
        'vybes-accent': '#f5c04e',
        'vybes-accent-light': '#f8d488',
        'vybes-accent-hover': '#f8d488',
        'vybes-brand': '#45AEBC',
        'vybes-brand-light': '#63C3CF',
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
