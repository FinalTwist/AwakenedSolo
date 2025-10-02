// AwakenedSolo/src/spec_gangster.hpp
#pragma once
#include "structs.hpp"

// Spec entry point
SPECIAL(gangster_spec);

// Hooks that mirror your existing Adventurer hooks.
// Call these exactly where you currently call adventurer_on_pc_login/enter_room.
void gangster_on_pc_login(struct char_data* ch);
void gangster_on_pc_enter_room(struct char_data* ch, struct room_data* room);