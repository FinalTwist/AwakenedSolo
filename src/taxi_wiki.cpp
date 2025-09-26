#include "taxi_wiki.hpp"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>


namespace {
std::vector<TaxiWiki::Entry> g_entries;

static inline std::string strtolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  return s;
}

// Allow-list for "SeattleEnvirons"
static bool allowed_seattle_environs(const std::string& tag_in) {
  std::string t; t.reserve(tag_in.size());
  for (char c : tag_in) t.push_back(std::tolower((unsigned char)c));
  // Accepted region tags for Seattle & environs:
  static const char* OK[] = {
    "seattle", "downtown", "auburn", "council island", "council",
    "everett", "renton", "tacoma", "touristville", "puyallup"
  };
  for (const char* s : OK) if (t == s) return true;
  return false;
}

static void trim(std::string& s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
  size_t b = s.size();
  while (b>a && std::isspace((unsigned char)s[b-1])) --b;
  s = s.substr(a, b-a);
}
static bool contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return false;
  auto H = strtolower(hay);
  auto N = strtolower(needle);
  return H.find(N) != std::string::npos;
}
}

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
      auto rest = s.substr(std::strlen("gridguide add"));
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
      e.region = "";
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

std::vector<Entry> find(const char* user_c, const char* regionFilter_c, size_t max_hits) {
  std::vector<Entry> out;
  std::string user = user_c ? user_c : "";
  std::string regionFilter = regionFilter_c ? regionFilter_c : "";
  if (user.empty()) return out;
  for (const auto& e : g_entries) {
    if (!regionFilter.empty()) {
      if (regionFilter == "SeattleEnvirons") {
        // Only allow entries explicitly tagged as one of our Seattle-area regions.
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
