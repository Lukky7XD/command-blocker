#include "Settings.h"

#include "CommandGate.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace cb::settings {
namespace {

std::string_view trim(std::string_view s) {
    const auto isSpace = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

void parseBool(std::string_view value, bool& out) {
    const std::string v = lower(value);
    if (v == "on" || v == "true" || v == "1") out = true;
    if (v == "off" || v == "false" || v == "0") out = false;
}

// 쉼표로 나눈 이름들 — 콘솔의 add 와 같은 규칙(정리 · 길이 · 중복 · 개수)
std::vector<std::string> parseCommands(std::string_view value) {
    std::vector<std::string> out;
    while (out.size() < static_cast<std::size_t>(blocker::kMaxCommands)) {
        const std::size_t comma = value.find(',');
        const std::string name = gate::normalizeName(value.substr(0, comma));
        if (!name.empty() && name.size() <= blocker::kMaxLength && std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return out;
}

} // namespace

std::string toIni(const blocker::Config& c) {
    std::string out = "; Command Blocker 설정 — 게임 콘솔에서 바꾸면 여기에 저장됩니다\r\n[CommandBlocker]\r\n";
    out += std::string("enabled=") + (c.enabled ? "on" : "off") + "\r\n";
    out += "commands=";
    for (std::size_t i = 0; i < c.commands.size(); ++i) out += (i ? "," : "") + c.commands[i];
    out += "\r\n";
    for (const blocker::SwitchDef& s : blocker::kSwitches) out += std::string(s.name) + "=" + (c.*(s.field) ? "on" : "off") + "\r\n";
    return out;
}

void fromIni(std::string_view text, blocker::Config& c) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // 메모장이 붙이는 BOM
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const std::size_t eq = line.find('=');
        if (line.empty() || line.front() == ';' || line.front() == '#' || eq == std::string_view::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string_view value = trim(line.substr(eq + 1));
        if (key == "commands") {
            c.commands = parseCommands(value);
        } else if (key == "enabled") {
            parseBool(value, c.enabled);
        } else {
            for (const blocker::SwitchDef& s : blocker::kSwitches) {
                if (key == s.name) parseBool(value, c.*(s.field));
            }
        }
    }
}

bool load(const std::wstring& path, blocker::Config& c) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    fromIni(text, c);
    return true;
}

bool save(const std::wstring& path, const blocker::Config& c) {
    if (path.empty()) return false;
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    out << toIni(c);
    return static_cast<bool>(out.flush());
}

} // namespace cb::settings
