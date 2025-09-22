#pragma once
#include "structs.hpp"
SPECIAL(adventurer_spec);
void adventurer_notify_social(struct char_data* actor, struct char_data* vict, int cmd);
void adventurer_maintain();
void adventurer_on_pc_enter_room(struct char_data* ch, struct room_data* room);
