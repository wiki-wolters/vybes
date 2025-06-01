<template>
  <div id="app" class="min-h-screen bg-vybes-dark text-vybes-text-primary">
    <!-- Navigation Header -->
    <nav class="bg-vybes-dark-element border-b border-vybes-border">
      <div class="container mx-auto px-6 py-4">
        <div class="flex items-center justify-between">
          <!-- Logo/Brand -->
          <router-link 
            to="/" 
            class="text-2xl font-bold text-vybes-accent hover:text-vybes-accent-light transition-colors"
          >
            Vybes DSP
          </router-link>
          
          <!-- Navigation Links -->
          <div class="flex space-x-6">
            <router-link 
              to="/" 
              class="nav-link"
              :class="{ 'nav-link-active': $route.name === 'Home' }"
            >
              Home
            </router-link>
            <router-link 
              to="/calibrate" 
              class="nav-link"
              :class="{ 'nav-link-active': $route.name === 'Calibrate' }"
            >
              Calibrate
            </router-link>
            <router-link 
              to="/tools" 
              class="nav-link"
              :class="{ 'nav-link-active': $route.name === 'Tools' }"
            >
              Tools
            </router-link>
          </div>
        </div>
      </div>
    </nav>

    <!-- Main Content -->
    <main class="flex-1">
      <router-view />
    </main>
  </div>
</template>

<script setup>
// You can add any global app logic here
</script>

<style scoped>
.nav-link {
  color: var(--vybes-text-primary);
  font-weight: 500;
  transition: color 0.3s;
}

.nav-link:hover {
  color: var(--vybes-accent);
}

.nav-link-active {
  color: var(--vybes-accent);
}
</style>