/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'vybes-primary': '#0088ff',
        'vybes-primary-hover': '#0066cc', // Added for hover state
        'vybes-blue': '#0088ff',
        'vybes-light-blue': '#33aaff', // Added - used in Vue component
        'vybes-accent': '#ff6b35', // Added - used in Vue component  
        'vybes-accent-hover': '#ff6b35',
        'vybes-dark-bg': '#1a1a1a',
        'vybes-dark-card': '#0f0f0f',
        'vybes-dark-element': '#222222',
        'vybes-dark-input': '#444444',
        'vybes-dark-border': '#111',
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
