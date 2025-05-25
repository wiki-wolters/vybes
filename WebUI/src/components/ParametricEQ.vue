<template>
  <div class="parametric-eq">
    <div class="eq-container" ref="eqContainer">
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

      <!-- EQ Points -->
      <div
        v-for="(point, index) in localEqPoints"
        :key="point.id"
        class="eq-point"
        :class="{ active: selectedPoint === index }"
        :style="{
          left: frequencyToX(point.frequency) + 'px',
          top: gainToY(point.gain) + 'px'
        }"
        @mousedown="startDrag(index, $event)"
        @touchstart="startDrag(index, $event)"
        @dblclick="removePoint(index)"
        @click="selectedPoint = index"
      >
        <div class="point-circle"></div>
        <div class="point-label">
          {{ Math.round(point.frequency) }}Hz
          <br>
          {{ point.gain > 0 ? '+' : '' }}{{ point.gain.toFixed(1) }}dB
        </div>
      </div>

      <!-- Add Point Button -->
      <button class="add-point-btn" @click="addPoint">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M8 2v12M2 8h12" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
        </svg>
      </button>
    </div>

    <!-- Point Selection Row -->
    <div class="point-selection">
      <button
        v-for="(point, index) in localEqPoints"
        :key="`select-${point.id}`"
        class="point-btn"
        :class="{ active: selectedPoint === index }"
        @click="selectedPoint = index"
      >
        {{ index + 1 }}
      </button>
    </div>

    <!-- Controls -->
    <div class="eq-controls" v-if="selectedPoint !== null && localEqPoints[selectedPoint]">
      <h4>EQ Point {{ selectedPoint + 1 }}</h4>
      <div class="control-group">
        <range-slider
          label="Frequency"
          :min="20"
          :max="20000"
          :step="1"
          unit="Hz"
          :decimals="0"
          v-model="localEqPoints[selectedPoint].frequency"
          @update:modelValue="emitChange"
        />
      </div>
      <div class="control-group">
        <range-slider
          label="Gain"
          :min="-15"
          :max="15"
          :step="0.1"
          unit="dB"
          :decimals="1"
          v-model="localEqPoints[selectedPoint].gain"
          @update:modelValue="emitChange"
        />
      </div>
      <div class="control-group">
        <range-slider
          label="Q Factor"
          :min="0.1"
          :max="10"
          :step="0.1"
          unit=""
          :decimals="1"
          v-model="localEqPoints[selectedPoint].q"
          @update:modelValue="emitChange"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted, watch } from 'vue';
import RangeSlider from './shared/RangeSlider.vue';

const props = defineProps({
  peqPoints: {
    type: Array,
    default: () => [
      { id: 1, frequency: 100, gain: 0, q: 1 },
      { id: 2, frequency: 1000, gain: 0, q: 1 },
      { id: 3, frequency: 10000, gain: 0, q: 1 }
    ]
  }
});

const emit = defineEmits(['change']);

// Component dimensions
const width = 800;
const height = 400;

// Local copy of EQ points
const localEqPoints = reactive([]);
let nextId = 1;

// Initialize local points from props
const initializePoints = () => {
  localEqPoints.splice(0, localEqPoints.length);
  props.peqPoints.forEach(point => {
    localEqPoints.push({ ...point });
    if (point.id >= nextId) {
      nextId = point.id + 1;
    }
  });
  
  // Ensure we have at least one point
  if (localEqPoints.length === 0) {
    localEqPoints.push({ id: nextId++, frequency: 1000, gain: 0, q: 1 });
  }
};

// Watch for prop changes
watch(() => props.peqPoints, initializePoints, { immediate: true, deep: true });

const selectedPoint = ref(0);
const dragState = reactive({
  isDragging: false,
  pointIndex: null,
  startX: 0,
  startY: 0,
  startFreq: 0,
  startGain: 0
});

// Debounced emit function
let emitTimeout = null;
const emitChange = () => {
  if (emitTimeout) {
    clearTimeout(emitTimeout);
  }
  
  emitTimeout = setTimeout(() => {
    // Create a clean copy of the points for emission
    const pointsToEmit = localEqPoints.map(point => ({
      id: point.id,
      frequency: point.frequency,
      gain: point.gain,
      q: point.q
    }));
    emit('change', pointsToEmit);
  }, 100);
};

// Frequency conversion (logarithmic scale)
const frequencyToX = (freq) => {
  const minFreq = Math.log10(20);
  const maxFreq = Math.log10(20000);
  const logFreq = Math.log10(freq);
  return ((logFreq - minFreq) / (maxFreq - minFreq)) * width;
};

const xToFrequency = (x) => {
  const minFreq = Math.log10(20);
  const maxFreq = Math.log10(20000);
  const ratio = x / width;
  return Math.pow(10, minFreq + ratio * (maxFreq - minFreq));
};

// Gain conversion (linear scale)
const gainToY = (gain) => {
  const maxGain = 15;
  const minGain = -15;
  return height - ((gain - minGain) / (maxGain - minGain)) * height;
};

const yToGain = (y) => {
  const maxGain = 15;
  const minGain = -15;
  const ratio = (height - y) / height;
  return minGain + ratio * (maxGain - minGain);
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
  const points = [];
  const steps = 200;
  for (let i = 0; i <= steps; i++) {
    const x = (i / steps) * width;
    const freq = xToFrequency(x);
    let totalGain = 0;
    // Calculate combined response from all EQ points
    localEqPoints.forEach(point => {
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

// Point management
const addPoint = () => {
  if (localEqPoints.length >= 10) return;

  const newPoint = {
    id: nextId++,
    frequency: 1000,
    gain: 0,
    q: 1
  };

  localEqPoints.push(newPoint);
  selectedPoint.value = localEqPoints.length - 1;
  emitChange();
};

const removePoint = (index) => {
  if (localEqPoints.length <= 1) return;

  localEqPoints.splice(index, 1);

  if (selectedPoint.value >= localEqPoints.length) {
    selectedPoint.value = localEqPoints.length - 1;
  }
  emitChange();
};

// Drag functionality
const startDrag = (index, event) => {
  event.preventDefault();
  selectedPoint.value = index;

  const clientX = event.touches ? event.touches[0].clientX : event.clientX;
  const clientY = event.touches ? event.touches[0].clientY : event.clientY;

  dragState.isDragging = true;
  dragState.pointIndex = index;
  dragState.startX = clientX;
  dragState.startY = clientY;
  dragState.startFreq = localEqPoints[index].frequency;
  dragState.startGain = localEqPoints[index].gain;
};

const onMouseMove = (event) => {
  if (!dragState.isDragging) return;

  const clientX = event.touches ? event.touches[0].clientX : event.clientX;
  const clientY = event.touches ? event.touches[0].clientY : event.clientY;

  const deltaX = clientX - dragState.startX;
  const deltaY = clientY - dragState.startY;

  const currentX = frequencyToX(dragState.startFreq) + deltaX;
  const currentY = gainToY(dragState.startGain) + deltaY;

  const newFreq = Math.max(20, Math.min(20000, xToFrequency(currentX)));
  const newGain = Math.max(-15, Math.min(15, yToGain(currentY)));

  localEqPoints[dragState.pointIndex].frequency = newFreq;
  localEqPoints[dragState.pointIndex].gain = newGain;
  
  emitChange();
};

const stopDrag = () => {
  dragState.isDragging = false;
  dragState.pointIndex = null;
};

// Event listeners
onMounted(() => {
  document.addEventListener('mousemove', onMouseMove);
  document.addEventListener('mouseup', stopDrag);
  document.addEventListener('touchmove', onMouseMove);
  document.addEventListener('touchend', stopDrag);
});

onUnmounted(() => {
  document.removeEventListener('mousemove', onMouseMove);
  document.removeEventListener('mouseup', stopDrag);
  document.removeEventListener('touchmove', onMouseMove);
  document.removeEventListener('touchend', stopDrag);
  
  if (emitTimeout) {
    clearTimeout(emitTimeout);
  }
});
</script>

<style scoped>
.parametric-eq {
  font-family: 'Segoe UI', system-ui, sans-serif;
  background: #1a1a1a;
  color: #fff;
  padding: 20px;
  border-radius: 8px;
}

.eq-container {
  position: relative;
  width: 800px;
  height: 400px;
  background: #0f0f0f;
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 20px;
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
  cursor: grab;
  z-index: 10;
  pointer-events: auto;
}

.eq-point:active {
  cursor: grabbing;
}

.point-circle {
  width: 12px;
  height: 12px;
  background: #0088ff;
  border: 2px solid #fff;
  border-radius: 50%;
  box-shadow: 0 2px 8px rgba(0, 136, 255, 0.4);
  transition: all 0.2s ease;
}

.eq-point:hover .point-circle {
  width: 16px;
  height: 16px;
  box-shadow: 0 4px 12px rgba(0, 136, 255, 0.6);
}

.eq-point.active .point-circle {
  background: #0088ff;
  border: 3px solid #fff;
  box-shadow: 0 4px 16px rgba(0, 136, 255, 0.8), 0 0 0 3px rgba(0, 136, 255, 0.3);
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

.add-point-btn {
  position: absolute;
  top: 10px;
  right: 10px;
  background: #333;
  color: #fff;
  border: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s ease;
  padding: 0;
}

.add-point-btn:hover {
  background: #0088ff;
  color: #fff;
  transform: scale(1.1);
}

.add-point-btn svg {
  display: block;
}

.point-selection {
  display: flex;
  gap: 8px;
  justify-content: center;
  margin-bottom: 20px;
  position: static;
  z-index: 1;
}

.point-btn {
  background: #333;
  color: #fff;
  border: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  font-size: 14px;
  font-weight: bold;
  transition: all 0.2s ease;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  line-height: 1;
}

.point-btn:hover {
  background: #444;
  transform: scale(1.05);
}

.point-btn.active {
  background: #0088ff;
  box-shadow: 0 0 0 2px rgba(0, 136, 255, 0.3);
}

.eq-controls {
  background: #222;
  padding: 20px;
  border-radius: 4px;
}

.eq-controls h4 {
  margin: 0 0 15px 0;
  color: #0088ff;
}

.control-group {
  margin-bottom: 15px;
}
</style>