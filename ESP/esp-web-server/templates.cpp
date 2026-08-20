#include "templates.h"
#include <string.h>

// --- Building blocks (mirror templates.js) ---

// Shared crossover point definitions. locked points are view-only in the UI
// and reject writes without confirm=true (driver protection).
static CrossoverPoint makeXo(const char* id, uint16_t freq, bool locked,
                             uint16_t min, uint16_t max) {
    CrossoverPoint point; // type defaults to LR4
    strlcpy(point.id, id, sizeof(point.id));
    point.freq = freq;
    point.locked = locked;
    point.min = min;
    point.max = max;
    return point;
}

static const CrossoverPoint SUB_XO = makeXo("sub_xo", 80, false, 40, 500);
static const CrossoverPoint MID_XO = makeXo("mid_xo", 400, true, 100, 2000);
static const CrossoverPoint TWT_XO = makeXo("twt_xo", 2500, true, 800, 8000);

// Source mixes
static const double LEFT_L = 1.0, LEFT_R = 0.0;
static const double RIGHT_L = 0.0, RIGHT_R = 1.0;
static const double MONO_L = 0.5, MONO_R = 0.5;

// A disabled, silent output slot ("Out N")
static void emptyOutput(Output& out, int index) {
    out = Output();
    snprintf(out.label, sizeof(out.label), "Out %d", index + 1);
}

// An enabled output with a label and source mix; filters/floor added by callers
static Output& output(Preset& preset, int index, const char* label, double srcLeft, double srcRight) {
    Output& out = preset.outputs[index];
    emptyOutput(out, index);
    out.enabled = true;
    strlcpy(out.label, label, sizeof(out.label));
    out.sourceLeft = srcLeft;
    out.sourceRight = srcRight;
    return out;
}

static void addCrossover(Preset& preset, const CrossoverPoint& point) {
    if (preset.num_crossovers < MAX_CROSSOVER_POINTS) {
        preset.crossovers[preset.num_crossovers++] = point;
    }
}

static void xoverRef(FilterSection& section, const char* id) {
    section.mode = FilterMode::Xover;
    strlcpy(section.xover, id, sizeof(section.xover));
}

// Default spl=0 input EQ set: three flat points at 100/1000/10000 Hz
static void defaultInputEq(InputEq& eq) {
    eq.enabled = false;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        eq.sets[i] = PEQSet();
    }
    eq.sets[0].spl = 0;
    eq.sets[0].num_points = 3;
    for (int k = 0; k < 3; k++) {
        eq.sets[0].points[k] = PEQPoint();
        eq.sets[0].points[k].freq = 100 * pow(10, k);
    }
}

// --- Template output/crossover factories ---

static void build_2_0(Preset& p) {
    output(p, 0, "Left", LEFT_L, LEFT_R);
    output(p, 1, "Right", RIGHT_L, RIGHT_R);
}

static void build_2_1(Preset& p) {
    addCrossover(p, SUB_XO);
    xoverRef(output(p, 0, "Left", LEFT_L, LEFT_R).hp, "sub_xo");
    xoverRef(output(p, 1, "Right", RIGHT_L, RIGHT_R).hp, "sub_xo");
    xoverRef(output(p, 2, "Sub", MONO_L, MONO_R).lp, "sub_xo");
}

static void build_2_2(Preset& p) {
    addCrossover(p, SUB_XO);
    xoverRef(output(p, 0, "Left", LEFT_L, LEFT_R).hp, "sub_xo");
    xoverRef(output(p, 1, "Right", RIGHT_L, RIGHT_R).hp, "sub_xo");
    xoverRef(output(p, 2, "Sub 1", MONO_L, MONO_R).lp, "sub_xo");
    xoverRef(output(p, 3, "Sub 2", MONO_L, MONO_R).lp, "sub_xo");
}

static void build_2way_sub(Preset& p) {
    addCrossover(p, SUB_XO);
    addCrossover(p, TWT_XO);
    Output& lw = output(p, 0, "L Woofer", LEFT_L, LEFT_R);
    xoverRef(lw.hp, "sub_xo");
    xoverRef(lw.lp, "twt_xo");
    Output& rw = output(p, 1, "R Woofer", RIGHT_L, RIGHT_R);
    xoverRef(rw.hp, "sub_xo");
    xoverRef(rw.lp, "twt_xo");
    Output& lt = output(p, 2, "L Tweeter", LEFT_L, LEFT_R);
    xoverRef(lt.hp, "twt_xo");
    lt.hpFloor = 800;
    Output& rt = output(p, 3, "R Tweeter", RIGHT_L, RIGHT_R);
    xoverRef(rt.hp, "twt_xo");
    rt.hpFloor = 800;
    xoverRef(output(p, 4, "Sub", MONO_L, MONO_R).lp, "sub_xo");
}

// The shared 3-way L/R sections of 3way and 3way-2sub. When withSubXo is
// true the lows are band-passed between sub_xo and mid_xo (3way-2sub);
// otherwise they run full-range below mid_xo.
static void build_3way_mains(Preset& p, bool withSubXo) {
    Output& ll = output(p, 0, "L Low", LEFT_L, LEFT_R);
    if (withSubXo) xoverRef(ll.hp, "sub_xo");
    xoverRef(ll.lp, "mid_xo");
    Output& rl = output(p, 1, "R Low", RIGHT_L, RIGHT_R);
    if (withSubXo) xoverRef(rl.hp, "sub_xo");
    xoverRef(rl.lp, "mid_xo");
    Output& lm = output(p, 2, "L Mid", LEFT_L, LEFT_R);
    xoverRef(lm.hp, "mid_xo");
    xoverRef(lm.lp, "twt_xo");
    lm.hpFloor = 100;
    Output& rm = output(p, 3, "R Mid", RIGHT_L, RIGHT_R);
    xoverRef(rm.hp, "mid_xo");
    xoverRef(rm.lp, "twt_xo");
    rm.hpFloor = 100;
    Output& lh = output(p, 4, "L High", LEFT_L, LEFT_R);
    xoverRef(lh.hp, "twt_xo");
    lh.hpFloor = 800;
    Output& rh = output(p, 5, "R High", RIGHT_L, RIGHT_R);
    xoverRef(rh.hp, "twt_xo");
    rh.hpFloor = 800;
}

static void build_3way(Preset& p) {
    addCrossover(p, MID_XO);
    addCrossover(p, TWT_XO);
    build_3way_mains(p, false);
}

static void build_3way_2sub(Preset& p) {
    addCrossover(p, SUB_XO);
    addCrossover(p, MID_XO);
    addCrossover(p, TWT_XO);
    build_3way_mains(p, true);
    xoverRef(output(p, 6, "Sub 1", MONO_L, MONO_R).lp, "sub_xo");
    xoverRef(output(p, 7, "Sub 2", MONO_L, MONO_R).lp, "sub_xo");
}

// --- Template table (order defines GET /templates order) ---

typedef void (*TemplateBuilder)(Preset&);

struct TemplateEntry {
    TemplateInfo info;
    TemplateBuilder build;
};

static const TemplateEntry TEMPLATES[] = {
    {{"2.0", "2.0 Stereo",
      "Left and right full-range speakers, no crossover.", 2}, build_2_0},
    {{"2.1", "2.1 Stereo + Sub",
      "Left and right speakers with a mono subwoofer and adjustable sub crossover.", 3}, build_2_1},
    {{"2.2", "2.2 Stereo + Dual Subs",
      "Left and right speakers with two mono subwoofers (independent gain and delay).", 4}, build_2_2},
    {{"2way-sub", "2-Way Active + Sub",
      "Active 2-way speakers (woofer + tweeter per side) plus a mono subwoofer.", 5}, build_2way_sub},
    {{"3way", "3-Way Active",
      "Active 3-way speakers: low, mid and high driver per side.", 6}, build_3way},
    {{"3way-2sub", "3-Way Active + Dual Subs",
      "Active 3-way speakers plus two mono subwoofers - all eight outputs.", 8}, build_3way_2sub},
};

int template_count() {
    return sizeof(TEMPLATES) / sizeof(TEMPLATES[0]);
}

const TemplateInfo& template_info(int index) {
    return TEMPLATES[index].info;
}

bool build_preset_from_template(Preset& preset, const char* templateId) {
    const TemplateEntry* entry = nullptr;
    for (int i = 0; i < template_count(); i++) {
        if (strcmp(TEMPLATES[i].info.id, templateId) == 0) {
            entry = &TEMPLATES[i];
            break;
        }
    }
    if (entry == nullptr) {
        return false;
    }

    // Reset everything but the name in place - a whole-Preset temporary
    // (~3 KB) doesn't fit the 4 KB httpd task stacks this runs on
    strlcpy(preset.templateId, entry->info.id, sizeof(preset.templateId));
    preset.num_crossovers = 0;
    for (int i = 0; i < MAX_CROSSOVER_POINTS; i++) {
        preset.crossovers[i] = CrossoverPoint();
    }
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        emptyOutput(preset.outputs[i], i);
    }
    defaultInputEq(preset.inputEq);
    preset.delaysEnabled = false;
    preset.firEnabled = false;
    preset.volume = PRESET_VOLUME_DEFAULT;

    entry->build(preset);
    return true;
}
