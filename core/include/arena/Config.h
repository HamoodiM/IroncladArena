#pragma once
// A tiny INI-like format for tuning data:
//
//   # comment
//   [enemy grunt]
//   radius = 14
//   speed  = 150
//
// A comment starts with '#' at the beginning of a line or '# ' anywhere, so
// values like `color = #E0604A` survive. A section header is `[type]` or
// `[type name]`. Values are kept as strings;
// typed reads happen in GameData, which knows which keys are numbers.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arena {

struct ConfigSection {
    std::string type;
    std::string name;
    int line = 0;  // line of the header, for error messages
    std::vector<std::pair<std::string, std::string>> values;

    const std::string* Find(std::string_view key) const;
};

struct ConfigFile {
    std::vector<ConfigSection> sections;
};

// Returns false and fills `error` ("line 12: ...") on the first syntax error.
bool ParseConfig(std::string_view text, ConfigFile& out, std::string& error);

// Strict number parse: the whole string must be a number.
bool ParseFloat(std::string_view s, float& out);
bool ParseInt(std::string_view s, int& out);

}  // namespace arena
