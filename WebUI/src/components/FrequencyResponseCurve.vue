<template>
    <div class="frequency-response" ref="responseContainer">
      <!-- Background grid -->
      <svg class="eq-grid" :width="width" :height="height">
        <!-- Frequency grid lines -->
        <g class="frequency-grid">
          <line
            v-for="freq in frequencyGridLines"
            :key="freq.value"
            :x1="freq.x"
            :y1="0"
            :x2="freq.x"
            :y2="height"
            stroke="#333"
            stroke-width="1"
            opacity="0.3"
          />
          <text
            v-for="freq in frequencyGridLines"
            :key="`label-${freq.value}`"
            :x="freq.x"
            :y="height - 5"
            fill="#666"
            font-size="10"
            text-anchor="middle"
          >
            {{ freq.label }}
          </text>
        </g>
  
        <!-- Gain grid lines -->
        <g class="gain-grid">
          <line
            v-for="gain in gainGridLines"
            :key="gain.value"
            :x1="0"
            :y1="gain.y"
            :x2="width"
            :y2="gain.y"
            stroke="#333"
            stroke-width="1"
            opacity="0.3"
          />
          <text
            v-for="gain in gainGridLines"
            :key="`label-${gain.value}`"
            :x="5"
            :y="gain.y - 3"
            fill="#666"
            font-size="10"
          >
            {{ gain.label }}
          </text>
        </g>
  
        <!-- Zero line -->
        <line
          :x1="0"
          :y1="height / 2"
          :x2="width"
          :y2="height / 2"
          stroke="#555"
          stroke-width="2"
        />
      </svg>
  
      <!-- EQ Curve -->
      <svg class="eq-curve" :width="width" :height="height">
        <path
          :d="curvePath"
          fill="none"
          stroke="#0088ff"
          stroke-width="3"
          opacity="0.8"
        />
      </svg>
  
      <!-- EQ Points (visual only) -->
      <div
        v-for="point in eqPoints"
        :key="point.id"
        class="eq-point"
        :style="{
          left: frequencyToX(point.frequency) + 'px',
          top: gainToY(point.gain) + 'px'
        }"
      >
        <div class="point-circle"></div>
        <div class="point-label">
          {{ Math.round(point.frequency) }}Hz
          <br>
          {{ point.gain > 0 ? '+' : '' }}{{ point.gain.toFixed(1) }}dB
        </div>
      </div>
    </div>
  </template>
  
  <script setup>
  import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue';
  
  const props = defineProps({
    eqPoints: {
      type: Array,
      default: () => []
    },
    showPoints: {
      type: Boolean,
      default: true
    }
  });
  
  // Component dimensions
  const responseContainer = ref(null);
  const width = ref(800); // Default width
  const height = 400;
  
  // Resize observer to update width
  let resizeObserver = null;
  
  const updateDimensions = () => {
    if (responseContainer.value) {
      const containerWidth = responseContainer.value.clientWidth;
      if (containerWidth > 0) {
        width.value = containerWidth;
      }
    }
  };
  
  // Frequency conversion (logarithmic scale)
  const frequencyToX = (freq) => {
    const minFreq = Math.log10(20);
    const maxFreq = Math.log10(20000);
    const logFreq = Math.log10(freq);
    return ((logFreq - minFreq) / (maxFreq - minFreq)) * width.value;
  };
  
  const xToFrequency = (x) => {
    const minFreq = Math.log10(20);
    const maxFreq = Math.log10(20000);
    const ratio = x / width.value;
    return Math.pow(10, minFreq + ratio * (maxFreq - minFreq));
  };
  
  // Gain conversion (linear scale)
  const gainToY = (gain) => {
    const maxGain = 15;
    const minGain = -15;
    return height - ((gain - minGain) / (maxGain - minGain)) * height;
  };
  
  // Grid lines
  const frequencyGridLines = computed(() => {
    const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    return frequencies.map(freq => ({
      value: freq,
      x: frequencyToX(freq),
      label: freq >= 1000 ? `${freq/1000}k` : `${freq}`
    }));
  });
  
  const gainGridLines = computed(() => {
    const gains = [-15, -10, -5, 0, 5, 10, 15];
    return gains.map(gain => ({
      value: gain,
      y: gainToY(gain),
      label: gain > 0 ? `+${gain}` : `${gain}`
    }));
  });
  
  // EQ curve calculation
  const curvePath = computed(() => {
    if (!props.eqPoints || props.eqPoints.length === 0) {
      // Return flat line at 0dB if no points
      const y = gainToY(0);
      return `M 0,${y} L ${width.value},${y}`;
    }
  
    const points = [];
    const steps = 200;
    
    for (let i = 0; i <= steps; i++) {
      const x = (i / steps) * width.value;
      const freq = xToFrequency(x);
      let totalGain = 0;
      
      // Calculate combined response from all EQ points
      props.eqPoints.forEach(point => {
        const gain = calculateBellFilter(freq, point.frequency, point.gain, point.q);
        totalGain += gain;
      });
  
      const y = gainToY(totalGain);
      points.push(`${x},${y}`);
    }
  
    return `M ${points.join(' L ')}`;
  });
  
  // Bell filter calculation (peaking EQ)
  const calculateBellFilter = (freq, centerFreq, gain, q) => {
    const ratio = freq / centerFreq;
    const bandwidth = 1 / q;
    const response = gain / (1 + Math.pow((ratio - 1/ratio) / bandwidth, 2));
    return response;
  };
  
  // Event listeners
  onMounted(async () => {
    // Initial dimension update
    await nextTick();
    updateDimensions();
    
    // Set up resize observer
    if (window.ResizeObserver && responseContainer.value) {
      resizeObserver = new ResizeObserver(updateDimensions);
      resizeObserver.observe(responseContainer.value);
    }
    
    // Fallback resize listener
    window.addEventListener('resize', updateDimensions);
  });
  
  onUnmounted(() => {
    window.removeEventListener('resize', updateDimensions);
    
    if (resizeObserver) {
      resizeObserver.disconnect();
    }
  });
  </script>
  
  <style scoped>
  .frequency-response {
    font-family: 'Segoe UI', system-ui, sans-serif;
    background: #1a1a1a;
    color: #fff;
    padding: 20px;
    border-radius: 8px;
    width: 100%;
  }
  
  .frequency-response > div:first-child {
    position: relative;
    width: 100%;
    height: 400px;
    background: #0f0f0f;
    border-radius: 4px;
    overflow: hidden;
    min-width: 300px;
  }
  
  .eq-grid, .eq-curve {
    position: absolute;
    top: 0;
    left: 0;
    pointer-events: none;
  }
  
  .eq-point {
    position: absolute;
    transform: translate(-50%, -50%);
    z-index: 10;
    pointer-events: none;
  }
  
  .point-circle {
    width: 8px;
    height: 8px;
    background: #0088ff;
    border: 2px solid #fff;
    border-radius: 50%;
    box-shadow: 0 2px 8px rgba(0, 136, 255, 0.4);
    transition: all 0.2s ease;
  }
  
  .eq-point:hover .point-circle {
    width: 12px;
    height: 12px;
    box-shadow: 0 4px 12px rgba(0, 136, 255, 0.6);
  }
  
  .point-label {
    position: absolute;
    top: -40px;
    left: 50%;
    transform: translateX(-50%);
    background: rgba(0, 0, 0, 0.8);
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 10px;
    text-align: center;
    white-space: nowrap;
    opacity: 0;
    transition: opacity 0.2s ease;
    pointer-events: none;
  }
  
  .eq-point:hover .point-label {
    opacity: 1;
  }
  </style>