#pragma once

#include <atomic>
#include <string>

// MinHook 위의 얇은 함수들.
namespace cb::hook {

bool init(std::string& error);
void shutdown();

// 걸고 켠다. 실패하면 반쯤 걸린 상태를 남기지 않는다.
bool create(void* target, void* detour, void** original, std::string& error);

// 끄고, 디투어 안에 있는 스레드가 다 나간 뒤에 지운다 — 트램펄린이 사라지기 전에 빠져나오게.
void remove(void* target, const std::atomic<int>& inDetour);

} // namespace cb::hook
