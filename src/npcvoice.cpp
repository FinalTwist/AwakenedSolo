
#include "npcvoice.hpp"
#include "interpreter.hpp"
#include "db.hpp"
#include "utils.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <functional>

using std::string;

// External say command from your command table.
extern int cmd_say;
ACMD_DECLARE(do_say);

namespace NPCVoice {

// -----------------------
// Personality resolution
// -----------------------

static std::unordered_map<const void*, int> g_forced_pers;

void set_personality(struct char_data* mob, Personality p) {
  g_forced_pers[mob] = (int)p;
}

// --- addressed greeting detection (case-insensitive, word-boundary) ---
static bool ci_isalpha(char c){ return std::isalpha((unsigned char)c); }
static char ci_lower(char c){ return (char)std::tolower((unsigned char)c); }

static bool word_match_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    if ((i == 0 || !ci_isalpha(hay[i-1])) &&
        std::equal(needle.begin(), needle.end(), hay.begin()+i,
                   [](char a, char b){ return ci_lower(a) == ci_lower(b); }) &&
        (i + needle.size() == hay.size() || !ci_isalpha(hay[i + needle.size()])))
      return true;
  }
  return false;
}

// True if text contains a greeting token (word-boundary, case-insensitive).
bool contains_greeting(const char* said) {
  if (!said || !*said) return false;
  std::string s(said);
  static const char* TOK[] = {"hello","hi","hey","yo","greetings","salutations","heya"};
  for (auto t : TOK) if (word_match_ci(s, t)) return true;
  return false;
}

static bool contains_any_greet_token(const std::string& s) {
  // keep this list tight to avoid false positives
  static const char* TOK[] = {"hello","hi","hey","yo","greetings","salutations","heya"};
  for (auto t : TOK) if (word_match_ci(s, t)) return true;
  return false;
}

bool is_addressed_greeting(const char* said, struct char_data* mob) {
  if (!said || !*said || !mob) return false;
  std::string s(said), nm = GET_NAME(mob) ? GET_NAME(mob) : "";
  if (nm.empty()) return false;
  return contains_any_greet_token(s) && word_match_ci(s, nm);
}

static int name_alpha_len(const char* s) {
  if (!s) return 0;
  int c = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  for (; *p; ++p) if (std::isalpha(*p)) ++c;
  return c;
}

Personality get_personality(struct char_data* mob) {
  auto it = g_forced_pers.find(mob);
  if (it != g_forced_pers.end())
    return static_cast<Personality>(it->second);

  const char* nm = GET_NAME(mob);
  int letters = name_alpha_len(nm);
  if (letters <= 0) return PERS_NEUTRAL;
  switch (letters % 3) {
    case 0: return PERS_GANG;
    case 1: return PERS_NEUTRAL;
    default: return PERS_POLITE;
  }
}

// -----------------------
// Odds, cooldowns, random
// -----------------------

static inline bool chance(int pct) { return number(1,100) <= pct; }
static inline time_t nowts() { return time(0); }

// cooldowns keyed by "ptr:tag"
static std::unordered_map<string, long> g_cd;

static string cd_key(const void* who, const char* tag) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%p", who);
  string k(buf);
  k.push_back(':');
  if (tag) k += tag;
  return k;
}

static bool cooldown_ok(const void* who, const char* tag, int seconds) {
  string k = cd_key(who, tag);
  auto it = g_cd.find(k);
  if (it == g_cd.end()) return true;
  return nowts() >= it->second;
}

static void arm_cooldown(const void* who, const char* tag, int seconds) {
  g_cd[cd_key(who, tag)] = nowts() + seconds;
}

// -----------------------
// Content loading
// -----------------------

static std::unordered_map<string, std::vector<string>> g_bank;

static void load_one(const char* pers, const char* ev) {
  char path[256];
  snprintf(path, sizeof(path), "lib/etc/npcvoice/categories/%s/%s.txt", pers, ev);
  std::vector<string> lines;
  if (FILE* fp = fopen(path, "r")) {
    char buf[2048];
    while (fgets(buf, sizeof(buf), fp)) {
      string s(buf);
      while (!s.empty() && (s.back()=='\n' || s.back()=='\r')) s.pop_back();
      if (!s.empty() && s[0] != '#') lines.push_back(s);
    }
    fclose(fp);
  }
  g_bank[string(pers) + "/" + ev] = std::move(lines);
}

void init() {
  static const char* P[] = {"gang","neutral","polite"};
  static const char* E[] = {
    "greet","farewell","listen",
    "combat_start","combat_hit","combat_death",
    "shop_buy","shop_sell","shop_deny","shop_poor",
    "quest_accept","quest_progress","quest_complete",
    "receive_item"
  };
  for (auto p : P) for (auto e : E) load_one(p, e);
}

// -----------------------
// Helpers
// -----------------------

static inline const char* pers_name(int p) {
  switch (p) {
    case PERS_GANG: return "gang";
    case PERS_POLITE: return "polite";
    default: return "neutral";
  }
}

static const std::vector<string>& lines_for(const char* pers, const char* ev) {
  static const std::vector<string> kEmpty;
  auto it = g_bank.find(string(pers) + "/" + ev);
  return (it != g_bank.end()) ? it->second : kEmpty;
}

// non-repeat per (mob,event)
static std::unordered_map<long, size_t> g_last_index;

static const string& pick_nonrepeating(const std::vector<string>& v,
                                       struct char_data* mob,
                                       const char* ev) {
  long key = GET_IDNUM(mob) ^ (long)(std::hash<string>{}(ev) & 0x7fffffff);
  size_t idx = v.empty() ? 0 : (size_t)number(0, (int)v.size()-1);
  auto it = g_last_index.find(key);
  if (it != g_last_index.end() && v.size() > 1 && idx == it->second) {
    idx = (idx + 1) % v.size();
  }
  g_last_index[key] = idx;
  return v.empty() ? *(new string("")) : v[idx];
}

// speak via say or say-to
static void speak_to_room_or_char(struct char_data* mob, struct char_data* ch, const char* msg) {
  if (!mob || !msg || !*msg) return;
  char buf[MAX_STRING_LENGTH];
  snprintf(buf, sizeof(buf), "%s", msg);
  do_say(mob, buf, cmd_say, 0);
}

// -----------------------
// Events
// -----------------------

void maybe_greet(struct char_data *ch, struct char_data *mob) {
  if (!ch || !mob) return;
  if (!chance(30)) return;
  if (!cooldown_ok(mob, "greet", 120)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "greet");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "greet");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "greet", 120);
}

void maybe_farewell(struct char_data *ch, struct char_data *mob) {
  if (!ch || !mob) return;
  if (!chance(25)) return;
  if (!cooldown_ok(mob, "farewell", 120)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "farewell");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "farewell");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "farewell", 120);
}

void maybe_listen(struct char_data *ch, struct char_data *mob, const char* said) {
  if (!ch || !mob) return;
  if (!chance(20)) return;
  if (!cooldown_ok(mob, "listen", 30)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "listen");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "listen");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "listen", 30);
}

void combat_start(struct char_data *ch, struct char_data *mob) {
  if (!ch || !mob) return;
  if (!chance(10)) { return; } // lowered per your preference
  if (!cooldown_ok(mob, "combat_start", 120)) { return; }
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "combat_start");
  if (!vec.empty()) {
    const string& s = pick_nonrepeating(vec, mob, "combat_start");
    if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  }
  arm_cooldown(mob, "combat_start", 120);
}

void combat_hit(struct char_data *ch, struct char_data *mob) {
  if (!ch || !mob) return;
  if (!chance(3)) return; // lowered
  if (!cooldown_ok(mob, "combat_hit", 120)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "combat_hit");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "combat_hit");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "combat_hit", 120);
}

void combat_death(struct char_data *ch, struct char_data *mob) {
  if (!ch || !mob) return;
  if (!chance(20)) return; // lowered
  if (!cooldown_ok(mob, "combat_death", 90)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "combat_death");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "combat_death");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "combat_death", 90);
}

void shop_event(struct char_data *ch, struct char_data *mob, int type) {
  if (!ch || !mob) return;
  if (!chance(40)) return;
  if (!cooldown_ok(mob, "shop", 30)) return;
  const char* ev =
    (type==1) ? "shop_buy" :
    (type==2) ? "shop_sell" :
    (type==3) ? "shop_deny" : "shop_poor";
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, ev);
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, ev);
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "shop", 30);
}

void quest_event(struct char_data *ch, struct char_data *mob, int type) {
  if (!ch || !mob) return;
  if (!chance(60)) return;
  if (!cooldown_ok(mob, "quest", 20)) return;
  const char* ev =
    (type==1) ? "quest_accept" :
    (type==2) ? "quest_progress" : "quest_complete";
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, ev);
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, ev);
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "quest", 20);
}

void receive_item(struct char_data *ch, struct char_data *mob, struct obj_data *obj) {
  if (!ch || !mob) return;
  if (!chance(50)) return;
  if (!cooldown_ok(mob, "receive_item", 60)) return;
  const char* pers = pers_name(get_personality(mob));
  auto& vec = lines_for(pers, "receive_item");
  if (vec.empty()) return;
  const string& s = pick_nonrepeating(vec, mob, "receive_item");
  if (!s.empty()) speak_to_room_or_char(mob, ch, s.c_str());
  arm_cooldown(mob, "receive_item", 60);
}

void addressed_greet(struct char_data* ch, struct char_data* mob) {
  if (!ch || !mob) return;

  const char* pers = pers_name(get_personality(mob));
  const auto& vec = lines_for(pers, "greet");

  if (!vec.empty()) {
    const std::string& s = pick_nonrepeating(vec, mob, "greet_forced");
    if (!s.empty()) {
      speak_to_room_or_char(mob, ch, s.c_str());
      return;
    }
  }
  // Fallback if file empty
  speak_to_room_or_char(mob, ch, "Hello.");
}

} // namespace NPCVoice
