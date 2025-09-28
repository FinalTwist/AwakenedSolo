// -----------------------------------------------------------------------------
// sysart_composer.cpp — drop-in composer for “system art” text
// -----------------------------------------------------------------------------
// This version expands variety significantly and doubles the requested pools.
// See the pools section for the expanded lists.
// -----------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cctype>

extern "C" {
  struct room_data;                     // forward
  extern struct room_data* world;       // provided by the MUD
  int real_room(long vnum);             // vnum -> rnum
}

// C++ linkage (matches utils.cpp)
int number(int from, int to);           // MUD RNG

#ifndef MAX_STRING_LENGTH
#define MAX_STRING_LENGTH 8192
#endif

namespace {
  template <typename T>
  inline const T& pick(const std::vector<T>& v) {
    return v[number(0, (int)v.size()-1)];
  }
}

// -----------------------
// Content pools
// -----------------------
namespace pools {
  static const std::vector<const char*> styles = {
    "spray-painted", "stenciled", "neon-inked", "hand-lettered",
    "glitched", "embossed", "laser-etched", "chalked",
    "thermochromic", "holofoil", "dripped", "pixelated"
  };

  static const std::vector<const char*> mediums = {
    "poster", "tag", "billboard", "holo-banner", "folded pamphlet",
    "sticker", "paste-up", "tile mosaic", "fiber flag", "LED scrawl"
  };

  static const std::vector<const char*> moods = {
    "wistful", "defiant", "anxious", "ecstatic", "melancholic",
    "playful", "grim", "urgent", "sarcastic", "hopeful"
  };

  // CATEGORY: Love / Affection (doubled LINES 10 -> 20)
  static const std::vector<const char*> love_titles = {
    "LOVE IS A PATCH", "I SAVED A SLOT FOR YOU", "YOU+ME=UPTIME",
    "HEARTS RUN HOT", "PERMA-LINK", "KISS.CACHE", "SYNC WITH ME",
    "YOUR SMILE, MY SKY", "LATENCY OF LONGING", "ECHOES OF US"
  };
  static const std::vector<const char*> love_lines = {
    "I wrote your name on the firewall so it would never fall.",
    "Meet me where the streetlights hum like old modems.",
    "Even in packet loss, I find you.",
    "If love is illegal here, then cuff me in your arms.",
    "I keep your laugh in my pocket like contraband sunshine.",
    "Two hearts overclocked, still under-volted for fear.",
    "Your touch: the soft reboot I never knew I needed.",
    "I mapped a path to you and called it home.",
    "Your name tastes like the first rain on chrome.",
    "Hold my hand; let the sirens harmonize with us.",
    // new 10
    "We share a password no system can crack.",
    "I pinned your heartbeat to my taskbar.",
    "Between alarms, I dream your name in lowercase.",
    "Your voice is the patch note I wait for.",
    "Under neon rain, we decompile the night.",
    "If sparks are crimes, book us for arson.",
    "Your eyes: two green LEDs saying 'ready'.",
    "I would reroute traffic just to walk beside you.",
    "Save me as default, and never reset.",
    "We scale to infinity when we hold on."
  };

  // CATEGORY: Hate / Snark / Graffiti beef (doubled TITLES 7 -> 14)
  static const std::vector<const char*> hate_titles = {
    "DONT TRUST THEIR SMILES", "THIS WALL LIES", "NO KINGS NO QUEENS",
    "DELETE YOUR HEROES", "ZERO GODS ZERO BOSSES",
    "TRUTH SPRAYED HERE", "YOUR BRAND IS A BARGAINING CHIP",
    // new 7
    "THE PRICE OF QUIET IS GUILT",
    "WEAPONIZED KINDNESS ZONE",
    "OPTICS OVER ETHICS",
    "SMILE FOR SURVEILLANCE",
    "BREAK GLASS CEILINGS, NOT PEOPLE",
    "LOYALTY CARDS, LOYALTY CHAINS",
    "THE ALGORITHM OWNS YOUR SHOELACES"
  };
  static const std::vector<const char*> hate_lines = {
    "All cops are quotas.",
    "They sell you the cage and call it premium.",
    "Your feed is a leash—nice collar, though.",
    "In case of emergency, break influencer.",
    "Authority is the oldest malware.",
    "We were fine until they optimized us.",
    "If compliance were cool, they wouldn't need drones.",
    "Love your neighbors, encrypt your enemies.",
    "The city eats kids and burps ads.",
    "No gods, just guidelines—and fines."
  };

  // CATEGORY: Corporate Ads / Propaganda
  static const std::vector<const char*> corp_brands = {
    "AetherSoft", "OmniDyne", "NeoNest", "Pulse&Co", "GloboSun",
    "Singularity Bank", "Pleiades Bio", "Trident Logistics",
    "HappySoy", "Skythread"
  };
  static const std::vector<const char*> corp_slogans = {
    "Upgrade your tomorrow—today.",
    "We monetize your potential so you don’t have to.",
    "Trust our algorithm. It already trusts you.",
    "Because safety should be scalable.",
    "The future tastes like this.",
    "Your data deserves a corner office.",
    "Dream faster.",
    "Small fees. Giant feelings.",
    "Care, but profitable.",
    "Bold. Bright. Boundless."
  };

  // CATEGORY: Protest / Activism (doubled TITLES 6 -> 12)
  static const std::vector<const char*> protest_titles = {
    "WE ARE NOT YOUR METRICS", "PAY THE GHOSTS", "HANDS OFF THE GRID",
    "REVOKE CONSENT", "BREATHE BEFORE PROFIT", "FREE THE FEED",
    // new 6
    "EVICT THE EVICTORS", "COUNT US, NOT CREDITS", "REPAIR > REPLACE",
    "NO CONTRACT WITHOUT A VOICE", "STREETS BELONG TO FEET",
    "MAKE CARE PUBLIC"
  };
  static const std::vector<const char*> protest_lines = {
    "They meter air; we measure courage.",
    "If you can’t own your body, own the street.",
    "Opt-out is a myth carved into contracts.",
    "We built the city; they trademarked the view.",
    "Unplug their teeth from our necks.",
    "Union is a spell—cast it loudly."
  };

  // CATEGORY: Philosophical / Cryptic (doubled TITLES 4 -> 8, LINES 6 -> 12)
  static const std::vector<const char*> phil_titles = {
    "THE MAP ATE THE CITY", "ECHOES BEFORE THOUGHT",
    "THE SKY IS A COMMENT FIELD", "CLOCKS THAT BLEED",
    // new 4
    "GRAVITY OF MEMORY", "THE RIVER DREAMS IN LOOPS",
    "STATIC PRAYERS", "GHOSTS OF THE USER"
  };
  static const std::vector<const char*> phil_lines = {
    "I saw a future where we prayed to receipts.",
    "Meaning is a side effect; choose your dosage.",
    "The self is a rumor told by mirrors.",
    "All roads are loops if your heart is round.",
    "We debug reality by loving badly.",
    "Yesterday’s truth is tonight’s costume.",
    // new 6
    "Every promise is a bridge with a toll.",
    "Time is the only creditor we never pay back.",
    "Facts are tools; stories are houses.",
    "Silence is a dialect of trust.",
    "The city is a library of unfinished sentences.",
    "What we call fate is cached momentum."
  };

  // CATEGORY: Surreal / Glitch
  static const std::vector<const char*> glitch_titles = {
    "// SEGMENTATION EMOTION", "404: COMFORT NOT FOUND",
    "NULL POINTER TO HEAVEN", "STACK OVERFLOW OF KISSES"
  };
  static const std::vector<const char*> glitch_lines = {
    "Someone salted the moon; the soup tasted like tides.",
    "The vending machine dispensed a small, polite thunderstorm.",
    "I put a quarter in the sky and got three sunsets.",
    "Bugs? Those are just features with wings.",
    "I left my name in RAM and it kept me warm.",
    "Hold F to pay attention."
  };

  // CATEGORY: Neighborhood / Chatter / Warnings
  static const std::vector<const char*> neighborhood_titles = {
    "BLOCK CHAT", "LOST & FOUND", "NEIGHBOR NOTE", "STREET NEWS"
  };
  static const std::vector<const char*> neighborhood_lines = {
    "Watch for the drone that pretends to be a star.",
    "Free seedlings behind the noodle stall—be kind.",
    "Hey Blue Hoodie, you dropped your synth-key.",
    "If the alley hums, take the long way home.",
    "Booth 3 serves broth and advice; tip for both.",
    "If you see the cat with LEDs, tell Jun it’s okay."
  };

  // TEMPLATES (doubled: look 4 -> 8, room 4 -> 8)
  static const std::vector<const char*> look_templates = {
    "A %s %s carries a %s message: \"%s\"",
    "This %s %s crackles with a %s vibe: \"%s\"",
    "You see a %s %s, its tone unmistakably %s: \"%s\"",
    "A %s %s, tagged in a %s hand: \"%s\"",
    // new 4
    "The %s %s whispers in a %s cadence: \"%s\"",
    "A %s %s radiates a %s aura: \"%s\"",
    "Someone left a %s %s pulsing with %s intent: \"%s\"",
    "A %s %s scrawled in a %s flourish: \"%s\""
  };

  static const std::vector<const char*> room_templates = {
    "%s %s here—\"%s\"",
    "Someone left a %s %s reading: \"%s\"",
    "There’s a %s %s on the wall: \"%s\"",
    "A %s %s flutters in the air: \"%s\"",
    // new 4
    "You notice a %s %s nearby: \"%s\"",
    "A fresh %s %s catches the eye: \"%s\"",
    "Faded but stubborn, a %s %s says: \"%s\"",
    "Half-torn, a %s %s still declares: \"%s\""
  };
}

// -----------------------
// Theme selection by room vnum (simple spread)
// -----------------------
static const char* pick_theme_for_room(long room_vnum) {
  if      (room_vnum % 7 == 0) return "corp";
  else if (room_vnum % 7 == 1) return "love";
  else if (room_vnum % 7 == 2) return "hate";
  else if (room_vnum % 7 == 3) return "protest";
  else if (room_vnum % 7 == 4) return "phil";
  else if (room_vnum % 7 == 5) return "glitch";
  else                          return "neighborhood";
}

// -----------------------
// Composer internals
// -----------------------
static void compose_from_theme(const char* theme,
                               std::string& out_title,
                               std::string& out_line) {
  using namespace pools;
  std::string t(theme ? theme : "");

  if (t == "love") {
    out_title = pick(love_titles);
    out_line  = pick(love_lines);
  } else if (t == "hate") {
    out_title = pick(hate_titles);
    out_line  = pick(hate_lines);
  } else if (t == "corp") {
    out_title = pick(corp_brands);
    out_title += ": ";
    out_title += pick(corp_slogans);
    out_line   = pick(corp_slogans);
  } else if (t == "protest") {
    out_title = pick(protest_titles);
    out_line  = pick(protest_lines);
  } else if (t == "phil") {
    out_title = pick(phil_titles);
    out_line  = pick(phil_lines);
  } else if (t == "glitch") {
    out_title = pick(glitch_titles);
    out_line  = pick(glitch_lines);
  } else { // neighborhood
    out_title = pick(neighborhood_titles);
    out_line  = pick(neighborhood_lines);
  }
}

static void build_strings(long room_vnum,
                          char* name_buf, size_t name_sz,
                          char* look_buf, size_t look_sz,
                          char* room_buf, size_t room_sz) {
  using namespace pools;

  const char* theme = pick_theme_for_room(room_vnum);

  // Core text: title + main line
  std::string title, line;
  compose_from_theme(theme, title, line);

  // Style modifiers
  const char* style  = pick(styles);
  const char* medium = pick(mediums);
  const char* mood   = pick(moods);

  // NAME/KEYWORDS — keep short and searchable
  std::snprintf(name_buf, name_sz, "%s %s", style, medium);

  // LOOK DESC
  {
    const char* tmpl = pick(look_templates);
    std::snprintf(look_buf, look_sz, tmpl, style, medium, mood, line.c_str());
  }

  // ROOM DESC
  {
    const char* tmpl = pick(room_templates);
    std::snprintf(room_buf, room_sz, tmpl, style, medium, title.c_str());
  }
}

// Public API (size-aware)
extern "C" void sysart_compose_ex(long room_vnum,
                                   char* name_buf, size_t name_sz,
                                   char* look_buf, size_t look_sz,
                                   char* room_buf, size_t room_sz) {
  if (!name_buf || !look_buf || !room_buf) return;
  if (name_sz == 0)  name_sz = MAX_STRING_LENGTH;
  if (look_sz == 0)  look_sz = MAX_STRING_LENGTH;
  if (room_sz == 0)  room_sz = MAX_STRING_LENGTH;
  name_buf[0] = look_buf[0] = room_buf[0] = '\0';
  build_strings(room_vnum, name_buf, name_sz, look_buf, look_sz, room_buf, room_sz);
}

// Legacy API (assumes caller provides sufficiently large buffers)
extern "C" void sysart_compose(long room_vnum,
                                char* out_name,
                                char* out_look,
                                char* out_roomdesc) {
  sysart_compose_ex(room_vnum,
                    out_name,    MAX_STRING_LENGTH,
                    out_look,    MAX_STRING_LENGTH,
                    out_roomdesc,MAX_STRING_LENGTH);
}

// ---------------------------------------------------------------------------
// Adapter for utils.cpp: sysart_compose(theme, std::string&, std::string&, std::string&)
// This wraps your existing room-vnum composer (sysart_compose_ex) so callers
// that only have a theme string can still get name/look/roomdesc strings.
// ---------------------------------------------------------------------------

#include <unordered_map>

// If this forward isn't visible here in your file already, keep it.
// (It's declared earlier in this file in most setups; having it again is harmless.)
extern "C" void sysart_compose_ex(long room_vnum,
                                  char* name_buf, size_t name_sz,
                                  char* look_buf, size_t look_sz,
                                  char* room_buf, size_t room_sz);

// Map a theme key to a stable pseudo-room seed.
// This keeps style/medium/mood selection varied but deterministic per theme.
static long __sysart_theme_seed(const std::string& theme) {
  static const std::unordered_map<std::string, long> kSeeds = {
    {"corp", 100000}, {"love", 100001}, {"hate", 100002},
    {"protest", 100003}, {"phil", 100004}, {"glitch", 100005},
    {"neighborhood", 100006}, {"market", 100007}, {"park", 100008},
    {"industrial", 100009}, {"waterfront", 100010}, {"slum", 100011},
    {"residential", 100012}, {"arcology", 100013}, {"sewer", 100014},
    {"subway", 100015}, {"bridge", 100016}, {"city", 100017}
  };
  auto it = kSeeds.find(theme);
  return it == kSeeds.end() ? 100017 : it->second; // default to "city"
}

// Public C++ overload used by src/utils.cpp
void sysart_compose(const std::string& theme,
                    std::string& out_name,
                    std::string& out_look,
                    std::string& out_roomdesc)
{
  char name_buf[MAX_STRING_LENGTH];
  char look_buf[MAX_STRING_LENGTH];
  char room_buf[MAX_STRING_LENGTH];
  name_buf[0] = look_buf[0] = room_buf[0] = '\0';

  sysart_compose_ex(__sysart_theme_seed(theme),
                    name_buf, sizeof(name_buf),
                    look_buf, sizeof(look_buf),
                    room_buf, sizeof(room_buf));

  out_name.assign(name_buf);
  out_look.assign(look_buf);
  out_roomdesc.assign(room_buf);
}
