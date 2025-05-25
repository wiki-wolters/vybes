/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'vybes-blue': '#0088ff',
        'vybes-dark-bg': '#1a1a1a',
        'vybes-dark-card': '#0f0f0f',
        'vybes-dark-element': '#222222',
        'vybes-dark-input': '#444',
        'vybes-text-primary': '#ffffff',
        'vybes-text-secondary': '#cccccc',
      },
      fontFamily: {
        sans: ['Segoe UI', 'system-ui', 'sans-serif'],
      }
    },
  },
  plugins: [],
}
