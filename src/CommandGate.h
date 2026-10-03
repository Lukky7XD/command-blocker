#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// 명령 막기의 판정 — Kaldrin Command disabler 와 같은 원리.
//
// 내 게임이 서버일 때(로컬 월드 · 친구가 들어온 내 월드), 모든 명령 실행이 지나는 `Command::run` 의 디투어(CommandHook)가
// 명령마다 여기에 묻고, 버릴 것이면 원본을 부르지 않는다 — 출력이 빈 채로 남아 요청한 사람에게 아무 글도 안 간다.
// 게임의 명령 표에는 아무것도 쓰지 않는다. 명령은 글자가 아니라 (만든 CommandRegistry 주소, 명령 기호) 로 가린다 —
// 그래서 별칭(tp · msg · w)과 `/execute … run …` 안의 명령도 같이 걸린다.
//
// 막는 주체는 스위치가 이름으로 가리키는 다섯뿐이다(호스트 · 다른 플레이어 · 명령 블록 · 웹소켓 · NPC).
// 그 밖의 주체(세계 설정 화면 · 행동 팩 · tick 함수 · 스크립트 …)와 못 읽은 주체는 막지 않는다.
namespace cb::gate {

// 사용자가 적은 이름 → 게임이 찾는 꼴. 앞뒤 공백과 앞의 '/' 를 걷고, 첫 공백에서 자르고, ASCII 소문자로.
std::string normalizeName(std::string_view text);

// ── 막을 명령의 목록 ──────────────────────────────────────────────────────
inline constexpr std::size_t kMaxBlocked = 32;

// 무시할 주체의 비트
inline constexpr std::uint8_t kHost = 1u << 0;
inline constexpr std::uint8_t kOtherPlayers = 1u << 1;
inline constexpr std::uint8_t kCommandBlocks = 1u << 2;
inline constexpr std::uint8_t kWebsockets = 1u << 3;
inline constexpr std::uint8_t kNpcs = 1u << 4;

struct Blocklist {
    std::uint64_t registry = 0;   // 서버 CommandRegistry — 0 이면 아무것도 안 막는다
    std::uint32_t count = 0;
    std::uint8_t sources = 0;     // 무시할 주체(k* 비트)
    std::uint8_t all = 0;         // 그 주체가 낸 명령은 목록 밖이라도 무시(「모든 명령 막기」)
    std::uint32_t symbols[kMaxBlocked] = {};
};
bool contains(const Blocklist& list, std::uint64_t registry, std::uint32_t symbol);
bool sameList(const Blocklist& a, const Blocklist& b);

// 이름들을 서버 표에서 찾아 기호로 바꾼다 — 읽기만 한다.
struct Resolved {
    bool ok = false;            // 표의 모양이 맞았다
    const char* why = "";       // ok 가 false 인 이유
    Blocklist list;             // ok 면 registry 와 찾은 기호들
    std::vector<int> unknown;   // 표에 없는 이름의 번호(wanted 의 0 기준)
};
Resolved resolve(std::uint64_t registry, const std::vector<std::string>& wanted);

// 목록 판 — 쓰는 쪽(CLI · 주기 스레드)은 락, 읽는 쪽(명령을 실행하는 게임 스레드)은 락 없이 seqlock 으로 읽는다.
class BlockBoard {
public:
    void write(const Blocklist& list);
    bool read(Blocklist& out) const;   // 쓰는 중이 길어 못 읽으면 false — 그 한 번은 통과시킨다

private:
    std::atomic<std::uint32_t> seq_{0};
    Blocklist list_{};
    std::mutex writeMutex_;
};

// 이 Command 객체는 어디 것인가. 부르는 쪽이 보호 아래에서 부른다.
//   Elsewhere : 목록의 표(서버 표)가 만든 명령이 아니다 — 보지 않는다
//   Listed    : 서버 표의 명령이고 목록에 있다
//   Unlisted  : 서버 표의 명령인데 목록에 없다 — 「모든 명령 막기」가 켜진 주체만 본다
enum class CommandKind : std::uint8_t { Elsewhere, Listed, Unlisted };
CommandKind classifyCommand(const void* command, const Blocklist& list);

// ── 요청한 주체 ──────────────────────────────────────────────────────────
struct Requester {
    bool readable = false;       // 요청한 주체까지 닿았다
    std::uint32_t type = 0xFF;   // getOriginType
    bool hasPlayer = false;      // 종류가 플레이어일 때 그 플레이어를 찾았다
    std::uint8_t hosting = 0;    // 그 플레이어의 isHostingPlayer 바이트
    bool npc = false;            // NPC 가 낸 명령 — 주체가 아니라 부른 자리로 디투어가 채운다
};
// origin 의 getOutputReceiver 를 따라 요청한 주체까지 간다(Virtual 은 넷까지). 게임의 가상 함수를 부르므로 보호 아래에서 부른다.
Requester findRequester(void* origin);

enum class Verdict : std::uint8_t {
    Host,          // 호스트 플레이어
    OtherPlayer,   // 다른 플레이어(떠났으면 여기)
    CommandBlock,  // 명령 블록 · 명령 블록 수레
    Websocket,     // 웹소켓
    Npc,           // NPC 의 버튼 · 대화창
    Other,         // 그 밖의 주체 — 막지 않는다
    Unreadable,    // 못 읽었다 — 막지 않는다
};
inline constexpr int kVerdictCount = 7;
Verdict judge(const Requester& who);

// 이 분류의 주체가 낸 이 명령을 무시하는가. 목록의 명령은 제 sources 비트가 켜졌을 때,
// 목록 밖 명령은 sources 와 all 비트가 둘 다 켜졌을 때. Other · Unreadable 은 늘 통과.
bool ignores(Verdict v, std::uint8_t sources, std::uint8_t all, bool listed);

} // namespace cb::gate
