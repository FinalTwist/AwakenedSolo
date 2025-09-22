/*
 * Adventurer NPC system (v2+confirm)
 * Pipeline:  zone-entry detect -> 5% spawn (eligible room in same zone)
 *  -> pick archetype (from archetypes.cpp) -> apply tier (skill + attribute MULT + gear)
 *  -> class-aware gear -> place -> supports socials, aggro, pickpocket+money.
 * Template mobile VNUM is read from lib/etc/adventurer_spawn.txt: template_mobile_vnum=####
 */
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

#include "awake.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "db.hpp"
#include "comm.hpp"
#include "handler.hpp"
#include "interpreter.hpp"
#include "constants.hpp"
#include "newmagic.hpp"
#include "archetypes.hpp"

#include "spec_adventurer.hpp"

// ---------- utils ----------
static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, b - a + 1);
}
static bool file_read_lines(const char* path, std::vector<std::string>& out) {
  out.clear();
  std::ifstream f(path);
  if (!f.good()) return false;
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back()=='\r' || line.back()==' ' || line.back()=='\t')) line.pop_back();
    if (line.empty() || line[0]=='#') continue;
    out.push_back(line);
  }
  return !out.empty();
}
static void split(const std::string& s, char sep, std::vector<std::string>& out) {
  out.clear();
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, sep)) out.push_back(trim(tok));
}
template<typename T> static T clamp_val(T v, T lo, T hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

// ---------- config & state ----------
static int adv_zone_entry_spawn_chance_pct = 5;
static int adv_zone_inactivity_despawn_seconds = 900;
static long adv_template_mobile_vnum = 25200; // default, overridable via config

static std::vector<std::string> adv_spawn_allow_flags;
static std::vector<std::string> adv_spawn_disallow_flags;

static std::unordered_map<long, time_t> adv_next_ok;
static std::unordered_map<long, int> adv_last_zone_for_pc;
static std::unordered_map<int, time_t> adv_zone_last_activity;

static std::vector<std::string> adv_first, adv_last;
static bool names_loaded = false;

struct Reply { enum Kind { SAY, EMOTE, ACTION } kind; std::string payload; };
static std::unordered_map<std::string, std::vector<Reply>> adv_replies_targeted;
static std::unordered_map<std::string, std::vector<Reply>> adv_replies_room;
static std::vector<Reply> adv_replies_default_targeted;
static std::vector<Reply> adv_replies_default_room;
static bool replies_loaded = false;

// ---------- Tiers ----------
struct Tier {
  int id=1;
  std::string name="Green";
  int weight=10;
  float skill_mult=1.0f;
  int skill_jitter=0;
  // NEW: multiplicative attribute scaling
  float attr_mult_bod=1.0f, attr_mult_qui=1.0f, attr_mult_rea=1.0f,
        attr_mult_str=1.0f, attr_mult_int=1.0f, attr_mult_wil=1.0f;
  // Back-compat additive bumps (optional)
  int bod_add=0, qui_add=0, rea_add=0, str_add=0, int_add=0, wil_add=0;
  std::vector<std::string> armor_aliases, weapon_aliases, ammo_aliases, init_ware_aliases;
};
static std::vector<Tier> adv_tiers;
static int adv_total_weight = 0;

static std::unordered_map<std::string, std::vector<int>> adv_alias_vnums;
struct ClassGear { std::vector<std::string> weapons, armor, ammo, initware; };
static std::unordered_map<std::string, ClassGear> adv_class_gear;

// ---------- forward decls ----------

static void load_tiers_if_needed();
static void load_alias_map_if_needed();
static void load_class_gear_if_needed();
static const Tier* pick_tier();
static bool is_room_ok_for_spawn(struct room_data *room) {
  if (!room) return FALSE;
  if (ROOM_IS_PEACEFUL(room)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_NOMOB)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_ARENA)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_STAFF_ONLY)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_ELEVATOR_SHAFT)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_STORAGE)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_TOO_CRAMPED_FOR_CHARACTERS)) return FALSE;
  if (ROOM_FLAGGED(room, ROOM_RADIATION)) return FALSE;
  return TRUE;
}
static void load_config_if_needed() {
  static bool loaded = false;
  if (loaded) { return; }
  loaded = true;

  // Load spawn config
  std::vector<std::string> lines;
  if (file_read_lines("lib/etc/adventurer_spawn.txt", lines)) {
    for (auto &ln : lines) {
      auto eq = ln.find('=');
      if (eq == std::string::npos) continue;
      auto key = trim(ln.substr(0, eq));
      auto val = trim(ln.substr(eq+1));
      if (key == "zone_entry_spawn_chance_percent") {
        int pct = atoi(val.c_str());
        if (pct >= 0 && pct <= 100) adv_zone_entry_spawn_chance_pct = pct;
      } else if (key == "despawn_zone_inactivity_seconds") {
        int s = atoi(val.c_str());
        if (s >= 60 && s <= 86400) adv_zone_inactivity_despawn_seconds = s;
      } else if (key == "spawn_allow_flags") {
        split(val, ',', adv_spawn_allow_flags);
      } else if (key == "spawn_disallow_flags") {
        split(val, ',', adv_spawn_disallow_flags);
      } else if (key == "template_mobile_vnum") {
        int v = atoi(val.c_str());
        if (v > 0) adv_template_mobile_vnum = v;
      }
    }
  }

  // Ensure other tables are loaded
  load_tiers_if_needed();
  load_alias_map_if_needed();
  load_class_gear_if_needed();
}

static void load_tiers_if_needed() {
  if (!adv_tiers.empty()) return;
  std::vector<std::string> lines;
  if (!file_read_lines("lib/etc/adventurer_difficulty.txt", lines)) {
    Tier t; t.id=2; t.name="Street"; t.weight=100; t.skill_mult=1.0f; t.skill_jitter=1;
    adv_tiers.push_back(t); adv_total_weight = t.weight; return;
  }
  for (auto &ln : lines) {
    std::vector<std::string> tok; split(ln, '|', tok);
    if (tok.size() < 3 || tok[0] != "tier") continue;
    Tier t; t.id = atoi(tok[1].c_str()); t.name = tok[2];
    for (size_t i=3;i<tok.size();++i) {
      auto eq = tok[i].find('=');
      if (eq == std::string::npos) continue;
      auto key = trim(tok[i].substr(0, eq));
      auto val = trim(tok[i].substr(eq+1));
      if (key == "weight") t.weight = atoi(val.c_str());
      else if (key == "skill_mult") t.skill_mult = atof(val.c_str());
      else if (key == "skill_jitter") t.skill_jitter = atoi(val.c_str());
      else if (key == "attr") { // back-compat additive
        std::vector<std::string> a; split(val, ',', a);
        if (a.size()>=6) { t.bod_add=atoi(a[0].c_str()); t.qui_add=atoi(a[1].c_str()); t.rea_add=atoi(a[2].c_str()); t.str_add=atoi(a[3].c_str()); t.int_add=atoi(a[4].c_str()); t.wil_add=atoi(a[5].c_str()); }
      } else if (key == "attr_mult") { // new multiplicative
        std::vector<std::string> a; split(val, ',', a);
        if (a.size()>=6) { t.attr_mult_bod=atof(a[0].c_str()); t.attr_mult_qui=atof(a[1].c_str()); t.attr_mult_rea=atof(a[2].c_str()); t.attr_mult_str=atof(a[3].c_str()); t.attr_mult_int=atof(a[4].c_str()); t.attr_mult_wil=atof(a[5].c_str()); }
      } else if (key == "weapons") split(val, ',', t.weapon_aliases);
      else if (key == "armor") split(val, ',', t.armor_aliases);
      else if (key == "ammo") split(val, ',', t.ammo_aliases);
      else if (key == "initware") split(val, ',', t.init_ware_aliases);
    }
    adv_total_weight += std::max(0, t.weight);
    adv_tiers.push_back(t);
  }
  if (adv_tiers.empty()) { Tier t; t.id=2; t.name="Street"; t.weight=100; adv_tiers.push_back(t); adv_total_weight = t.weight; }
}

static void load_alias_map_if_needed() {
  if (!adv_alias_vnums.empty()) return;
  std::vector<std::string> lines;
  if (!file_read_lines("lib/etc/adventurer_gear_map.txt", lines)) return;
  for (auto &ln : lines) {
    auto eq = ln.find('|');
    if (eq==std::string::npos) continue;
    std::string alias = trim(ln.substr(0,eq));
    std::vector<std::string> nums; split(ln.substr(eq+1), ' ', nums);
    std::vector<int> arr;
    for (auto &n : nums) { if (!n.empty()) arr.push_back(atoi(n.c_str())); }
    if (!alias.empty() && !arr.empty()) adv_alias_vnums[alias]=arr;
  }
}

static void load_class_gear_if_needed() {
  if (!adv_class_gear.empty()) return;
  std::vector<std::string> lines;
  if (!file_read_lines("lib/etc/adventurer_class_gear.txt", lines)) return;
  for (auto &ln : lines) {
    std::vector<std::string> tok; split(ln, '|', tok);
    if (tok.size() < 2 || tok[0] != "class") continue;
    std::string tag = tok[1];
    ClassGear cg;
    for (size_t i=2;i<tok.size();++i) {
      auto eq = tok[i].find('=');
      if (eq == std::string::npos) continue;
      auto key = trim(tok[i].substr(0, eq));
      auto val = trim(tok[i].substr(eq+1));
      if (key == "weapons") split(val, ',', cg.weapons);
      else if (key == "armor") split(val, ',', cg.armor);
      else if (key == "ammo") split(val, ',', cg.ammo);
      else if (key == "initware") split(val, ',', cg.initware);
    }
    adv_class_gear[tag] = cg;
  }
}

// ---------- names ----------
static void load_names_if_needed() {
  if (names_loaded) return;
  file_read_lines("lib/etc/adventurer_first.txt", adv_first);
  file_read_lines("lib/etc/adventurer_last.txt", adv_last);
  if (adv_first.empty()) adv_first.push_back("Alex");
  if (adv_last.empty()) adv_last.push_back("Johnson");
  names_loaded = true;
}
static std::string random_fullname() {
  load_names_if_needed();
  int fi = number(0, (int)adv_first.size() - 1);
  int li = number(0, (int)adv_last.size() - 1);
  return adv_first[fi] + std::string(" ") + adv_last[li];
}

// ---------- socials ----------
static void load_replies_if_needed() {
  if (replies_loaded) return;
  std::vector<std::string> lines;
  file_read_lines("lib/etc/adventurer_social_replies.txt", lines);
  auto add = [&](std::unordered_map<std::string, std::vector<Reply>>& bucket,
                 std::vector<Reply>& def,
                 const std::string& key, const Reply& r) {
    if (key == "default") def.push_back(r);
    else bucket[key].push_back(r);
  };
  for (const auto& ln : lines) {
    std::vector<std::string> t; split(ln, '|', t);
    if (t.size() < 4) continue;
    std::string social = t[0], scope = t[1], type = t[2], payload = t[3];
    Reply r;
    if (type == "say") r.kind = Reply::SAY;
    else if (type == "emote") r.kind = Reply::EMOTE;
    else r.kind = Reply::ACTION;
    if (!payload.empty() && payload[0] == '\"' && payload.back() == '\"')
      payload = payload.substr(1, payload.size()-2);
    r.payload = payload;
    if (scope == "targeted") add(adv_replies_targeted, adv_replies_default_targeted, social, r);
    else add(adv_replies_room, adv_replies_default_room, social, r);
  }
  if (adv_replies_default_targeted.empty()) {
    adv_replies_default_targeted.push_back({ Reply::SAY, "Easy there, chummer." });
    adv_replies_default_targeted.push_back({ Reply::ACTION, "grin" });
  }
  if (adv_replies_default_room.empty()) {
    adv_replies_default_room.push_back({ Reply::ACTION, "smile" });
    adv_replies_default_room.push_back({ Reply::SAY, "Heh." });
  }
  replies_loaded = true;
}

// ---------- identity & flags ----------
static void adv_assign_identity(struct char_data* mob) {
  std::string fullname = random_fullname();
  DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.name);
  mob->player.physical_text.name = str_dup(fullname.c_str());
  DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.keywords);
  std::string keys = fullname + " adventurer runner";
  mob->player.physical_text.keywords = str_dup(keys.c_str());
  DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.room_desc);
  std::string rdesc = fullname + " is here, gearing up for the next run.";
  mob->player.physical_text.room_desc = str_dup(rdesc.c_str());
  DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.look_desc);
  std::string ldesc = "A grim-faced adventurer with the look of someone who's seen a few too many back-alley clinics.";
  mob->player.physical_text.look_desc = str_dup(ldesc.c_str());
}
static void adv_configure_flags(struct char_data* mob) {
  MOB_FLAGS(mob).RemoveBit(MOB_SENTINEL);
  MOB_FLAGS(mob).SetBit(MOB_STAY_ZONE);
  MOB_FLAGS(mob).RemoveBit(MOB_WIMPY);
  #ifdef MOB_NOSTEAL
    MOB_FLAGS(mob).RemoveBit(MOB_NOSTEAL);
  #endif
}

// ---------- helpers ----------
static bool is_adventurer(struct char_data* ch) {
  return IS_NPC(ch) && IS_MOB(ch) && MOB_HAS_SPEC(ch, adventurer_spec);
}
static bool cooldown_ok(struct char_data* mob) {
  long id = GET_IDNUM(mob);
  time_t now = time(0);
  auto it = adv_next_ok.find(id);
  if (it == adv_next_ok.end() || it->second <= now) {
    adv_next_ok[id] = now + number(2, 5);
    return true;
  }
  return false;
}
static void adv_do_say(struct char_data* mob, const char* text) { char buf[MAX_INPUT_LENGTH]; snprintf(buf, sizeof(buf), "say %s", text); command_interpreter(mob, buf, GET_CHAR_NAME(mob)); }
static void adv_do_emote(struct char_data* mob, const char* text) { char buf[MAX_INPUT_LENGTH]; snprintf(buf, sizeof(buf), "emote %s", text); command_interpreter(mob, buf, GET_CHAR_NAME(mob)); }
static void adv_do_action(struct char_data* mob, const char* action, const char* target) {
  char buf[MAX_INPUT_LENGTH];
  if (target && *target) snprintf(buf, sizeof(buf), "%s %s", action, target);
  else snprintf(buf, sizeof(buf), "%s", action);
  command_interpreter(mob, buf, GET_CHAR_NAME(mob));
}

// ---------- tier scaling ----------
static void adv_apply_tier_scaling(struct char_data* mob, const Tier* tier) {
  if (!tier) return;
  // Skills first
  for (int skill = 0; skill < MAX_SKILLS; skill++) {
    extern int get_character_skill(struct char_data *ch, int skill);
    int base = GET_SKILL(mob, skill);
    if (base <= 0) continue;
    int jitter = number(-tier->skill_jitter, tier->skill_jitter);
    int val = (int) (base * tier->skill_mult + 0.5f) + jitter;
    val = clamp_val(val, 2, 12);
    set_character_skill(mob, skill, val, FALSE, FALSE);
  }
  // Attributes: multiplicative then additive, clamped
  #undef SET_ATTR
#define SET_ATTR(getter,setter,mult,add) do { int v = getter; v = (int)(v * (mult) + 0.5f) + (add); v = MIN(12, MAX(1, v)); setter = v; } while(0)
  SET_ATTR(GET_REAL_BOD(mob), GET_REAL_BOD(mob), tier->attr_mult_bod, tier->bod_add);
  SET_ATTR(GET_REAL_QUI(mob), GET_REAL_QUI(mob), tier->attr_mult_qui, tier->qui_add);
  SET_ATTR(GET_REAL_REA(mob), GET_REAL_REA(mob), tier->attr_mult_rea, tier->rea_add);
  SET_ATTR(GET_REAL_STR(mob), GET_REAL_STR(mob), tier->attr_mult_str, tier->str_add);
  SET_ATTR(GET_REAL_INT(mob), GET_REAL_INT(mob), tier->attr_mult_int, tier->int_add);
  SET_ATTR(GET_REAL_WIL(mob), GET_REAL_WIL(mob), tier->attr_mult_wil, tier->wil_add);
  #undef SET_ATTR

  if (mob->player.physical_text.keywords) {
    std::string keys = mob->player.physical_text.keywords;
    keys += " tier_" + tier->name;
    DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.keywords);
    mob->player.physical_text.keywords = str_dup(keys.c_str());
  }
}

static void adv_apply_gear_for_aliases(struct char_data* mob, const std::vector<std::string>& aliases) {
  for (auto &al : aliases) {
    auto it = adv_alias_vnums.find(al);
    if (it == adv_alias_vnums.end() || it->second.empty()) continue;
    int vnum = it->second[number(0, (int)it->second.size()-1)];
    if (vnum <= 0) continue;
    if (struct obj_data *o = read_object(vnum, VIRTUAL, OBJ_LOAD_REASON_MOB_DEFAULT_GEAR)) {
      obj_to_char(o, mob);
    }
  }
}

static void adv_apply_tier_and_class_gear(struct char_data* mob, const Tier* tier, const std::string& class_tag) {
  std::vector<std::string> w, a, am, iw;
  auto it = adv_class_gear.find(class_tag);
  if (it != adv_class_gear.end()) { w = it->second.weapons; a = it->second.armor; am = it->second.ammo; iw = it->second.initware; }
  if (w.empty()) w = tier->weapon_aliases;
  if (a.empty()) a = tier->armor_aliases;
  if (am.empty()) am = tier->ammo_aliases;
  if (iw.empty()) iw = tier->init_ware_aliases;

  if (!w.empty()) adv_apply_gear_for_aliases(mob, { w[number(0, (int)w.size()-1)] });
  if (!a.empty()) adv_apply_gear_for_aliases(mob, { a[number(0, (int)a.size()-1)] });
  if (!am.empty()) { auto chosen = am[number(0, (int)am.size()-1)]; adv_apply_gear_for_aliases(mob, { chosen, chosen }); }
  if (!iw.empty()) adv_apply_gear_for_aliases(mob, iw);

  char wearbuf[] = "wear all";
  command_interpreter(mob, wearbuf, GET_CHAR_NAME(mob));
}

// ---------- economy ----------
static void adv_give_economy(struct char_data* mob, const Tier* tier) {
  int t = tier ? tier->id : 2;
  int mult = 100 + (t - 2) * 25; // 75..175%
  int loose = number(100, 1000) * mult / 100;
  #ifdef GET_NUYEN
    GET_NUYEN_RAW(mob) += loose;
  #else
    #ifdef GET_GOLD
      GET_GOLD(mob) += loose;
    #endif
  #endif
  int cred = number(1000, 10000) * mult / 100;
  auto it = adv_alias_vnums.find("credstick");
  if (it != adv_alias_vnums.end() && !it->second.empty()) {
    int vnum = it->second[number(0, (int)it->second.size()-1)];
    if (vnum > 0) {
      if (struct obj_data *cs = read_object(vnum, VIRTUAL, OBJ_LOAD_REASON_MOB_DEFAULT_GEAR)) {
        #ifdef GET_OBJ_VAL
          GET_OBJ_VAL(cs, 0) = cred;
        #endif
        #ifdef GET_OBJ_COST
          GET_OBJ_COST(cs) = cred;
        #endif
        obj_to_char(cs, mob);
      }
    }
  }
}

// ---------- class detection & archetype apply ----------
static std::string detect_archetype_class(struct archetype_data* A) {
  if (!A) return "samurai";
  bool has_spells = (A->spells[0][0] != 0);
  bool has_powers = (A->powers[0][0] != 0);
  bool has_deck = (A->cyberdeck > 0);
  if (has_deck) return "decker";
  if (has_spells) return "mage";
  if (has_powers) return "adept";
  return "samurai";
}

static bool adv_apply_archetype_to_mob(struct char_data* mob, const Tier* tier, std::string& out_class_tag) {
  int count = NUM_CCR_ARCHETYPES;
  if (count <= 0) return false;
  int i = number(0, count - 1);
  struct archetype_data *A = archetypes[i];
  if (!A) return false;

  out_class_tag = detect_archetype_class(A);

  for (int skill = 0; skill < MAX_SKILLS; skill++)
    if (A->skills[skill])
      set_character_skill(mob, skill, A->skills[skill], FALSE, FALSE);

  for (int s = 0; s < NUM_ARCHETYPE_SPELLS; s++) {
    if (!A->spells[s][0]) break;
    struct spell_data *spell = new spell_data;
    spell->name = str_dup(spells[A->spells[s][0]].name);
    spell->type = A->spells[s][0];
    spell->subtype = A->spells[s][1];
    spell->force = A->spells[s][2];
    spell->next = GET_SPELLS(mob);
    GET_SPELLS(mob) = spell;
    GET_SPELLS_DIRTY_BIT(mob) = TRUE;
  }

  for (int p = 0; p < NUM_ARCHETYPE_ABILITIES; p++) {
    if (!A->powers[p][0]) break;
    SET_POWER_TOTAL(mob, A->powers[p][0], A->powers[p][1]);
  }

  if (A->weapon > 0) {
    struct obj_data *weapon = read_object(A->weapon, VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE);
    if (weapon) {
      #define ATTACH_IF_EXISTS(vnum) if ((vnum) > 0) { struct obj_data *acc = read_object((vnum), VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE); if (acc) obj_to_obj(acc, weapon); }
      ATTACH_IF_EXISTS(A->weapon_top);
      ATTACH_IF_EXISTS(A->weapon_barrel);
      ATTACH_IF_EXISTS(A->weapon_under);
      #undef ATTACH_IF_EXISTS
      obj_to_char(weapon, mob);
    }
  }

  for (int wearloc = 0; wearloc < NUM_WEARS; wearloc++) {
    if (A->worn[wearloc] > 0) {
      struct obj_data *o = read_object(A->worn[wearloc], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE);
      if (o) equip_char(mob, o, wearloc);
    }
  }

  for (int c = 0; c < NUM_ARCHETYPE_CARRIED; c++) {
    if (A->carried[c] > 0) {
      struct obj_data *o = read_object(A->carried[c], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE);
      if (o) obj_to_char(o, mob);
    } else break;
  }

  for (int cyb = 0; cyb < NUM_ARCHETYPE_CYBERWARE; cyb++) {
    if (!A->cyberware[cyb]) break;
    if (struct obj_data *ware = read_object(A->cyberware[cyb], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE)) obj_to_char(ware, mob);
  }
  for (int bio = 0; bio < NUM_ARCHETYPE_BIOWARE; bio++) {
    if (!A->bioware[bio]) break;
    if (struct obj_data *ware = read_object(A->bioware[bio], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE)) obj_to_char(ware, mob);
  }
  for (int f = 0; f < NUM_ARCHETYPE_FOCI; f++) {
    if (!A->foci[f][0]) break;
    if (struct obj_data *focus = read_object(A->foci[f][0], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE)) obj_to_char(focus, mob);
  }
  if (A->cyberdeck > 0) {
    if (struct obj_data *deck = read_object(A->cyberdeck, VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE)) obj_to_char(deck, mob);
  }
  for (int sw = 0; sw < NUM_ARCHETYPE_SOFTWARE; sw++) {
    if (!A->software[sw]) break;
    if (struct obj_data *soft = read_object(A->software[sw], VIRTUAL, OBJ_LOAD_REASON_ARCHETYPE)) obj_to_char(soft, mob);
  }

  // Tier scaling and gear/economy
  adv_apply_tier_scaling(mob, tier);
  adv_apply_tier_and_class_gear(mob, tier, out_class_tag);
  adv_give_economy(mob, tier);
  return true;
}

// ---------- room flags ----------
static bool has_flag_named(struct room_data* room, const std::string& flag_name) {
  if (flag_name == "PEACEFUL") return ROOM_IS_PEACEFUL(room);
  if (flag_name == "!MOB") return ROOM_FLAGGED(room, ROOM_NOMOB);
  if (flag_name == "INDOORS") return ROOM_FLAGGED(room, ROOM_INDOORS);
  if (flag_name == "ARENA") return ROOM_FLAGGED(room, ROOM_ARENA);
  if (flag_name == "STAFF-ONLY") return ROOM_FLAGGED(room, ROOM_STAFF_ONLY);
  if (flag_name == "ELEVATOR_SHAFT") return ROOM_FLAGGED(room, ROOM_ELEVATOR_SHAFT);
  if (flag_name == "STORAGE") return ROOM_FLAGGED(room, ROOM_STORAGE);
  if (flag_name == "SMALL_DRONE_ONLY") return ROOM_FLAGGED(room, ROOM_TOO_CRAMPED_FOR_CHARACTERS);
  // ASTRAL is not a room flag; ignoring.
  if (flag_name == "ASTRAL") return FALSE;
  if (flag_name == "RADIOACTIVE") return ROOM_FLAGGED(room, ROOM_RADIATION);
  return FALSE;
}



// ---------- spawn & despawn ----------
static void spawn_one_adventurer_in_room(struct room_data* room) {
  if (!room) return;
  struct char_data* mob = read_mobile(adv_template_mobile_vnum, VIRTUAL);
  if (!mob) return;

  const Tier* tier = pick_tier();
  std::string class_tag;

  adv_assign_identity(mob);
  adv_configure_flags(mob);
  adv_apply_archetype_to_mob(mob, tier, class_tag);

  if (mob->player.physical_text.room_desc && tier) {
    std::string r = mob->player.physical_text.room_desc;
    r += " (looks " + class_tag + " " + tier->name + ")";
    DELETE_ARRAY_IF_EXTANT(mob->player.physical_text.room_desc);
    mob->player.physical_text.room_desc = str_dup(r.c_str());
  }

  char_to_room(mob, room);
}

static const Tier* pick_tier() {
  load_tiers_if_needed();
  int r = number(1, std::max(1, adv_total_weight));
  int acc = 0;
  for (auto &t : adv_tiers) {
    acc += std::max(0,t.weight);
    if (r <= acc) return &t;
  }
  return &adv_tiers.back();
}

// Zone-entry hook
void adventurer_on_pc_enter_room(struct char_data* ch, struct room_data* room) {
  if (!ch || !room) return;
  if (IS_NPC(ch)) return;
  load_config_if_needed();

  int z = room->zone;
  long id = GET_IDNUM(ch);
  int last = adv_last_zone_for_pc[id];
  adv_zone_last_activity[z] = time(0);

  if (last == z) return; // only on zone change
  adv_last_zone_for_pc[id] = z;

  if (number(1, 100) > adv_zone_entry_spawn_chance_pct) return;

  // random eligible room in zone
  std::vector<struct room_data*> candidates;
  for (rnum_t rr = 0; rr <= top_of_world; rr++) {
    if (!VALID_ROOM_RNUM(rr)) continue;
    struct room_data* R = &world[rr];
    if (R->zone != z) continue;
    if (!is_room_ok_for_spawn(R)) continue;
    candidates.push_back(R);
  }
  if (candidates.empty()) return;
  struct room_data* dest = candidates[number(0, (int)candidates.size()-1)];
  spawn_one_adventurer_in_room(dest);
}

// maintenance
void adventurer_maintain() {
  load_config_if_needed();
  time_t now = time(0);

  std::unordered_set<int> zones_with_pcs;
  for (struct char_data* ch = character_list; ch; ch = ch->next_in_character_list) {
    if (IS_NPC(ch)) continue;
    struct room_data* room = get_ch_in_room(ch);
    if (!room) continue;
    int z = room->zone;
    zones_with_pcs.insert(z);
    adv_zone_last_activity[z] = now;
  }

  std::unordered_set<int> zones_with_adventurers;
  for (struct char_data* ch = character_list; ch; ch = ch->next_in_character_list) {
    if (!is_adventurer(ch)) continue;
    struct room_data* room = get_ch_in_room(ch);
    if (!room) continue;
    zones_with_adventurers.insert(room->zone);
  }

  for (int z : zones_with_adventurers) {
    if (zones_with_pcs.count(z)) continue;
    time_t last = adv_zone_last_activity.count(z) ? adv_zone_last_activity[z] : 0;
    if (last == 0) continue;
    if (now - last < adv_zone_inactivity_despawn_seconds) continue;

    for (struct char_data* ch = character_list; ch; ) {
      struct char_data* next = ch->next_in_character_list;
      if (is_adventurer(ch)) {
        struct room_data* room = get_ch_in_room(ch);
        if (room && room->zone == z) {
          extract_char(ch, FALSE);
        }
      }
      ch = next;
    }
    adv_zone_last_activity[z] = now;
  }
}

// SPECIAL
SPECIAL(adventurer_spec) {
  struct char_data* mob = (struct char_data*) me;
  if (!IS_NPC(mob)) return FALSE;
  if (cmd == 0 && get_ch_in_room(mob)) {
    if (!mob->player.physical_text.keywords || !str_str(mob->player.physical_text.keywords, "adventurer")) {
      const Tier* tier = pick_tier();
      std::string class_tag;
      adv_assign_identity(mob);
      adv_configure_flags(mob);
      adv_apply_archetype_to_mob(mob, tier, class_tag);
    }
  }
  return FALSE;
}

void adventurer_notify_social(struct char_data* actor, struct char_data* vict, int cmd) {
  if (!actor) return;
  struct room_data* room = get_ch_in_room(actor);
  if (!room || ROOM_IS_PEACEFUL(room)) return;
  load_replies_if_needed();

  const char* social = CMD_NAME;
  if (!social) social = "default";

  for (struct char_data* mob = room->people; mob; mob = mob->next_in_room) {
    if (!is_adventurer(mob)) continue;
    if (!AWAKE(mob)) continue;
    if (!cooldown_ok(mob)) continue;
    if (!CAN_SEE(mob, actor)) continue;

    bool targeted = (vict == mob);
    const std::vector<Reply>* choices = nullptr;
    if (targeted) {
      auto it = adv_replies_targeted.find(social);
      choices = (it != adv_replies_targeted.end()) ? &it->second : &adv_replies_default_targeted;
    } else {
      auto it = adv_replies_room.find(social);
      choices = (it != adv_replies_room.end()) ? &it->second : &adv_replies_default_room;
    }
    if (!choices || choices->empty()) continue;
    const Reply& r = (*choices)[number(0, (int)choices->size()-1)];

    char payload[MAX_INPUT_LENGTH];
    const char* actname = PERS(actor, mob);
    std::string pay = r.payload;
    size_t pos = 0;
    while ((pos = pay.find("%name%", pos)) != std::string::npos) {
      pay.replace(pos, 6, actname ? actname : "someone");
      pos += (actname ? strlen(actname) : 7);
    }
    strlcpy(payload, pay.c_str(), sizeof(payload));

    switch (r.kind) {
      case Reply::SAY:   adv_do_say(mob, payload); break;
      case Reply::EMOTE: adv_do_emote(mob, payload); break;
      case Reply::ACTION: adv_do_action(mob, payload, targeted ? (actname ? actname : NULL) : NULL); break;
    }
  }
}