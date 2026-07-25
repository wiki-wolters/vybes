#ifndef TEMPLATES_H
#define TEMPLATES_H

// V1 preset templates - the factories that generate a preset's output
// channels, crossover points and safety floors. This is a verbatim port of
// mock-server/templates.js (which the contract suite treats as the spec);
// keep the two in sync.
//
// A template is a factory, not a mode: applying one generates the initial
// config, after which every field remains editable (edits beyond what the
// template's simple view can express flip preset.templateId to "custom").

#include "config.h"

#define DEFAULT_TEMPLATE_ID "2.1"

struct TemplateInfo {
    const char* id;
    const char* label;
    const char* description;
    uint8_t outputsUsed;
};

// Template metadata for GET /templates, in declaration order
int template_count();
const TemplateInfo& template_info(int index);

// Populate everything but the preset's name from the template.
// Returns false (preset untouched) for an unknown template id.
bool build_preset_from_template(Preset& preset, const char* templateId);

#endif // TEMPLATES_H
