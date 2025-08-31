<template>
    <section 
      class="mb-3 p-3 bg-vybes-dark-card rounded-lg"
    >
      <div
        class="flex justify-between items-center"
        :class="{ 'pb-3': isExpanded }"
      >
        <h3 class="text-lg font-semibold text-vybes-accent">{{ title }}</h3>
        
        <div class="flex items-center space-x-3">
          <slot name="header-actions"></slot>
          
          <!-- Toggle Switch -->
          <ToggleSwitch :model-value="isExpanded" @update:modelValue="handleToggle" />
        </div>
      </div>
      
      <!-- Collapsible Content -->
      <div 
        ref="contentRef"
        class="overflow-hidden"
        :class="{ 'transition-all duration-300 ease-in-out': animate }"
        :style="{
          maxHeight: isExpanded ? contentHeight + 'px' : '0px',
          opacity: isExpanded ? 1 : 0
        }"
      >
        <div ref="innerContentRef">
          <slot></slot>
        </div>
      </div>
      
      <!-- Actions (only visible when expanded) -->
      <div 
        v-if="$slots.actions && isExpanded" 
        class="mt-4 flex justify-end space-x-2 transition-opacity duration-300"
        :class="{ 'opacity-100': isExpanded, 'opacity-0': !isExpanded }"
      >
        <slot name="actions"></slot>
      </div>
    </section>
  </template>
  
  <script setup>
  import { ref, nextTick, onMounted, watch, computed } from 'vue'
  import ToggleSwitch from './ToggleSwitch.vue'
  
  const props = defineProps({
    title: {
      type: String,
      required: true
    },
    modelValue: {
      type: Boolean,
      required: true
    },
    animate: {
      type: Boolean,
      default: true
    }
  })
  
  const emit = defineEmits(['update:modelValue', 'toggle'])
  
  // Use v-model if provided, otherwise use internal state
  const isExpanded = computed({
    get() {
      return props.modelValue
    },
    set(value) {
      emit('update:modelValue', value)
    }
  })
  
  const contentRef = ref(null)
  const innerContentRef = ref(null)
  const contentHeight = ref(0)
  
  const updateContentHeight = async () => {
    if (innerContentRef.value) {
      await nextTick()
      contentHeight.value = innerContentRef.value.scrollHeight
    }
  }
  
  const handleToggle = (value) => {
    isExpanded.value = value
  }

  const toggleExpanded = () => {
    isExpanded.value = !isExpanded.value
  }
  
  // Watch for content changes and update height
  watch(isExpanded, async (newValue) => {
    if (newValue) {
      await updateContentHeight()
    }
  })
  
  // Initialize content height on mount
  onMounted(() => {
    updateContentHeight()
  })
  
  // Expose methods for parent components
  defineExpose({
    toggle: toggleExpanded,
    expand: () => {
      isExpanded.value = true
    },
    collapse: () => {
      isExpanded.value = false
    },
    isExpanded: () => isExpanded.value
  })
  </script>
  
  <style scoped>
  /* Smooth transitions for all interactive elements */
  .transition-all {
    transition-property: all;
    transition-timing-function: cubic-bezier(0.4, 0, 0.2, 1);
  }
  </style>