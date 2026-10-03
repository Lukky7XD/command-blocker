#include "Game.h"

#include "Hook.h"
#include "Memory.h"
#include "Offsets.h"

#include <windows.h>

#include <atomic>
#include <format>

namespace cb::game {
namespace {

using TickInputFn = void* (*)(void*, void*, void*, void*);
TickInputFn g_original = nullptr;
void* g_target = nullptr;
std::uint8_t* g_base = nullptr;
std::atomic<void*> g_game{nullptr};
std::atomic<int> g_inDetour{0};

// 하는 일은 this 를 한 번 잡는 것뿐 — 게임 스레드에서 매 프레임 돈다.
void* hkTickInput(void* self, void* b, void* c, void* d) {
    g_inDetour.fetch_add(1, std::memory_order_acq_rel);
    if (self != nullptr && g_game.load(std::memory_order_relaxed) == nullptr) g_game.store(self, std::memory_order_release);
    void* result = g_original(self, b, c, d);
    g_inDetour.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

enum class Hop { Ok, Broken, Mismatch, Fault };

const char* hopName(Hop h) {
    switch (h) {
    case Hop::Broken: return "BrokenPath";
    case Hop::Mismatch: return "Mismatch";
    case Hop::Fault: return "Fault";
    default: return "";
    }
}

// [from + offset] 을 따라가고, 도착한 객체의 vtable 이 기대한 것인지 본다 — 오프셋만으로는 신원이 아니다.
Hop follow(std::uint64_t from, std::uint32_t offset, std::uint32_t vtableRva, std::uint64_t& out) {
    if (!readValue(from + offset, out)) return Hop::Fault;
    if (out == 0) return Hop::Broken;
    std::uint64_t vtable = 0;
    if (!readValue(out, vtable)) return Hop::Fault;
    return vtable == reinterpret_cast<std::uint64_t>(g_base) + vtableRva ? Hop::Ok : Hop::Mismatch;
}

} // namespace

std::uint8_t* verifiedBase(std::string& why) {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (base == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        why = "게임 이미지를 읽지 못했습니다";
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        why = "게임 이미지를 읽지 못했습니다";
        return nullptr;
    }
    if (nt->OptionalHeader.SizeOfImage != off::kImageSize || nt->FileHeader.TimeDateStamp != off::kTimeDateStamp) {
        why = std::format("지원하지 않는 게임 빌드입니다 — 1.26.5203.0 전용 (이 이미지: 크기 {} · 빌드 시각 0x{:08X})",
                          nt->OptionalHeader.SizeOfImage, nt->FileHeader.TimeDateStamp);
        return nullptr;
    }
    return base;
}

bool installRootHook(std::uint8_t* gameBase, std::string& error) {
    if (g_target) return true;
    g_base = gameBase;
    void* target = gameBase + off::kTickInput;
    if (!hook::create(target, reinterpret_cast<void*>(&hkTickInput), reinterpret_cast<void**>(&g_original), error)) {
        error = "MinecraftGame::tickInput " + error;
        return false;
    }
    g_target = target;
    return true;
}

void removeRootHook() {
    if (!g_target) return;
    hook::remove(g_target, g_inDetour);
    g_target = nullptr;
    g_original = nullptr;
}

void* minecraftGame() { return g_game.load(std::memory_order_acquire); }

std::uint64_t serverCommandRegistry(const char*& why, bool& noServer) {
    noServer = false;
    const auto mg = reinterpret_cast<std::uint64_t>(minecraftGame());
    if (mg == 0 || g_base == nullptr) {
        why = "NoRoot";
        return 0;
    }
    std::uint64_t instance = 0, minecraft = 0, commands = 0, session = 0, handler = 0;
    Hop h = follow(mg, off::kGameServerInstance, off::kVtServerInstance, instance);
    if (h == Hop::Ok) h = follow(instance, off::kInstanceMinecraft, off::kVtMinecraft, minecraft);
    if (h != Hop::Ok) {
        // 사슬이 0 에서 끊겼다 = in-process 서버가 없다(월드 밖 · 남의 월드 · 서버) — 정상 상태다
        noServer = h == Hop::Broken;
        why = hopName(h);
        return 0;
    }
    if ((h = follow(minecraft, off::kMinecraftCommands, off::kVtMinecraftCommands, commands)) != Hop::Ok) {
        why = hopName(h);
        return 0;
    }
    if (!readValue(minecraft + off::kMinecraftSession, session) || session == 0) {
        why = "NoSession";
        return 0;
    }
    if ((h = follow(session, off::kSessionHandler, off::kVtServerNetworkHandler, handler)) != Hop::Ok) {
        why = hopName(h);
        return 0;
    }
    std::uint64_t held = 0;
    if (!readValue(handler + off::kHandlerCommands, held) || held != commands) {
        why = "NotServerCommands";
        return 0;
    }
    std::uint64_t registry = 0;
    if (!readValue(commands + off::kCommandsRegistry, registry) || registry == 0) {
        why = "NoRegistry";
        return 0;
    }
    return registry;
}

} // namespace cb::game
