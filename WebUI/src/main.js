import { createApp } from 'vue'
import router from './router'
import './style.css'
import App from './App.vue'
import apiClient from './api-client.js'

const app = createApp(App)

// Provide the API client to all components
app.provide('vybesAPI', apiClient)

// Create a reactive object for live update data that components can watch
app.provide('liveUpdateData', {})

app.use(router)
app.mount('#app')