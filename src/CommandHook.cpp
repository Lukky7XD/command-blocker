#include "CommandHook.h"

#include "Hook.h"
#include "Memory.h"
#include "Offsets.h"

#include <intrin.h>

#include <atomic>

namespace cb::cmdrun {
namespace {

// 인자는 셋(명령 · origin · CommandOutput)이지만 넷째 레지스터도 그대로 넘긴다 — 잃을 것이 없다.
using RunFn = void (*)(void*, void*, void*, void*);
RunFn g_original = nullptr;
void* g_target = nullptr;
std::uint8_t* g_npcCaller = nullptr;   // NPC 동작의 Command::run 호출 반환 주소
std::atomic<int> g_inDetour{0};
gate::BlockBoard g_board;
// 이 스레드에서 NPC 자리의 호출이 원본을 도는 깊이 — 그 안에서 불린 명령(`/execute … run` · `/function`)도 NPC 가 낸 것이다.
thread_local int t_npcDepth = 0;
std::atomic<unsigned long long> g_ignored[gate::kVerdictCount] = {};

struct InspectCtx {
    const void* command;
    void* origin;
    const gate::Blocklist* list;
    bool checked;   // 요청한 주체를 볼 명령이다
    bool listed;    // 그중 목록의 명령
    gate::Requester who;
};

// 보호 아래 — 명령의 표 · 기호를 읽고, 볼 명령이면 요청한 주체까지 걷는다.
void inspect(void* p) {
    auto* c = static_cast<InspectCtx*>(p);
    const gate::CommandKind kind = gate::classifyCommand(c->command, *c->list);
    if (kind == gate::CommandKind::Elsewhere) return;
    if (kind == gate::CommandKind::Unlisted && c->list->all == 0) return;
    c->checked = true;
    c->listed = kind == gate::CommandKind::Listed;
    c->who = gate::findRequester(c->origin);
}

void hkRun(void* command, void* origin, void* output, void* r9) {
    g_inDetour.fetch_add(1, std::memory_order_acq_rel);
    const bool npcSite = g_npcCaller != nullptr && static_cast<std::uint8_t*>(_ReturnAddress()) == g_npcCaller;
    const bool npc = npcSite || t_npcDepth > 0;
    bool skip = false;
    gate::Blocklist list;
    if (command != nullptr && origin != nullptr && g_board.read(list) && list.registry != 0 &&
        (list.count > 0 || list.all != 0)) {
        InspectCtx ctx{command, origin, &list, false, false, {}};
        const bool ok = runGuarded(&inspect, &ctx);
        if (ctx.checked) {
            if (!ok) ctx.who.readable = false;   // 주체를 읽다 터졌다 — 못 읽음(통과)
            ctx.who.npc = npc;
            const gate::Verdict v = gate::judge(ctx.who);
            skip = gate::ignores(v, list.sources, list.all, ctx.listed);
            if (skip) g_ignored[static_cast<int>(v)].fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!skip) {
        if (npcSite) ++t_npcDepth;
        try {
            g_original(command, origin, output, r9);
        } catch (...) {
            if (npcSite) --t_npcDepth;
            g_inDetour.fetch_sub(1, std::memory_order_acq_rel);
            throw;   // 게임의 예외는 게임의 것이다 — 세기만 내리고 그대로 올려 보낸다
        }
        if (npcSite) --t_npcDepth;
    }
    g_inDetour.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace

bool install(std::uint8_t* gameBase, std::string& error) {
    if (g_target) return true;
    g_npcCaller = gameBase + off::kCommandRunNpcCaller;
    g_board.write(gate::Blocklist{});
    void* target = gameBase + off::kCommandRun;
    if (!hook::create(target, reinterpret_cast<void*>(&hkRun), reinterpret_cast<void**>(&g_original), error)) {
        error = "Command::run " + error;
        return false;
    }
    g_target = target;
    return true;
}

void remove() {
    g_board.write(gate::Blocklist{});
    if (!g_target) return;
    hook::remove(g_target, g_inDetour);
    g_target = nullptr;
    g_original = nullptr;
}

bool installed() { return g_target != nullptr; }

void publish(const gate::Blocklist& list) {
    gate::Blocklist now;
    if (g_board.read(now) && gate::sameList(now, list)) return;
    g_board.write(list);
}

unsigned long long ignoredCount(gate::Verdict v) {
    return g_ignored[static_cast<int>(v)].load(std::memory_order_relaxed);
}

} // namespace cb::cmdrun
