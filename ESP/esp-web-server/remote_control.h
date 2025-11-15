#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H

#include <Arduino.h>

class RemoteControl {
public:
    void setup();
    void loop();

private:
    void handle_ir_code(uint64_t code);
    void next_preset();
    void previous_preset();
    void apply_preset();

    unsigned long _preset_selection_time = 0;
    int _selected_preset_index = -1;
    
    unsigned long _volume_button_held_time = 0;
    unsigned long _last_volume_increment_time = 0;
    unsigned long _last_ir_code_time = 0;
    int _last_volume_action = -1;
};

#endif