#include "Cli.h"

#include "Blocker.h"
#include "CommandGate.h"
#include "CommandHook.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>
#include <string_view>
#include <vector>

namespace cb::cli {
namespace {

using blocker::Config;

HANDLE g_in = INVALID_HANDLE_VALUE;
HANDLE g_out = INVALID_HANDLE_VALUE;
bool g_allocated = false;

// 스위치 — status 에 이 차례로 보인다(웹소켓이 맨 아래). parent 가 꺼져 있으면 하위 스위치는 뜻이 없다.
struct SwitchDef {
    const char* name;
    bool Config::*field;
    bool Config::*parent;
    const char* label;
};
constexpr SwitchDef kSwitches[] = {
    {"players", &Config::players, nullptr, "플레이어 막기 (호스트 포함)"},
    {"allowhost", &Config::allowHost, &Config::players, "호스트 허용 — 내가 친 명령은 실행"},
    {"allplayers", &Config::allPlayers, &Config::players, "모든 명령 막기"},
    {"commandblocks", &Config::commandBlocks, nullptr, "명령 블록 막기 (명령 블록 수레 포함)"},
    {"allcommandblocks", &Config::allCommandBlocks, &Config::commandBlocks, "모든 명령 막기"},
    {"npcs", &Config::npcs, nullptr, "NPC 막기 (버튼 · 대화창의 명령)"},
    {"allnpcs", &Config::allNpcs, &Config::npcs, "모든 명령 막기"},
    {"websockets", &Config::websockets, nullptr, "웹소켓 막기 (/connect 로 붙은 서버)"},
    {"allwebsockets", &Config::allWebsockets, &Config::websockets, "모든 명령 막기"},
};

// 콘솔 창의 닫기는 게임 프로세스를 통째로 끝낸다 — Ctrl+C · Ctrl+Break 만이라도 삼킨다.
BOOL WINAPI onCtrl(DWORD type) { return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT; }

void print(std::string_view text) {
    if (text.empty()) return;
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), n);
    DWORD written = 0;
    WriteConsoleW(g_out, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
}

void println(std::string_view text = {}) {
    print(text);
    print("\n");
}

// false = 콘솔을 더 읽을 수 없다. Ctrl+C 가 읽기를 끊으면 빈 줄로 돌려준다.
bool readLine(std::string& out) {
    std::wstring line;
    wchar_t buf[256];
    while (line.empty() || line.back() != L'\n') {
        DWORD n = 0;
        if (!ReadConsoleW(g_in, buf, ARRAYSIZE(buf), &n, nullptr)) {
            if (GetLastError() != ERROR_OPERATION_ABORTED) return false;
            n = 0;
        }
        if (n == 0) break;
        line.append(buf, n);
    }
    while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    out.assign(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), out.data(), bytes, nullptr, nullptr);
    return true;
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string word; in >> word;) out.push_back(word);
    return out;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* onOff(bool v) { return v ? "on" : "off"; }

void printHelp() {
    println("명령어");
    println("  status                  상태 · 스위치 · 막을 명령 목록");
    println("  on | off                차단 켜기 / 끄기");
    println("  add <명령>              막을 명령 추가 (앞의 / 는 없어도 됨, 별칭 tp · msg · w 도 됨, 최대 24개)");
    println("  del <번호|명령>         막을 명령 삭제");
    println("  clear                   막을 명령 모두 삭제");
    println("  set <스위치> <on|off>   누가 낸 명령을 막을지 — 스위치 이름은 status 에 나옴");
    println("  unload                  차단을 풀고 DLL 을 내림");
    println("내가 호스트인 월드에서만 막습니다. help 처럼 각자의 게임에서 도는 명령은 막을 수 없습니다.");
}

std::string worldText(const Config& c, const blocker::Status& s) {
    if (!s.hookReady) return "명령 실행 자리를 걸지 못해 막을 수 없습니다";
    if (!s.rootReady) return "게임 루트를 기다리는 중 (게임 화면이 그려지면 잡힙니다)";
    if (s.hosting) {
        return c.enabled ? std::format("내가 호스트인 월드 — 목록의 명령 {}개를 막는 중", s.blocked)
                         : "내가 호스트인 월드 — 차단이 꺼져 있음";
    }
    if (s.noServer) return "호스트 중인 월드 없음 (월드 밖이거나 남의 월드 · 서버)";
    return std::format("서버의 명령 표를 읽지 못했습니다 ({})", s.why);
}

void printStatus() {
    const Config c = blocker::config();
    const blocker::Status s = blocker::status();
    println(std::format("차단: {}", c.enabled ? "켜짐" : "꺼짐"));
    println("월드: " + worldText(c, s));
    println();
    println("스위치 (set <이름> on|off)");
    for (const SwitchDef& d : kSwitches) {
        const bool live = d.parent == nullptr || c.*(d.parent);
        println(std::format("  {:<18} {:<4} {}{}{}", d.parent ? std::string("  ") + d.name : std::string(d.name),
                            onOff(c.*(d.field)), d.parent ? "└ " : "", d.label, live ? "" : "  (상위 스위치가 꺼져 있어 무효)"));
    }
    println();
    println(std::format("막을 명령 ({}/{})", c.commands.size(), blocker::kMaxCommands));
    if (c.commands.empty()) println("  (없음 — add <명령> 으로 추가)");
    for (std::size_t i = 0; i < c.commands.size(); ++i) {
        const bool unknown = s.hosting && std::find(s.unknown.begin(), s.unknown.end(), static_cast<int>(i)) != s.unknown.end();
        println(std::format("  {:>2}. {}{}", i + 1, c.commands[i], unknown ? "   ← 서버 명령 표에 없는 이름" : ""));
    }
    println();
    using gate::Verdict;
    println(std::format("무시한 명령: 호스트 {} · 다른 플레이어 {} · 명령 블록 {} · 웹소켓 {} · NPC {}",
                        cmdrun::ignoredCount(Verdict::Host), cmdrun::ignoredCount(Verdict::OtherPlayer),
                        cmdrun::ignoredCount(Verdict::CommandBlock), cmdrun::ignoredCount(Verdict::Websocket),
                        cmdrun::ignoredCount(Verdict::Npc)));
}

void addCommand(const std::vector<std::string>& args) {
    const std::string name = args.size() >= 2 ? gate::normalizeName(args[1]) : std::string();
    if (name.empty()) return println("사용법: add <명령>   예) add give");
    if (name.size() > blocker::kMaxLength) return println("명령 이름이 너무 깁니다");
    const char* problem = nullptr;
    int index = -1;
    blocker::edit([&](Config& c) {
        if (std::find(c.commands.begin(), c.commands.end(), name) != c.commands.end()) {
            problem = "이미 목록에 있습니다";
        } else if (c.commands.size() >= blocker::kMaxCommands) {
            problem = "목록이 가득 찼습니다 (최대 24개)";
        } else {
            index = static_cast<int>(c.commands.size());
            c.commands.push_back(name);
        }
    });
    if (problem) return println(std::format("{}: {}", problem, name));
    println("추가: " + name);
    const blocker::Status s = blocker::status();
    if (s.hosting && std::find(s.unknown.begin(), s.unknown.end(), index) != s.unknown.end()) {
        println("  ※ 이 월드의 서버 명령 표에 없는 이름입니다 — 철자를 확인하세요");
    }
}

void deleteCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) return println("사용법: del <번호|명령>");
    const std::string& arg = args[1];
    const bool isNumber = arg.size() <= 3 &&
                          std::all_of(arg.begin(), arg.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    const std::string name = gate::normalizeName(arg);
    std::string removed;
    blocker::edit([&](Config& c) {
        auto it = c.commands.end();
        if (isNumber) {
            const std::size_t n = std::stoul(arg);
            if (n >= 1 && n <= c.commands.size()) it = c.commands.begin() + static_cast<std::ptrdiff_t>(n - 1);
        } else {
            it = std::find(c.commands.begin(), c.commands.end(), name);
        }
        if (it == c.commands.end()) return;
        removed = *it;
        c.commands.erase(it);
    });
    println(removed.empty() ? "목록에 없습니다: " + arg : "삭제: " + removed);
}

void setSwitch(const std::vector<std::string>& args) {
    if (args.size() < 3) return println("사용법: set <스위치> <on|off>   — 스위치 이름은 status 에 나옴");
    const std::string name = lower(args[1]);
    const std::string value = lower(args[2]);
    const auto def = std::find_if(std::begin(kSwitches), std::end(kSwitches),
                                  [&](const SwitchDef& d) { return name == d.name; });
    if (def == std::end(kSwitches)) return println("모르는 스위치입니다: " + args[1]);
    if (value != "on" && value != "off") return println("값은 on 또는 off 입니다");
    bool parentOff = false;
    blocker::edit([&](Config& c) {
        c.*(def->field) = value == "on";
        parentOff = def->parent != nullptr && !(c.*(def->parent));
    });
    println(std::format("{} = {}{}", def->name, value, parentOff ? "  (상위 스위치가 꺼져 있어 지금은 무효)" : ""));
}

} // namespace

bool open() {
    g_allocated = AllocConsole() != FALSE;
    g_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    g_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (g_out == INVALID_HANDLE_VALUE || g_in == INVALID_HANDLE_VALUE) {
        close();
        return false;
    }
    SetConsoleTitleW(L"Command Blocker - Minecraft");
    // 닫기 버튼을 없앤다(conhost 창일 때만 먹는다 — Windows Terminal 탭은 못 막는다)
    if (HWND window = GetConsoleWindow()) {
        if (HMENU menu = GetSystemMenu(window, FALSE)) DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    SetConsoleCtrlHandler(&onCtrl, TRUE);
    return true;
}

void close() {
    SetConsoleCtrlHandler(&onCtrl, FALSE);
    if (g_in != INVALID_HANDLE_VALUE) CloseHandle(g_in);
    if (g_out != INVALID_HANDLE_VALUE) CloseHandle(g_out);
    g_in = g_out = INVALID_HANDLE_VALUE;
    if (g_allocated) FreeConsole();
    g_allocated = false;
}

void run(bool started, const std::string& startError) {
    println("Command Blocker — Minecraft Bedrock 1.26.5203.0");
    println("내가 호스트인 월드(로컬 · 친구가 들어온 월드)에서 고른 명령을 조용히 무시합니다.");
    println("※ 이 창을 닫으면 게임도 함께 꺼집니다 — 내릴 때는 unload 를 입력하세요.");
    if (started) {
        println("준비됨 — help 를 입력하면 명령어가 나옵니다.");
    } else {
        println("시작하지 못했습니다: " + startError);
        println("막는 기능 없이 떠 있습니다 — unload 로 내리세요.");
    }
    for (;;) {
        print("\n> ");
        std::string line;
        if (!readLine(line)) return;
        const std::vector<std::string> args = split(line);
        if (args.empty()) continue;
        const std::string cmd = lower(args[0]);
        if (cmd == "unload" || cmd == "exit") return;
        if (cmd == "help" || cmd == "?") {
            printHelp();
        } else if (!started) {
            println("시작하지 못해 쓸 수 없습니다 — unload 로 내리세요");
        } else if (cmd == "status") {
            printStatus();
        } else if (cmd == "on" || cmd == "off") {
            blocker::edit([&](Config& c) { c.enabled = cmd == "on"; });
            println(cmd == "on" ? "차단 켜짐" : "차단 꺼짐 — 모든 명령이 그대로 실행됩니다");
        } else if (cmd == "add") {
            addCommand(args);
        } else if (cmd == "del") {
            deleteCommand(args);
        } else if (cmd == "clear") {
            blocker::edit([](Config& c) { c.commands.clear(); });
            println("막을 명령을 모두 지웠습니다");
        } else if (cmd == "set") {
            setSwitch(args);
        } else {
            println("모르는 명령어입니다: " + args[0] + " — help 를 입력하세요");
        }
    }
}

} // namespace cb::cli
