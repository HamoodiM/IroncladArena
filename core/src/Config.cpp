#include "arena/Config.h"

#include <charconv>
#include <string>

namespace arena {
namespace {

std::string_view Trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

bool IsIdentifier(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// A comment is '#' at the start of a line, or '#' followed by whitespace or
// the end of the line. That keeps colour values like #E0604A intact.
std::string_view StripComment(std::string_view line) {
    const std::string_view trimmed = Trim(line);
    if (!trimmed.empty() && trimmed.front() == '#') return {};
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '#') continue;
        const bool atEnd = i + 1 == line.size();
        if (atEnd || line[i + 1] == ' ' || line[i + 1] == '\t' || line[i + 1] == '\r') return line.substr(0, i);
    }
    return line;
}

}  // namespace

const std::string* ConfigSection::Find(std::string_view key) const {
    for (const auto& [k, v] : values)
        if (k == key) return &v;
    return nullptr;
}

bool ParseConfig(std::string_view text, ConfigFile& out, std::string& error) {
    out.sections.clear();
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = text.find('\n', pos);
        std::string_view line = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = (end == std::string_view::npos) ? text.size() + 1 : end + 1;
        ++lineNo;

        line = StripComment(line);
        line = Trim(line);
        if (line.empty()) continue;

        const auto fail = [&](const std::string& msg) {
            error = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        if (line.front() == '[') {
            if (line.back() != ']') return fail("section header is missing ']'");
            const std::string_view inner = Trim(line.substr(1, line.size() - 2));
            const auto space = inner.find_first_of(" \t");
            ConfigSection section;
            section.line = lineNo;
            section.type = std::string(Trim(inner.substr(0, space)));
            if (space != std::string_view::npos) section.name = std::string(Trim(inner.substr(space)));
            if (!IsIdentifier(section.type)) return fail("bad section type '" + section.type + "'");
            if (!section.name.empty() && !IsIdentifier(section.name)) return fail("bad section name '" + section.name + "'");
            out.sections.push_back(std::move(section));
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string_view::npos) return fail("expected 'key = value'");
        if (out.sections.empty()) return fail("value outside of any [section]");
        const std::string_view key = Trim(line.substr(0, eq));
        const std::string_view value = Trim(line.substr(eq + 1));
        if (!IsIdentifier(key)) return fail("bad key '" + std::string(key) + "'");
        if (value.empty()) return fail("key '" + std::string(key) + "' has no value");
        auto& section = out.sections.back();
        if (section.Find(key)) return fail("duplicate key '" + std::string(key) + "'");
        section.values.emplace_back(std::string(key), std::string(value));
    }
    return true;
}

bool ParseFloat(std::string_view s, float& out) {
    s = Trim(s);
    if (s.empty()) return false;
    if (s.front() == '+') s.remove_prefix(1);
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool ParseInt(std::string_view s, int& out) {
    s = Trim(s);
    if (s.empty()) return false;
    if (s.front() == '+') s.remove_prefix(1);
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace arena
