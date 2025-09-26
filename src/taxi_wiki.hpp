#pragma once
#include <string>
#include <vector>

namespace TaxiWiki {

struct Entry {
  std::string keyword;   // canonical keyword
  std::string display;   // pretty name (may match keyword)
  long x{0};
  long y{0};
  std::string region;    // optional
};

int load(const char* path);

// Case-insensitive substring match against keyword/display.
// regionFilter: "" for all.
std::vector<Entry> find(const char* user, const char* regionFilter, size_t max_hits);

} // namespace TaxiWiki
