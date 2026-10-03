#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Command disabler 의 상태 — 설정을 들고, 주기마다(그리고 바뀔 때마다) 서버 표에서 목록을 다시 맞춰 Command::run 디투어에 올린다.
namespace cb::blocker {

inline constexpr int kMaxCommands = 24;
inline constexpr std::size_t kMaxLength = 64;

// 기본값은 Kaldrin Command disabler 와 같다. 하위 스위치(allowHost · all*)는 부모 스위치가 켜졌을 때만 뜻이 있다.
struct Config {
    bool enabled = true;
    std::vector<std::string> commands;   // 막을 명령 이름(normalizeName 을 거친 꼴)
    bool players = true;                 // 플레이어 막기 — 호스트 포함
    bool allowHost = false;              //   └ 호스트 허용
    bool allPlayers = false;             //   └ 모든 명령 막기
    bool commandBlocks = true;           // 명령 블록 막기 — 수레 포함
    bool allCommandBlocks = false;
    bool npcs = true;                    // NPC 막기 — 버튼 · 대화창의 명령
    bool allNpcs = false;
    bool websockets = true;              // 웹소켓 막기 — /connect 로 붙은 서버
    bool allWebsockets = false;
};

// 스위치 → 목록 판의 비트(gate::k*)
struct Bits {
    std::uint8_t sources = 0;
    std::uint8_t all = 0;
};
Bits toBits(const Config& c);

struct Status {
    bool hookReady = false;     // Command::run 훅이 걸렸다
    bool rootReady = false;     // MinecraftGame* 를 잡았다
    bool hosting = false;       // 서버 표를 찾았다 — 내가 호스트인 월드
    bool noServer = false;      // 서버 사슬이 비었다 — 월드 밖이거나 남의 월드 · 서버
    const char* why = "";       // hosting 이 아닌 이유
    int blocked = 0;            // 서버 표에서 찾은 명령 기호 수
    std::vector<int> unknown;   // 서버 표에 없는 commands 의 번호(0 기준)
};

bool start(std::string& error);   // 게임 빌드 확인 · 훅 · 주기 스레드
void stop();                      // 막기를 풀고 훅을 뗀다

Config config();
void edit(const std::function<void(Config&)>& change);   // 바꾸고 곧바로 다시 맞춘다
Status status();

} // namespace cb::blocker
