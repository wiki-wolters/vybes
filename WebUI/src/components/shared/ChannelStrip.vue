<template>
  <div
    class="rounded-lg bg-vybes-dark-element/40 p-4 self-start"
    :class="{ 'opacity-60': !output.enabled }"
  >
    <!-- Collapsed header: index, label, enable, and a one-line summary -->
    <div class="flex items-center justify-between gap-2">
      <div class="flex items-center gap-2 min-w-0">
        <button
          v-if="output.enabled"
          type="button"
          class="p-1 -ml-1 flex-none text-vybes-text-secondary cursor-pointer"
          :aria-expanded="expanded"
          :aria-label="`${expanded ? 'Collapse' : 'Expand'} ${output.label}`"
          @click="$emit('toggle')"
        >
          <svg
            class="w-4 h-4 transition-transform duration-200"
            :class="{ 'rotate-90': expanded }"
            fill="none" stroke="currentColor" viewBox="0 0 24 24"
          >
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M9 5l7 7-7 7" />
          </svg>
        </button>
        <span class="text-xs font-mono tabular-nums text-vybes-text-secondary flex-none">{{ output.index + 1 }}</span>
        <input
          :value="output.label"
          type="text"
          maxlength="24"
          class="bg-transparent border-b border-transparent focus:border-vybes-accent focus:outline-none text-vybes-text-primary font-semibold min-w-0 w-full"
          :disabled="!output.enabled"
          @change="store.setOutputLabel(output.index, $event.target.value || output.label)"
        />
      </div>
      <ToggleSwitch
        :model-value="output.enabled"
        :aria-label="`Enable output ${output.index + 1}`"
        @update:modelValue="store.setOutputEnabled(output.index, $event)"
      />
    </div>

    <!-- Summary stands in for the collapsed contents -->
    <button
      v-if="output.enabled && !expanded"
      type="button"
      class="mt-1.5 w-full text-left text-xs text-vybes-text-secondary tabular-nums cursor-pointer"
      :aria-label="`Expand ${output.label}`"
      @click="$emit('toggle')"
    >{{ summary }}</button>

    <template v-if="output.enabled && expanded">
      <!-- Source mix -->
      <div class="strip-row mt-3">
        <span class="strip-label">Source</span>
        <SourceMixInput
          :model-value="output.source"
          @update:modelValue="store.setOutputSource(output.index, $event)"
        />
      </div>

      <!-- Crossover sections -->
      <div class="strip-row">
        <span class="strip-label">High-pass
          <span v-if="output.hpFloor > 0" class="text-vybes-accent" :title="`Protected: high-pass cannot go below ${output.hpFloor} Hz`">
            &ge;{{ formatValue(output.hpFloor, 'Hz', 0) }}
          </span>
        </span>
        <FilterSelect which="hp" :output="output" />
      </div>
      <div class="strip-row">
        <span class="strip-label">Low-pass</span>
        <FilterSelect which="lp" :output="output" />
      </div>

      <!-- Gain / delay -->
      <div class="strip-row">
        <RangeSlider
          :model-value="output.gainDb"
          label="Gain"
          :min="-40" :max="10" :step="0.1" unit="dB" :decimals="1"
          @update:modelValue="store.setOutputGain(output.index, $event)"
        />
      </div>
      <div class="strip-row">
        <SpeakerDelayInput
          title="Delay"
          :model-value="output.delayUs"
          @update:modelValue="store.setOutputDelay(output.index, $event)"
        />
      </div>

      <!-- FIR -->
      <div class="strip-row">
        <SelectGroup
          v-if="store.firFiles.length > 0"
          :model-value="output.fir"
          label="FIR filter"
          @update:modelValue="store.setOutputFir(output.index, $event)"
        >
          <option value="">None</option>
          <option v-if="output.fir && !store.firFiles.includes(output.fir)" :value="output.fir">
            {{ output.fir }} (missing)
          </option>
          <option v-for="file in store.firFiles" :key="file" :value="file">{{ file }}</option>
        </SelectGroup>
        <InputGroup
          v-else
          :model-value="output.fir"
          label="FIR filter"
          @update:modelValue="store.setOutputFir(output.index, $event)"
        />
      </div>

      <!-- PEQ -->
      <CollapsibleSection
        :title="`PEQ (${output.peq.length}/10)${output.eqEnabled === false ? ' · bypassed' : ''}`"
        :toggleable="false"
        :animate="false"
        :start-expanded="false"
        class="!mb-0"
      >
        <ToggleSwitch
          :model-value="output.eqEnabled !== false"
          label="EQ enabled"
          class="mb-2"
          @update:modelValue="store.setOutputEqEnabled(output.index, $event)"
        />
        <ParametricEQ
          :peq-points="peqPoints"
          :preset-name="store.presetName"
          :output="output.index"
          :max-points="10"
          @change="store.saveOutputEq(output.index, $event)"
        />
      </CollapsibleSection>

      <!-- Mute / invert -->
      <div class="flex items-center gap-6 mt-3">
        <ToggleSwitch
          :model-value="output.mute"
          label="Mute"
          @update:modelValue="store.setOutputMute(output.index, $event)"
        />
        <ToggleSwitch
          :model-value="output.invert"
          label="Invert"
          @update:modelValue="store.setOutputInvert(output.index, $event)"
        />
      </div>
    </template>
  </div>
</template>

<script setup>
import { computed, h } from 'vue';
import RangeSlider from './RangeSlider.vue';
import SelectGroup from './SelectGroup.vue';
import InputGroup from './InputGroup.vue';
import ToggleSwitch from './ToggleSwitch.vue';
import SpeakerDelayInput from './SpeakerDelayInput.vue';
import SourceMixInput from './SourceMixInput.vue';
import CollapsibleSection from './CollapsibleSection.vue';
import ParametricEQ from '../ParametricEQ.vue';
import { usePresetStore } from '../../stores/preset.js';
import { formatValue } from '../../utilities.js';

const props = defineProps({
  /** Output object from the store, including its `index` */
  output: { type: Object, required: true },
  /** Accordion state; the parent owns it so it can enforce the mobile rule */
  expanded: { type: Boolean, default: false },
});

defineEmits(['toggle']);

const store = usePresetStore();

// What the collapsed strip has to convey on one line:
// "Left · HP 80 Hz · LP off · 0 dB · 2 PEQ"
const summary = computed(() => {
  const o = props.output;
  const parts = [sourceLabel(o.source), filterLabel(o.hp, 'HP'), filterLabel(o.lp, 'LP'),
    formatValue(o.gainDb, 'dB', 1)];
  if (o.delayUs > 0) parts.push(formatValue(o.delayUs, 'µs', 0));
  if (o.fir) parts.push('FIR');
  if (o.peq.length) parts.push(`${o.peq.length} PEQ${o.eqEnabled === false ? ' (byp)' : ''}`);
  if (o.mute) parts.push('muted');
  if (o.invert) parts.push('inverted');
  return parts.join(' · ');
});

function sourceLabel({ left, right }) {
  if (left > 0 && right > 0) {
    return left === right ? 'Mono' : `L ${Math.round(left * 100)}/R ${Math.round(right * 100)}`;
  }
  if (left > 0) return 'Left';
  if (right > 0) return 'Right';
  return 'No source';
}

function filterLabel(filter, prefix) {
  if (!filter || filter.mode === 'off') return `${prefix} off`;
  const freq = filter.mode === 'manual'
    ? filter.freq
    : store.crossovers.find((x) => x.id === filter.xover)?.freq;
  return freq ? `${prefix} ${formatValue(Number(freq), 'Hz', 0)}` : `${prefix} on`;
}

// ParametricEQ expects points with ids
const peqPoints = computed(() =>
  props.output.peq.map((p, id) => ({ id, ...p }))
);

/**
 * HP/LP section editor: mode select (Off / crossover points / Manual) plus
 * frequency and slope inputs in manual mode. Rendered here so the strip
 * stays one file; the store owns the API push and the server owns the
 * hpFloor verdict (rejections resync + surface via the error banner).
 */
const FilterSelect = {
  props: { which: String, output: Object },
  setup(fsProps) {
    const filter = computed(() => fsProps.output[fsProps.which]);

    const selection = computed(() => {
      if (filter.value.mode === 'xover') return `xover:${filter.value.xover}`;
      return filter.value.mode; // 'off' | 'manual'
    });

    function onModeChange(event) {
      const value = event.target.value;
      let next;
      if (value === 'off') {
        next = { mode: 'off' };
      } else if (value === 'manual') {
        next = { mode: 'manual', freq: currentFreq(), type: filter.value.type || 'LR4' };
      } else {
        next = { mode: 'xover', xover: value.slice('xover:'.length) };
      }
      store.setOutputFilter(fsProps.output.index, fsProps.which, next);
    }

    function currentFreq() {
      if (filter.value.mode === 'manual' && filter.value.freq) return filter.value.freq;
      const point = store.crossovers.find((x) => x.id === filter.value.xover);
      return point ? point.freq : 1000;
    }

    function onManualFreq(event) {
      const freq = Number(event.target.value);
      if (!Number.isFinite(freq)) return;
      store.setOutputFilter(fsProps.output.index, fsProps.which, {
        mode: 'manual', freq, type: filter.value.type || 'LR4',
      });
    }

    function onManualType(event) {
      store.setOutputFilter(fsProps.output.index, fsProps.which, {
        mode: 'manual', freq: currentFreq(), type: event.target.value,
      });
    }

    const selectClass = 'bg-vybes-dark-input text-vybes-text-primary text-sm rounded-md px-2 py-1.5 w-full';

    return () => {
      const children = [
        h('select', { class: selectClass, value: selection.value, onChange: onModeChange }, [
          h('option', { value: 'off' }, 'Off'),
          ...store.crossovers.map((point) =>
            h('option', { value: `xover:${point.id}` }, `${XO_TITLES[point.id] || point.id} (${point.freq} Hz)`)
          ),
          h('option', { value: 'manual' }, 'Manual'),
        ]),
      ];
      if (filter.value.mode === 'manual') {
        children.push(
          h('div', { class: 'flex gap-2 mt-1.5' }, [
            h('input', {
              class: selectClass, type: 'number', min: 20, max: 20000,
              value: filter.value.freq, onChange: onManualFreq, 'aria-label': 'Frequency (Hz)',
            }),
            h('select', {
              class: selectClass, value: filter.value.type || 'LR4', onChange: onManualType,
              'aria-label': 'Slope',
            }, ['LR2', 'LR4', 'BW2'].map((t) => h('option', { value: t }, t))),
          ])
        );
      }
      return h('div', { class: 'w-full' }, children);
    };
  },
};

const XO_TITLES = { sub_xo: 'Sub crossover', mid_xo: 'Mid crossover', twt_xo: 'Tweeter crossover' };
</script>

<style scoped>
@reference "../../style.css";

.strip-row {
  @apply mb-3;
}
.strip-label {
  @apply block text-sm text-vybes-text-secondary mb-1;
}
</style>
