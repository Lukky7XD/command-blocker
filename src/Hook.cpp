#include "Hook.h"

#include <windows.h>

#include <MinHook.h>

namespace cb::hook {

bool init(std::string& error) {
    const MH_STATUS s = MH_Initialize();
    if (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED) return true;
    error = std::string("MinHook 초기화 실패: ") + MH_StatusToString(s);
    return false;
}

void shutdown() { MH_Uninitialize(); }

bool create(void* target, void* detour, void** original, std::string& error) {
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) {
        s = MH_EnableHook(target);
        if (s == MH_OK) return true;
        MH_RemoveHook(target);
    }
    error = std::string("훅 실패: ") + MH_StatusToString(s);
    return false;
}

void remove(void* target, const std::atomic<int>& inDetour) {
    if (!target) return;
    MH_DisableHook(target);   // 이 뒤로는 아무도 디투어에 들어오지 않는다
    while (inDetour.load(std::memory_order_acquire) > 0) Sleep(1);
    Sleep(50);                // 세기를 내린 뒤 ret 까지 남은 몇 명령
    MH_RemoveHook(target);
}

} // namespace cb::hook
