/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'vybes-primary': '#3b82f6',
        'vybes-primary-dark': '#2563eb',
        'vybes-primary-hover': '#0066cc', // Added for hover state
        'vybes-blue': '#0088ff',
        'vybes-light-blue': '#33aaff', // Added - used in Vue component
        'vybes-accent': '#f59e0b', // Added - used in Vue component  
        'vybes-accent-light': '#fbbf24',
        'vybes-accent-hover': '#fbbf24',
        'vybes-dark-bg': '#1a1a1a',
        'vybes-dark-card': '#374151',
        'vybes-dark': '#111827',
        'vybes-dark-element': '#1f2937',
        'vybes-dark-card': '#4b5563',
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
