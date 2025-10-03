// AwakenedSolo/src/spec_gangster.cpp
//
// Gangster NPCs:
// - Generation: identical to Adventurers via adventurer_configure_like(mob)
//   (tier, archetype, skills, gear, nuyen/credstick, base flags).
// - Deltas: aggressive, not sentinel, stay in zone, not wimpy, killable.
// - Tagging: appends "gangster" to keywords so we can identify them without mutating proto spec.
// - Spawning: on player login and on player zone entry, roll 8% for each room
//   in that zone whose name/description contains "street" or "alley" (case-insensitive).
// - Cleanup: despawn after zone inactivity; strip items before extract to avoid litter.

// Use adventurer drip-queue helpers
extern bool adv_zone_spawn_cooldown_allows(int zone);
extern bool adv_schedule_room_spawn(struct room_data* room, bool is_gangster);
extern void adv_record_zone_spawn(int zone);


#include "structs.hpp"
#include "awake.hpp"
#include "utils.hpp"
#include "comm.hpp"
#include "db.hpp"
#include "interpreter.hpp"
#include "constants.hpp"
#include "handler.hpp"      // char_to_room, extract_obj, extract_char, GET_EQ, etc.

#include <vector>
#include <unordered_map>
#include <cctype>
#include <cstring>

#include "spec_adventurer.hpp"  // adventurer_configure_like(), is_adventurer()

// Fresh-spawn tag helpers from spec_adventurer.cpp
extern void adv_kw_remove(struct char_data *mob, const char *kw);
extern void adv_kw_append(struct char_data *mob, const char *kw);
extern const char *ADV_FRESH_KW;
#include "spec_gangster.hpp"

extern void adv_boot_cleanup_if_needed();  // defined in spec_adventurer.cpp

// ------------------------ Config ------------------------
static int  g_despawn_inactive_secs = 900;   // override via lib/etc/gangster_spawn.txt
static int  g_template_vnum         = 20022; // reuse Adventurer template unless configured

static std::unordered_map<int /*zone vnum*/, time_t> g_zone_last_activity;

static void gangster_read_spawn_cfg() {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;

  if (FILE *fl = fopen("lib/etc/gangster_spawn.txt", "r")) {
    char line[512];
    while (fgets(line, sizeof(line), fl)) {
      if (*line == '#' || *line == ';' || *line == '\n') continue;
      int vali = 0;
      if (sscanf(line, "template_mobile_vnum = %d", &vali) == 1) { g_template_vnum = vali; continue; }
      if (sscanf(line, "despawn_zone_inactivity_seconds = %d", &vali) == 1) { g_despawn_inactive_secs = MAX(0, vali); continue; }
    }
    fclose(fl);
  }
}

// ------------------------ Helpers ------------------------

static inline bool gs_keywords_alias_proto(struct char_data* mob) {
  if (!mob || !IS_NPC(mob)) return false;
  int rnum = GET_MOB_RNUM(mob);
  if (rnum < 0 || rnum > top_of_mobt) return false;
  return mob->player.physical_text.keywords == mob_proto[rnum].player.physical_text.keywords;
}

static bool ci_contains(const char* hay, const char* needle) {
  if (!hay || !needle || !*needle) return false;
  const size_t nlen = strlen(needle);
  for (const char* p = hay; *p; ++p) {
    size_t i = 0;
    while (i < nlen && p[i] && std::tolower((unsigned char)p[i]) == std::tolower((unsigned char)needle[i])) ++i;
    if (i == nlen) return true;
  }
  return false;
}

static bool room_is_streetlike(struct room_data* room) {
  if (!room) return false;
  const char* nm = room->name ? room->name : "";
  const char* ds = room->description ? room->description : "";
  return ci_contains(nm, "street") || ci_contains(nm, "alley")
      || ci_contains(ds, "street") || ci_contains(ds, "alley");
}

// Apply gangster-specific flag deltas after Adventurer generator runs.
static void gangster_apply_flag_deltas(struct char_data* mob) {
  MOB_FLAGS(mob).RemoveBit(MOB_SENTINEL);
  MOB_FLAGS(mob).SetBit(MOB_STAY_ZONE);
  MOB_FLAGS(mob).RemoveBit(MOB_WIMPY);
  MOB_FLAGS(mob).RemoveBit(MOB_NOKILL);
  MOB_FLAGS(mob).SetBit(MOB_AGGRESSIVE);
}

// Tag the mob so we can identify it later without touching proto specs.
static void gangster_append_keyword(struct char_data* mob) {
  const char* cur = mob->player.physical_text.keywords;
  if (!cur) return;
  // Avoid double-appending if already tagged.
  if (ci_contains(cur, "gangster")) return;

  char buf[MAX_INPUT_LENGTH];
  snprintf(buf, sizeof(buf), "%s gangster", cur);
  // Only free if not aliasing the proto.
  if (!gs_keywords_alias_proto(mob)) {
    DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.keywords);
  }
  mob->player.physical_text.keywords = str_dup(buf);
}

// Identify a gangster by the keyword tag (safe; no proto mutations).
static bool is_gangster(struct char_data* ch) {
  if (!ch || !IS_NPC(ch)) return false;
  const char* kw = ch->player.physical_text.keywords;
  return kw && ci_contains(kw, "gangster");
}

// ------------------------ Spec entry (no command handling) ------------------------
SPECIAL(gangster_spec) {
  if (!IS_NPC(ch)) return FALSE;
  return FALSE;
}

// ------------------------ Spawning (8% per street/alley room) ------------------------

void gangster_on_pc_login(struct char_data* ch) {
  if (!ch || IS_NPC(ch)) return;
  gangster_read_spawn_cfg();

  struct room_data* here = get_ch_in_room(ch);
  if (!here) return;
  const int z = here->zone;

  bool enqueued_any = false;  // <-- add this
  std::vector<rnum_t> candidates;
  candidates.reserve(64);

  for (rnum_t rr = 0; rr <= top_of_world; ++rr) {
    if (!VALID_ROOM_RNUM(rr)) continue;
    struct room_data* R = &world[rr];
    if (!R || R->zone != z) continue;
    if (ROOM_FLAGGED(R, ROOM_NOMOB)) continue;
    if (!room_is_streetlike(R)) continue;
    candidates.push_back(rr);
  }
  if (candidates.empty()) { 
    if (enqueued_any) { adv_record_zone_spawn(z); }
    g_zone_last_activity[z] = time(0); return; 
  }

  const int chance = 8; // 8% per eligible room
  for (rnum_t rr : candidates) {
    if (number(1, 100) > chance) continue;

    // read_mobile: (vnum_or_rnum, type) — in your tree this is two args.
    struct char_data* mob = read_mobile(g_template_vnum, VIRTUAL);
    if (!mob) continue;

    // Place mob in the destination room.
    char_to_room(mob, &world[rr]);

    // Configure identically to Adventurers, then apply gangster deltas and tag.
    // Mark fresh, then configure once. }

  if (enqueued_any) { adv_record_zone_spawn(z); }
  g_zone_last_activity[z] = time(0);
  }
}


// Drip-spawn a single gangster in the given room (called from adventurer maintainer)
void gangster_spawn_one_in_room(struct room_data* room) {
  if (!room) return;
  // Ensure template is ready in the same way the immediate path would
  if (real_mobile(g_template_vnum) < 0) {
    // Fallback: skip if template not loaded yet
    return;
  }
  struct char_data* mob = read_mobile(g_template_vnum, VIRTUAL);
  if (!mob) return;
  char_to_room(mob, room);
  // Configure like an adventurer, then apply gangster specifics
  adv_kw_append(mob, ADV_FRESH_KW);
  (void) adventurer_configure_like(mob);
  gangster_apply_flag_deltas(mob);
  gangster_append_keyword(mob);
}

void gangster_on_pc_enter_room(struct char_data* ch, struct room_data* room) {
  if (!ch || IS_NPC(ch) || !room) return;
  gangster_read_spawn_cfg();

  const int z = room->zone;
  bool enqueued_any = false;
  if (!adv_zone_spawn_cooldown_allows(z)) { return; }

  std::vector<rnum_t> candidates;
  candidates.reserve(64);

  for (rnum_t rr = 0; rr <= top_of_world; ++rr) {
    if (!VALID_ROOM_RNUM(rr)) continue;
    struct room_data* R = &world[rr];
    if (!R || R->zone != z) continue;
    if (ROOM_FLAGGED(R, ROOM_NOMOB)) continue;
    if (!room_is_streetlike(R)) continue;
    candidates.push_back(rr);
  }
  if (candidates.empty()) { if (enqueued_any) { adv_record_zone_spawn(z); }
  g_zone_last_activity[z] = time(0); return; }

  const int chance = 8; // 8% per eligible room
  for (rnum_t rr : candidates) {
    if (number(1, 100) > chance) continue;

    if (adv_schedule_room_spawn(&world[rr], /*is_gangster=*/true)) { enqueued_any = true; } }

  if (enqueued_any) { adv_record_zone_spawn(z); }
  g_zone_last_activity[z] = time(0);
}

// ------------------------ Lightweight maintain: despawn + combat draw ------------------------

static void gangster_maybe_draw_on_combat(struct char_data *mob) {
  if (!FIGHTING(mob)) return;
  if (GET_EQ(mob, WEAR_WIELD)) return;
  command_interpreter(mob, const_cast<char*>("draw"), GET_CHAR_NAME(mob));
}

static void gangster_maintain() {
  gangster_read_spawn_cfg();

  time_t now = time(0);

  // Despawn gangsters from zones that have gone inactive.
  for (auto it = g_zone_last_activity.begin(); it != g_zone_last_activity.end(); ++it) {
    const int z = it->first;
    const time_t last = it->second;
    if (now - last < g_despawn_inactive_secs) continue;

    for (struct char_data *ch = character_list; ch; ch = ch->next_in_character_list) {
      if (!is_gangster(ch)) continue;
      struct room_data *r = get_ch_in_room(ch);
      if (!r || r->zone != z) continue;

      // Strip items to avoid despawn litter; then extract.
      while (ch->carrying) extract_obj(ch->carrying);
      for (int i = 0; i < NUM_WEARS; ++i) {
        if (GET_EQ(ch, i)) extract_obj(GET_EQ(ch, i));
      }
      extract_char(ch);
    }

    it->second = now; // avoid repeated work same tick
  }

  // Per-gangster upkeep (weapon discipline in combat).
  for (struct char_data* ch = character_list; ch; ch = ch->next_in_character_list) {
    if (!is_gangster(ch)) continue;
    if (!AWAKE(ch)) continue;
    if (!get_ch_in_room(ch)) continue;
    gangster_maybe_draw_on_combat(ch);
  }
}

// Hook from the same place as Adventurer maintenance (e.g., mobile_activity()).
void gangster_global_maintain_hook() {
  gangster_maintain();
}