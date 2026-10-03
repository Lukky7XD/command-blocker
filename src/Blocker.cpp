#include "Blocker.h"

#include "CommandGate.h"
#include "CommandHook.h"
#include "Game.h"
#include "Hook.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace cb::blocker {
namespace {

constexpr auto kInterval = std::chrono::milliseconds(500);   // 게임 메모리를 걸으므로 프레임마다 하지 않는다

std::mutex g_mutex;   // 아래 셋과 맞추기
Config g_config;
Status g_status;
bool g_stopping = false;
std::condition_variable g_wake;
// std::thread 가 아니다 — 게임이 끝날 때 DLL 의 정적 소멸자가 joinable 한 std::thread 를 만나면 terminate 한다.
HANDLE g_ticker = nullptr;

void applyLocked() {
    Status s;
    s.hookReady = cmdrun::installed();
    s.rootReady = game::minecraftGame() != nullptr;
    const Bits bits = toBits(g_config);
    const char* why = "";
    const std::uint64_t registry = game::serverCommandRegistry(why, s.noServer);
    gate::Resolved r = registry != 0 ? gate::resolve(registry, g_config.commands) : gate::Resolved{};
    r.list.sources = bits.sources;
    r.list.all = bits.all;
    s.hosting = registry != 0 && r.ok;
    s.why = registry != 0 ? r.why : why;
    s.blocked = static_cast<int>(r.list.count);
    s.unknown = std::move(r.unknown);

    if (!g_config.enabled || (g_config.commands.empty() && bits.all == 0)) {
        cmdrun::publish(gate::Blocklist{});
    } else if (s.hosting) {
        cmdrun::publish(r.list);
    } else if (s.noServer) {
        cmdrun::publish(gate::Blocklist{});
    }
    // 그 밖(로딩 중 사슬이 잠깐 어긋남)은 앞의 목록을 둔다 — 비우면 그 틈에 명령이 지나간다.
    // 목록은 만든 표의 주소가 같아야 걸리므로 새 월드에는 저절로 안 걸린다.
    g_status = std::move(s);
}

DWORD WINAPI tickerMain(LPVOID) {
    std::unique_lock<std::mutex> lock(g_mutex);
    while (!g_stopping) {
        applyLocked();
        g_wake.wait_for(lock, kInterval, [] { return g_stopping; });
    }
    return 0;
}

} // namespace

Bits toBits(const Config& c) {
    Bits b;
    if (c.players) {
        const std::uint8_t who = gate::kOtherPlayers | (c.allowHost ? 0 : gate::kHost);
        b.sources |= who;
        if (c.allPlayers) b.all |= who;
    }
    if (c.commandBlocks) {
        b.sources |= gate::kCommandBlocks;
        if (c.allCommandBlocks) b.all |= gate::kCommandBlocks;
    }
    if (c.npcs) {
        b.sources |= gate::kNpcs;
        if (c.allNpcs) b.all |= gate::kNpcs;
    }
    if (c.websockets) {
        b.sources |= gate::kWebsockets;
        if (c.allWebsockets) b.all |= gate::kWebsockets;
    }
    return b;
}

bool start(std::string& error) {
    std::uint8_t* base = game::verifiedBase(error);
    if (!base || !hook::init(error)) return false;
    if (!game::installRootHook(base, error) || !cmdrun::install(base, error)) {
        cmdrun::remove();
        game::removeRootHook();
        hook::shutdown();
        return false;
    }
    g_stopping = false;
    g_ticker = CreateThread(nullptr, 0, &tickerMain, nullptr, 0, nullptr);
    return true;
}

void stop() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stopping = true;
    }
    g_wake.notify_all();
    if (g_ticker) {
        WaitForSingleObject(g_ticker, INFINITE);
        CloseHandle(g_ticker);
        g_ticker = nullptr;
    }
    cmdrun::remove();
    game::removeRootHook();
    hook::shutdown();
}

Config config() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_config;
}

void edit(const std::function<void(Config&)>& change) {
    std::lock_guard<std::mutex> lock(g_mutex);
    change(g_config);
    applyLocked();
}

Status status() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}

} // namespace cb::blocker
