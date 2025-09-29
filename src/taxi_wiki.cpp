#include "taxi_wiki.hpp"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "awake.hpp"   // for LOG_* and TRUE/FALSE
#include "utils.hpp"   // for mudlog(...)

namespace {

std::vector<TaxiWiki::Entry> g_entries;

static inline std::string strtolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  return s;
}

static void trim(std::string& s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
  size_t b = s.size();
  while (b>a && std::isspace((unsigned char)s[b-1])) --b;
  s = s.substr(a, b-a);
}

// Normalize: lowercase and remove non-alphanumeric chars.
static std::string alnum_lower(std::string s) {
  std::string out; out.reserve(s.size());
  for (unsigned char c : s) {
    if (std::isalnum(c)) out.push_back(std::tolower(c));
  }
  return out;
}

// Fuzzy contains: case-insensitive, tolerant of punctuation and simple plural/possessive variants.
static bool contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return false;

  // Normalize both sides (strip punctuation, make lowercase).
  const std::string H = alnum_lower(hay);
  std::string N = alnum_lower(needle);
  if (N.empty()) return false;

  auto direct_hit = (H.find(N) != std::string::npos);
  if (direct_hit) return true;

  // Try to singularize simple plural/possessive forms: 's, s, es
  auto try_forms = [&](std::string n) -> bool {
    if (n.empty()) return false;
    if (H.find(n) != std::string::npos) return true;
    // 's -> ''
    if (n.size() > 2 && n.substr(n.size()-2) == "s") {
      // case: "'s" already removed by alnum_lower, so just 's -> s
      // handled by the next branch
      ;
    }
    // trailing 's'
    if (n.size() > 1 && n.back() == 's') {
      n.pop_back();
      if (!n.empty() && H.find(n) != std::string::npos) return true;
    }
    // trailing "es"
    if (n.size() > 2 && n.substr(n.size()-2) == "es") {
      n.erase(n.size()-2);
      if (!n.empty() && H.find(n) != std::string::npos) return true;
    }
    return false;
  };

  if (try_forms(N)) return true;

  // Token-wise check (lets "collection agency" match on "collection" or "agency")
  // Split the *original* needle on non-alnum and check tokens >= 3 chars.
  {
    std::string tok;
    for (char c : needle) {
      if (std::isalnum((unsigned char)c)) tok.push_back(std::tolower((unsigned char)c));
      else {
        if (tok.size() >= 3 && (H.find(tok) != std::string::npos || try_forms(tok))) return true;
        tok.clear();
      }
    }
    if (tok.size() >= 3 && (H.find(tok) != std::string::npos || try_forms(tok))) return true;
  }

  return false;
}

// Allow-list for "SeattleEnvirons"
static bool allowed_seattle_environs(const std::string& tag_in) {
  std::string t; t.reserve(tag_in.size());
  for (char c : tag_in) t.push_back(std::tolower((unsigned char)c));
  static const char* OK[] = {
    "seattle", "downtown", "auburn", "council island", "council",
    "everett", "renton", "tacoma", "touristville", "puyallup"
  };
  for (const char* s : OK) if (t == s) return true;
  return false;
}

} // anon

namespace TaxiWiki {

int load(const char* path) {
  g_entries.clear();
  FILE* f = std::fopen(path, "r");
  if (!f) return 0;
  char line[1024];
  int n=0;
  while (std::fgets(line, sizeof(line), f)) {
    std::string s(line);
    trim(s);
    if (s.empty() || s[0] == '#') continue;

    if (s.rfind("gridguide add", 0) == 0) {
      // parse: gridguide add <Keyword> <X>, <Y>
      std::string rest = s.substr(std::strlen("gridguide add"));
      trim(rest);
      size_t sp = rest.find(' ');
      if (sp == std::string::npos) continue;
      std::string key = rest.substr(0, sp);
      std::string coords = rest.substr(sp+1);
      for (auto& c : coords) if (c == ',') c = ' ';
      long gx=0, gy=0;
      if (std::sscanf(coords.c_str(), "%ld %ld", &gx, &gy) != 2) continue;
      Entry e;
      e.keyword = key;
      e.display = key;
      e.x = gx;
      e.y = gy;
      e.region = ""; // untagged
      g_entries.push_back(e);
      ++n;
    } else {
      // TSV: region\tkeyword\tdisplay\tx\ty
      std::string region, key, disp, sx, sy;
      size_t p1 = s.find('\t');
      if (p1 == std::string::npos) continue;
      region = s.substr(0, p1);
      size_t p2 = s.find('\t', p1+1);
      if (p2 == std::string::npos) continue;
      key = s.substr(p1+1, p2-(p1+1));
      size_t p3 = s.find('\t', p2+1);
      if (p3 == std::string::npos) continue;
      disp = s.substr(p2+1, p3-(p2+1));
      size_t p4 = s.find('\t', p3+1);
      if (p4 == std::string::npos) continue;
      sx = s.substr(p3+1, p4-(p3+1));
      sy = s.substr(p4+1);
      trim(region); trim(key); trim(disp); trim(sx); trim(sy);
      long gx = std::strtol(sx.c_str(), nullptr, 10);
      long gy = std::strtol(sy.c_str(), nullptr, 10);
      Entry e;
      e.keyword = key;
      e.display = disp.empty() ? key : disp;
      e.x = gx;
      e.y = gy;
      e.region = region;
      g_entries.push_back(e);
      ++n;
    }
  }
  std::fclose(f);
  return n;
}

// Case-insensitive substring match against keyword/display.
// regionFilter: "" for all; "SeattleEnvirons" to allow-list specific regions.
std::vector<Entry> find(const char* user_c, const char* regionFilter_c, size_t max_hits) {
  std::vector<Entry> out;
  if (!user_c) return out;
  std::string user(user_c);
  std::string regionFilter = regionFilter_c ? std::string(regionFilter_c) : std::string();

  for (const auto& e : g_entries) {
    if (!regionFilter.empty()) {
      if (regionFilter == "SeattleEnvirons") {
        if (e.region.empty() || !allowed_seattle_environs(e.region))
          continue;
      } else {
        if (!e.region.empty() && strtolower(e.region) != strtolower(regionFilter))
          continue;
      }
    }
    if (contains_ci(e.keyword, user) || contains_ci(e.display, user)) {
      out.push_back(e);
      if (out.size() >= max_hits) break;
    }
  }
  return out;
}

} // namespace TaxiWiki
