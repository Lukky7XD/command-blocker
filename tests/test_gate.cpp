// 게임 없이 도는 시험 — 판정 로직과, 서버 명령 표 걷기가 MSVC std::map 의 실제 배치와 맞는지.
// 명령 표는 진짜 std::map 을 게임과 같은 자리(+0x1B0 · +0x1D0)에 둔다 — Release 빌드에서만 배치가 게임과 같다.
#include "Blocker.h"
#include "CommandGate.h"
#include "Offsets.h"
#include "Settings.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);     \
            ++g_failed;                                                     \
        }                                                                   \
    } while (0)

using namespace cb;
using gate::Verdict;

// 게임의 Signature — 기호만 제자리에
struct FakeSignature {
    unsigned char pad[off::kSignatureSymbol] = {};
    std::uint32_t symbol = 0;
};

using SignatureMap = std::map<std::string, FakeSignature>;
struct FakeRegistry {
    unsigned char pad0[off::kRegistrySignatures] = {};
    SignatureMap signatures;
    unsigned char pad1[off::kRegistryAliases - off::kRegistrySignatures - sizeof(SignatureMap)] = {};
    std::map<std::string, std::string> aliases;
};

void testNormalize() {
    CHECK(gate::normalizeName(" /Give @s diamond") == "give");
    CHECK(gate::normalizeName("//TP") == "tp");
    CHECK(gate::normalizeName("   ").empty());
}

void testResolve() {
    if constexpr (sizeof(SignatureMap) != 16) {
        std::printf("FAIL build cb_tests in Release - Debug std::map layout differs from the game\n");
        ++g_failed;
        return;
    }
    auto reg = std::make_unique<FakeRegistry>();
    const auto base = reinterpret_cast<std::uintptr_t>(reg.get());
    CHECK(reinterpret_cast<std::uintptr_t>(&reg->signatures) - base == off::kRegistrySignatures);
    CHECK(reinterpret_cast<std::uintptr_t>(&reg->aliases) - base == off::kRegistryAliases);

    const char* names[] = {"give", "teleport", "tell", "kill", "time", "a_command_name_longer_than_sso"};
    for (std::uint32_t i = 0; i < 6; ++i) reg->signatures[names[i]].symbol = 100 + i;
    reg->aliases["tp"] = "teleport";
    reg->aliases["msg"] = "tell";
    reg->aliases["w"] = "tell";

    const auto registry = static_cast<std::uint64_t>(base);
    const gate::Resolved r =
        gate::resolve(registry, {"/Give", "tp", "teleport", "nope", "", "msg", "A_Command_Name_Longer_Than_SSO"});
    CHECK(r.ok);
    CHECK(r.list.registry == registry);
    CHECK(r.list.count == 4);                            // tp 와 teleport 는 한 기호
    CHECK(gate::contains(r.list, registry, 100));        // give
    CHECK(gate::contains(r.list, registry, 101));        // teleport (별칭 tp)
    CHECK(gate::contains(r.list, registry, 102));        // tell (별칭 msg)
    CHECK(gate::contains(r.list, registry, 105));        // 힙에 든 긴 이름
    CHECK(!gate::contains(r.list, registry, 103));       // kill — 안 적었다
    CHECK(!gate::contains(r.list, registry + 8, 100));   // 다른 표의 같은 기호
    CHECK(r.unknown.size() == 1 && r.unknown[0] == 3);   // nope (빈 줄은 세지 않는다)

    CHECK(!gate::resolve(0, {"give"}).ok);
    auto empty = std::make_unique<FakeRegistry>();
    CHECK(!gate::resolve(reinterpret_cast<std::uint64_t>(empty.get()), {"give"}).ok);   // 빈 표는 모양이 아니다
}

void testClassify() {
    alignas(8) unsigned char command[0x20] = {};
    const std::uint64_t registry = 0x1234;
    const std::uint32_t symbol = 7;
    std::memcpy(command + off::kCommandRegistry, &registry, sizeof registry);
    std::memcpy(command + off::kCommandSymbol, &symbol, sizeof symbol);
    gate::Blocklist list;
    list.registry = registry;
    list.count = 1;
    list.symbols[0] = 7;
    CHECK(gate::classifyCommand(command, list) == gate::CommandKind::Listed);
    list.symbols[0] = 8;
    CHECK(gate::classifyCommand(command, list) == gate::CommandKind::Unlisted);
    list.registry = 0x9999;
    CHECK(gate::classifyCommand(command, list) == gate::CommandKind::Elsewhere);
}

// CommandOrigin 흉내 — 게임과 같은 vtable 칸에 함수를 둔다
struct FakeOrigin {
    void* const* vtable;
    std::uint8_t type;
    FakeOrigin* receiver;   // nullptr = 자기 자신
    void* entity;
};
void* fakeReceiver(void* self) {
    auto* o = static_cast<FakeOrigin*>(self);
    return o->receiver ? o->receiver : o;
}
std::uint8_t fakeType(void* self) { return static_cast<FakeOrigin*>(self)->type; }
void* fakeEntity(void* self) { return static_cast<FakeOrigin*>(self)->entity; }

void* const* fakeVtable() {
    static void* vt[32] = {};
    vt[off::kOriginReceiver / 8] = reinterpret_cast<void*>(&fakeReceiver);
    vt[off::kOriginType / 8] = reinterpret_cast<void*>(&fakeType);
    vt[off::kOriginEntity / 8] = reinterpret_cast<void*>(&fakeEntity);
    return vt;
}

Verdict judgeOrigin(FakeOrigin& o) { return gate::judge(gate::findRequester(&o)); }

void testRequester() {
    std::vector<std::uint8_t> host(off::kPlayerHosting + 1, 0);
    std::vector<std::uint8_t> member(off::kPlayerHosting + 1, 0);
    host[off::kPlayerHosting] = 1;
    void* const* vt = fakeVtable();
    const auto type = [](std::uint32_t t) { return static_cast<std::uint8_t>(t); };

    FakeOrigin hostOrigin{vt, type(off::kTypePlayer), nullptr, host.data()};
    FakeOrigin memberOrigin{vt, type(off::kTypePlayer), nullptr, member.data()};
    FakeOrigin leftPlayer{vt, type(off::kTypePlayer), nullptr, nullptr};
    FakeOrigin commandBlock{vt, type(off::kTypeCommandBlock), nullptr, nullptr};
    FakeOrigin minecart{vt, type(off::kTypeCommandBlockMinecart), nullptr, nullptr};
    FakeOrigin websocket{vt, type(off::kTypeAutomationPlayer), nullptr, host.data()};   // 호스트가 붙은 웹소켓도 웹소켓
    FakeOrigin server{vt, 7, nullptr, nullptr};
    FakeOrigin executeAsMember{vt, type(off::kTypeVirtual), &memberOrigin, nullptr};      // /execute — 요청한 주체를 따른다
    FakeOrigin executeTwice{vt, type(off::kTypeVirtual), &executeAsMember, nullptr};

    CHECK(judgeOrigin(hostOrigin) == Verdict::Host);
    CHECK(judgeOrigin(memberOrigin) == Verdict::OtherPlayer);
    CHECK(judgeOrigin(leftPlayer) == Verdict::OtherPlayer);
    CHECK(judgeOrigin(commandBlock) == Verdict::CommandBlock);
    CHECK(judgeOrigin(minecart) == Verdict::CommandBlock);
    CHECK(judgeOrigin(websocket) == Verdict::Websocket);
    CHECK(judgeOrigin(server) == Verdict::Other);
    CHECK(judgeOrigin(executeAsMember) == Verdict::OtherPlayer);
    CHECK(judgeOrigin(executeTwice) == Verdict::OtherPlayer);

    gate::Requester npc = gate::findRequester(&server);
    npc.npc = true;
    CHECK(gate::judge(npc) == Verdict::Npc);
    CHECK(gate::judge(gate::Requester{}) == Verdict::Unreadable);
}

void testIgnores() {
    const std::uint8_t every = gate::kHost | gate::kOtherPlayers | gate::kCommandBlocks | gate::kWebsockets | gate::kNpcs;
    CHECK(gate::ignores(Verdict::Host, every, 0, true));
    CHECK(!gate::ignores(Verdict::Host, every & ~gate::kHost, 0, true));          // 호스트 허용
    CHECK(!gate::ignores(Verdict::OtherPlayer, every, 0, false));                 // 목록 밖
    CHECK(gate::ignores(Verdict::OtherPlayer, every, gate::kOtherPlayers, false)); // 모든 명령 막기
    CHECK(!gate::ignores(Verdict::OtherPlayer, gate::kHost, gate::kOtherPlayers, true));
    CHECK(gate::ignores(Verdict::Npc, gate::kNpcs, 0, true));
    CHECK(!gate::ignores(Verdict::Other, 0xFF, 0xFF, true));
    CHECK(!gate::ignores(Verdict::Unreadable, 0xFF, 0xFF, true));
}

void testBits() {
    blocker::Config c;
    blocker::Bits b = blocker::toBits(c);
    CHECK(b.sources == (gate::kHost | gate::kOtherPlayers | gate::kCommandBlocks | gate::kWebsockets | gate::kNpcs));
    CHECK(b.all == 0);
    c.allowHost = true;
    c.allPlayers = true;
    b = blocker::toBits(c);
    CHECK((b.sources & gate::kHost) == 0 && (b.sources & gate::kOtherPlayers) != 0);
    CHECK(b.all == gate::kOtherPlayers);
    c.allowHost = false;
    CHECK(blocker::toBits(c).all == (gate::kOtherPlayers | gate::kHost));
    c.players = false;   // 하위 스위치는 부모가 꺼지면 뜻이 없다
    c.commandBlocks = false;
    c.allCommandBlocks = true;
    b = blocker::toBits(c);
    CHECK((b.sources & (gate::kHost | gate::kOtherPlayers | gate::kCommandBlocks)) == 0);
    CHECK(b.all == 0);
}

void testBoard() {
    gate::BlockBoard board;
    gate::Blocklist in;
    in.registry = 5;
    in.count = 2;
    in.sources = 3;
    in.all = 1;
    in.symbols[0] = 9;
    in.symbols[1] = 10;
    board.write(in);
    gate::Blocklist out;
    CHECK(board.read(out) && gate::sameList(in, out));
}

void testSettings() {
    blocker::Config c;
    c.enabled = false;
    c.commands = {"give", "tp"};
    c.allowHost = true;
    c.npcs = false;
    c.allWebsockets = true;
    blocker::Config back;
    settings::fromIni(settings::toIni(c), back);
    CHECK(!back.enabled);
    CHECK(back.commands == c.commands);
    for (const blocker::SwitchDef& s : blocker::kSwitches) CHECK(back.*(s.field) == c.*(s.field));

    // 손으로 고친 파일 — BOM · 대소문자 · 공백 · 중복 · 빈 칸 · 모르는 칸 · 잘못된 값
    blocker::Config hand;
    settings::fromIni("\xEF\xBB\xBF; note\r\n[CommandBlocker]\r\nEnabled = OFF\r\ncommands = /Give, TP ,give,, kill\r\n"
                      "players=maybe\r\nunknown=1\r\nnpcs=0\n",
                      hand);
    CHECK(!hand.enabled);
    CHECK((hand.commands == std::vector<std::string>{"give", "tp", "kill"}));
    CHECK(hand.players);   // 잘못된 값은 그대로(기본 on)
    CHECK(!hand.npcs);

    std::string many = "commands=";
    for (int i = 0; i < 30; ++i) many += "c" + std::to_string(i) + ",";
    blocker::Config capped;
    settings::fromIni(many, capped);
    CHECK(capped.commands.size() == static_cast<std::size_t>(blocker::kMaxCommands));

    // 파일로 — 쓰고 다시 읽기, 없는 파일
    const std::wstring path = (std::filesystem::temp_directory_path() / L"cb_tests.ini").wstring();
    CHECK(settings::save(path, c));
    blocker::Config loaded;
    CHECK(settings::load(path, loaded));
    CHECK(!loaded.enabled && loaded.commands == c.commands && loaded.allowHost);
    std::filesystem::remove(path);
    CHECK(!settings::load(path, loaded));
    CHECK(!settings::save(L"", c));
}

} // namespace

int main() {
    testNormalize();
    testResolve();
    testClassify();
    testRequester();
    testIgnores();
    testBits();
    testBoard();
    testSettings();
    if (g_failed == 0) std::printf("all tests passed\n");
    return g_failed == 0 ? 0 : 1;
}
