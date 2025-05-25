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
        v-for="(point, index) in eqPoints"
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
        v-for="(point, index) in eqPoints"
        :key="`select-${point.id}`"
        class="point-btn"
        :class="{ active: selectedPoint === index }"
        @click="selectedPoint = index"
      >
        {{ index + 1 }}
      </button>
    </div>

    <!-- Controls -->
    <div class="eq-controls" v-if="selectedPoint !== null">
      <h4>EQ Point {{ selectedPoint + 1 }}</h4>
      <div class="control-group">
        <range-slider
          label="Frequency"
          :min="20"
          :max="20000"
          :step="1"
          unit="Hz"
          :decimals="0"
          v-model="eqPoints[selectedPoint].frequency"
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
          v-model="eqPoints[selectedPoint].gain"
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
          v-model="eqPoints[selectedPoint].q"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted, defineProps, watch } from 'vue'; // Added watch
import RangeSlider from '../shared/RangeSlider.vue';

const props = defineProps({
  apiClient: {
    type: Object,
    required: true
  }
});

// Component dimensions
const width = 800
const height = 400

// EQ state - This will be populated by onMounted from API data
const eqPoints = reactive([])

const selectedPoint = ref(null) // Initialize to null, set after points are loaded
const dragState = reactive({
  isDragging: false,
  pointIndex: null,
  startX: 0,
  startY: 0,
  startFreq: 0,
  startGain: 0
})

let nextId = 4

// Frequency conversion (logarithmic scale)
const frequencyToX = (freq) => {
  const minFreq = Math.log10(20)
  const maxFreq = Math.log10(20000)
  const logFreq = Math.log10(freq)
  return ((logFreq - minFreq) / (maxFreq - minFreq)) * width
}

const xToFrequency = (x) => {
  const minFreq = Math.log10(20)
  const maxFreq = Math.log10(20000)
  const ratio = x / width
  return Math.pow(10, minFreq + ratio * (maxFreq - minFreq))
}

// Gain conversion (linear scale)
const gainToY = (gain) => {
  const maxGain = 15
  const minGain = -15
  return height - ((gain - minGain) / (maxGain - minGain)) * height
}

const yToGain = (y) => {
  const maxGain = 15
  const minGain = -15
  const ratio = (height - y) / height
  return minGain + ratio * (maxGain - minGain)
}

// Grid lines
const frequencyGridLines = computed(() => {
  const frequencies = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
  return frequencies.map(freq => ({
    value: freq,
    x: frequencyToX(freq),
    label: freq >= 1000 ? `${freq/1000}k` : `${freq}`
  }))
})

const gainGridLines = computed(() => {
  const gains = [-15, -10, -5, 0, 5, 10, 15]
  return gains.map(gain => ({
    value: gain,
    y: gainToY(gain),
    label: gain > 0 ? `+${gain}` : `${gain}`
  }))
})

// EQ curve calculation
const curvePath = computed(() => {
  const points = []
  const steps = 200
  for (let i = 0; i <= steps; i++) {
    const x = (i / steps) * width
    const freq = xToFrequency(x)
    let totalGain = 0
    // Calculate combined response from all EQ points
    eqPoints.forEach(point => {
      const gain = calculateBellFilter(freq, point.frequency, point.gain, point.q)
      totalGain += gain
    })

    const y = gainToY(totalGain)
    points.push(`${x},${y}`)
  }

  return `M ${points.join(' L ')}`
})

// Bell filter calculation (peaking EQ)
const calculateBellFilter = (freq, centerFreq, gain, q) => {
  const w = 2 * Math.PI * freq
  const w0 = 2 * Math.PI * centerFreq
  const A = Math.pow(10, gain / 40)
  const alpha = Math.sin(w0) / (2 * q)

  // Simplified bell filter response
  const ratio = freq / centerFreq
  const bandwidth = 1 / q
  const response = gain / (1 + Math.pow((ratio - 1/ratio) / bandwidth, 2))

  return response
}

// Point management
const addPoint = () => {
  if (eqPoints.length >= 10) return

  const newPoint = {
    id: nextId++,
    frequency: 1000,
    gain: 0,
    q: 1
  }

  eqPoints.push(newPoint)
  selectedPoint.value = eqPoints.length - 1
}

const removePoint = (index) => {
  if (eqPoints.length <= 1) return

  eqPoints.splice(index, 1)

  if (selectedPoint.value >= eqPoints.length) {
    selectedPoint.value = eqPoints.length - 1
  }
}

// Drag functionality
const startDrag = (index, event) => {
  event.preventDefault()
  selectedPoint.value = index

  const clientX = event.touches ? event.touches[0].clientX : event.clientX
  const clientY = event.touches ? event.touches[0].clientY : event.clientY

  dragState.isDragging = true
  dragState.pointIndex = index
  dragState.startX = clientX
  dragState.startY = clientY
  dragState.startFreq = eqPoints[index].frequency
  dragState.startGain = eqPoints[index].gain
}

const onMouseMove = (event) => {
  if (!dragState.isDragging) return

  const clientX = event.touches ? event.touches[0].clientX : event.clientX
  const clientY = event.touches ? event.touches[0].clientY : event.clientY

  const deltaX = clientX - dragState.startX
  const deltaY = clientY - dragState.startY

  const currentX = frequencyToX(dragState.startFreq) + deltaX
  const currentY = gainToY(dragState.startGain) + deltaY

  const newFreq = Math.max(20, Math.min(20000, xToFrequency(currentX)))
  const newGain = Math.max(-15, Math.min(15, yToGain(currentY)))

  eqPoints[dragState.pointIndex].frequency = newFreq
  eqPoints[dragState.pointIndex].gain = newGain
}

const stopDrag = () => {
  dragState.isDragging = false
  dragState.pointIndex = null
}

// API related state
const currentPresetName = ref(null);
const currentPresetDetails = ref(null); // To store the full preset object
const eqType = ref('room'); // Default to 'room', can be made dynamic later
const currentSPL = ref(85);  // Default SPL, can be made dynamic later
const availablePresets = ref([]);

// Event listeners
onMounted(async () => {
  document.addEventListener('mousemove', onMouseMove);
  document.addEventListener('mouseup', stopDrag);
  document.addEventListener('touchmove', onMouseMove);
  document.addEventListener('touchend', stopDrag);

  try {
    console.log("ParametricEQ mounted, apiClient:", props.apiClient);
    if (props.apiClient && typeof props.apiClient.getPresets === 'function') {
      availablePresets.value = await props.apiClient.getPresets();
      const currentPreset = availablePresets.value.find(p => p.isCurrent);

      if (currentPreset) {
        currentPresetName.value = currentPreset.name;
        currentPresetDetails.value = await props.apiClient.getPreset(currentPreset.name);
        console.log("Current preset loaded:", currentPresetDetails.value);

        if (currentPresetDetails.value && currentPresetDetails.value.eq &&
            currentPresetDetails.value.eq[eqType.value]) {

          const eqSettingsForSPL = currentPresetDetails.value.eq[eqType.value].find(
            eq => eq.spl === currentSPL.value
          );

          if (eqSettingsForSPL && eqSettingsForSPL.peqSet) {
            eqPoints.splice(0, eqPoints.length); // Clear existing default points
            eqSettingsForSPL.peqSet.forEach((p, index) => {
              eqPoints.push({
                id: index + 1, 
                frequency: p.frequency,
                gain: p.gain,
                q: p.q
              });
            });
            nextId = eqPoints.length + 1;
            if (eqPoints.length > 0) {
               selectedPoint.value = 0;
            } else {
               selectedPoint.value = null;
            }
            console.log("EQ points updated from preset:", eqPoints);
          } else {
            console.log(`No PEQ set found for preset '${currentPresetName.value}', type '${eqType.value}', SPL ${currentSPL.value}. Using default points.`);
            if(eqPoints.length === 0) {
               addPoint(); 
            }
          }
        } else {
           console.log("No EQ data in current preset for current type/SPL. Using default points.");
           if(eqPoints.length === 0) {
               addPoint();
            }
        }
      } else {
        console.log("No current preset found. Using default EQ points.");
        if(eqPoints.length === 0) { 
           eqPoints.splice(0, eqPoints.length,
               { id: 1, frequency: 100, gain: 0, q: 1 },
               { id: 2, frequency: 1000, gain: 0, q: 1 },
               { id: 3, frequency: 10000, gain: 0, q: 1 }
           );
           nextId = 4;
           selectedPoint.value = 0;
        }
      }
    } else {
      console.error("API client is not available or getPresets is not a function.");
       // Fallback to default points if API is not available for some reason
        if(eqPoints.length === 0) { 
            eqPoints.splice(0, eqPoints.length,
                { id: 1, frequency: 100, gain: 0, q: 1 },
                { id: 2, frequency: 1000, gain: 0, q: 1 },
                { id: 3, frequency: 10000, gain: 0, q: 1 }
            );
            nextId = 4;
            selectedPoint.value = 0;
        }
    }
  } catch (error) {
    console.error("Error loading presets or EQ data:", error);
     if(eqPoints.length === 0) { 
       eqPoints.splice(0, eqPoints.length,
           { id: 1, frequency: 100, gain: 0, q: 1 },
           { id: 2, frequency: 1000, gain: 0, q: 1 },
           { id: 3, frequency: 10000, gain: 0, q: 1 }
       );
       nextId = 4;
       selectedPoint.value = 0;
     }
  }
});

watch(eqPoints, async (newEqPoints) => {
  if (currentPresetName.value && props.apiClient && typeof props.apiClient.setEQ === 'function') {
    const peqSetForAPI = newEqPoints.map(p => ({
      frequency: p.frequency,
      gain: p.gain,
      q: p.q
    }));
    try {
      console.log(`Setting EQ for preset: ${currentPresetName.value}, type: ${eqType.value}, SPL: ${currentSPL.value}`);
      await props.apiClient.setEQ(currentPresetName.value, eqType.value, currentSPL.value, peqSetForAPI);
      console.log("EQ successfully updated via API.");
    } catch (error) {
      console.error("Error updating EQ via API:", error);
    }
  }
}, { deep: true });

onUnmounted(() => {
  document.removeEventListener('mousemove', onMouseMove)
  document.removeEventListener('mouseup', stopDrag)
  document.removeEventListener('touchmove', onMouseMove)
  document.removeEventListener('touchend', stopDrag)
})
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

.control-group label {
  display: block;
  margin-bottom: 5px;
  font-size: 14px;
  color: #ccc;
}

.control-row {
  display: flex;
  gap: 10px;
  align-items: center;
}

.control-group input[type="range"] {
  flex: 1;
  height: 4px;
  background: #444;
  border-radius: 2px;
  outline: none;
  -webkit-appearance: none;
}

.control-group input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 16px;
  height: 16px;
  background: #0088ff;
  border-radius: 50%;
  cursor: pointer;
}

.control-group input[type="range"]::-moz-range-thumb {
  width: 16px;
  height: 16px;
  background: #0088ff;
  border-radius: 50%;
  cursor: pointer;
  border: none;
}

.number-input {
  width: 80px;
  padding: 4px 8px;
  background: #333;
  border: 1px solid #555;
  border-radius: 4px;
  color: #fff;
  font-size: 12px;
  text-align: center;
}

.number-input:focus {
  outline: none;
  border-color: #0088ff;
}

</style>
