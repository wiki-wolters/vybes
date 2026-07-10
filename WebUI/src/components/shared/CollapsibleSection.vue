<template>
  <section class="mb-3 bg-vybes-dark-element rounded-none sm:rounded-lg">
    <!-- Header: tapping it (or the chevron) expands; the switch only enables -->
    <div
      class="flex justify-between items-center px-3 py-3 sm:px-4 cursor-pointer select-none"
      @click="toggleExpanded"
    >
      <div class="flex items-center gap-2 min-w-0">
        <svg
          class="w-4 h-4 flex-none text-vybes-text-secondary transition-transform duration-200"
          :class="{ 'rotate-90': isExpanded }"
          fill="none" stroke="currentColor" viewBox="0 0 24 24"
        >
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M9 5l7 7-7 7" />
        </svg>
        <h3 class="text-lg font-semibold text-vybes-text-primary truncate">{{ title }}</h3>
      </div>

      <div class="flex items-center space-x-3" @click.stop>
        <slot name="header-actions"></slot>
        <ToggleSwitch
          v-if="toggleable"
          :model-value="modelValue"
          @update:modelValue="onEnabledToggle"
        />
      </div>
    </div>

    <!-- Collapsible content -->
    <div
      ref="contentRef"
      class="overflow-hidden"
      :class="{ 'transition-all duration-300 ease-in-out': animate }"
      :style="{ maxHeight: maxHeight, opacity: isExpanded ? 1 : 0 }"
    >
      <div class="px-3 pb-3 sm:px-4">
        <slot></slot>

        <div v-if="$slots.actions" class="mt-4 flex justify-end space-x-2">
          <slot name="actions"></slot>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { ref, watch } from 'vue'
import ToggleSwitch from './ToggleSwitch.vue'

const props = defineProps({
  title: {
    type: String,
    required: true
  },
  // Enabled state (the switch). Only meaningful when toggleable.
  modelValue: {
    type: Boolean,
    default: false
  },
  // Sections without an enable concept (e.g. Volume & Balance) hide the
  // switch and are purely expandable.
  toggleable: {
    type: Boolean,
    default: true
  },
  animate: {
    type: Boolean,
    default: true
  }
})

const emit = defineEmits(['update:modelValue'])

// Enabled sections start expanded; disabled ones start collapsed but can
// still be opened to inspect their settings.
const isExpanded = ref(props.toggleable ? props.modelValue : true)
const contentRef = ref(null)
// 'none' while at rest so content can grow (e.g. the EQ adding bands)
// without being clipped by a stale measured height.
const maxHeight = ref(isExpanded.value ? 'none' : '0px')

watch(isExpanded, (expanded) => {
  const el = contentRef.value
  if (!el) return
  if (!props.animate) {
    maxHeight.value = expanded ? 'none' : '0px'
    return
  }
  if (expanded) {
    maxHeight.value = el.scrollHeight + 'px'
    const release = (e) => {
      if (e.target !== el) return
      el.removeEventListener('transitionend', release)
      if (isExpanded.value) maxHeight.value = 'none'
    }
    el.addEventListener('transitionend', release)
  } else {
    // Collapse in two steps: pin the current height, then animate to 0.
    maxHeight.value = el.scrollHeight + 'px'
    void el.offsetHeight // force reflow so the transition starts from here
    maxHeight.value = '0px'
  }
})

const toggleExpanded = () => {
  isExpanded.value = !isExpanded.value
}

const onEnabledToggle = (value) => {
  emit('update:modelValue', value)
  // Enabling something you can't see is disorienting — reveal it.
  if (value) isExpanded.value = true
}

// Expose methods for parent components
defineExpose({
  toggle: toggleExpanded,
  expand: () => { isExpanded.value = true },
  collapse: () => { isExpanded.value = false },
  isExpanded: () => isExpanded.value
})
</script>
