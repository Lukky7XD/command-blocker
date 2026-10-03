#pragma once

#include <cstdint>

// Minecraft Bedrock 1.26.5203.0 (Microsoft.MinecraftUWP · Minecraft.Windows.exe x64) 전용 주소와 오프셋.
// 출처: kaldrin-client `sdk/offsets/1.26.5203.json` 의 functions · vtables · paths(server.*) · commands 절.
// 다른 빌드에서는 전부 틀린 값이다 — 이미지 신원(크기 + PE TimeDateStamp)이 맞을 때만 쓴다(`game::verifiedBase`).
namespace cb::off {

// 이미지 신원 — 크기만으로는 1.26.5101 과 구별되지 않는다
inline constexpr std::uint32_t kImageSize = 314576896;
inline constexpr std::uint32_t kTimeDateStamp = 0x6AB54E37;

// ── 함수 (RVA) ─────────────────────────────────────────────────────────────
// ?Command::run(Command* this, CommandOrigin const&, CommandOutput&) — 모든 명령 실행이 여기를 지난다. 되돌리는 값 없음.
inline constexpr std::uint32_t kCommandRun = 0x04A30A30;
// NPC 동작(버튼 · 대화창)이 Command::run 을 부르는 call 의 반환 주소 — 부른 자리로 NPC 를 가른다
inline constexpr std::uint32_t kCommandRunNpcCaller = 0x02340BEE;
// MinecraftGame::tickInput — 매 프레임 불린다, this = MinecraftGame*
inline constexpr std::uint32_t kTickInput = 0x007E70F0;

// ── vtable (RVA) — 서버 사슬 도착지의 신원 ─────────────────────────────────
inline constexpr std::uint32_t kVtServerInstance = 0x0E83B158;
inline constexpr std::uint32_t kVtMinecraft = 0x0E7CA5B0;
inline constexpr std::uint32_t kVtMinecraftCommands = 0x0E8EE870;
inline constexpr std::uint32_t kVtServerNetworkHandler = 0x0E7BE9F0;

// ── 서버 사슬: MinecraftGame → ServerInstance → Minecraft → {MinecraftCommands, GameSession → ServerNetworkHandler} ──
inline constexpr std::uint32_t kGameServerInstance = 0x15E0;   // MinecraftGame+  : ServerInstance* (내 게임이 서버일 때만)
inline constexpr std::uint32_t kInstanceMinecraft = 0xB8;      // ServerInstance+ : Minecraft*
inline constexpr std::uint32_t kMinecraftCommands = 0xC0;      // Minecraft+      : MinecraftCommands*
inline constexpr std::uint32_t kMinecraftSession = 0xC8;       // Minecraft+      : GameSession*
inline constexpr std::uint32_t kSessionHandler = 0x48;         // GameSession+    : ServerNetworkHandler*
// ServerNetworkHandler+ : MinecraftCommands& — 같아야 서버 것이다(클라이언트에도 같은 vtable 의 자동 완성용 인스턴스가 있다)
inline constexpr std::uint32_t kHandlerCommands = 0x288;
inline constexpr std::uint32_t kCommandsRegistry = 0x10;       // MinecraftCommands+ : CommandRegistry*

// ── CommandRegistry — MSVC std::map 둘 ─────────────────────────────────────
inline constexpr std::uint32_t kRegistrySignatures = 0x1B0;    // std::map<std::string, Signature> (머리 포인터 · +kMapSize 개수)
inline constexpr std::uint32_t kRegistryAliases = 0x1D0;       // std::map<std::string, std::string> — 값이 본래 이름
inline constexpr std::uint32_t kMapSize = 0x8;
inline constexpr std::uint32_t kNodeLeft = 0x0;                // MSVC _Tree_node
inline constexpr std::uint32_t kNodeParent = 0x8;
inline constexpr std::uint32_t kNodeRight = 0x10;
inline constexpr std::uint32_t kNodeIsNil = 0x19;              // 머리 노드면 1
inline constexpr std::uint32_t kNodeKey = 0x20;                // std::string
inline constexpr std::uint32_t kNodeValue = 0x40;              // Signature · 별칭 map 에서는 std::string
inline constexpr std::uint32_t kSignatureSymbol = 0x74;        // Signature+ : 명령 기호(u32)
inline constexpr std::uint64_t kMaxSignatures = 0x1000;        // 모양 검사 — 개수 상한

// ── Command — CommandRegistry::createCommand 가 채운다 ─────────────────────
inline constexpr std::uint32_t kCommandRegistry = 0x10;        // Command+ : 만든 CommandRegistry*
inline constexpr std::uint32_t kCommandSymbol = 0x18;          // Command+ : 명령 기호(u32)

// ── CommandOrigin — vtable 안의 바이트 자리(칸 × 8) ────────────────────────
inline constexpr std::uint32_t kOriginReceiver = 0xB8;         // 칸 23 getOutputReceiver — 요청한 주체
inline constexpr std::uint32_t kOriginType = 0xC8;             // 칸 25 getOriginType (u8)
inline constexpr std::uint32_t kOriginEntity = 0x40;           // 칸 8  getEntity
// CommandOriginType
inline constexpr std::uint32_t kTypePlayer = 0;
inline constexpr std::uint32_t kTypeCommandBlock = 1;
inline constexpr std::uint32_t kTypeCommandBlockMinecart = 2;
inline constexpr std::uint32_t kTypeAutomationPlayer = 5;      // 웹소켓 (플레이어가 붙은 것)
inline constexpr std::uint32_t kTypeClientAutomation = 6;      // 웹소켓 (안 붙은 것)
inline constexpr std::uint32_t kTypeVirtual = 9;               // /execute 사슬 — 요청한 주체의 복사본을 든다

inline constexpr std::uint32_t kPlayerHosting = 0xCAA;         // Player+ : isHostingPlayer (bool)

} // namespace cb::off
