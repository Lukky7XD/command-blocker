#pragma once

#include "CommandGate.h"

#include <cstdint>
#include <string>

// `Command::run` 디투어 — 목록 판이 무시하라는 주체가 낸 명령이면 원본을 부르지 않는다.
namespace cb::cmdrun {

bool install(std::uint8_t* gameBase, std::string& error);
void remove();   // 목록을 비우고, 안에 있는 스레드가 다 나간 뒤 뗀다
bool installed();

// 막을 목록. 같은 목록이면 쓰지 않는다.
void publish(const gate::Blocklist& list);

// 무시한 명령 수 — 주체 분류마다
unsigned long long ignoredCount(gate::Verdict v);

} // namespace cb::cmdrun
