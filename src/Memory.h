#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

// SEH 보호 아래 fn(arg) — 게임 메모리를 잘못 읽어도 게임이 죽지 않는다. 예외 없이 끝났으면 true.
bool runGuarded(void (*fn)(void*), void* arg);

// 보호 아래 bytes 만큼 읽는다. 주소가 0 이거나 읽다 터지면 false.
bool readMem(std::uint64_t address, void* out, std::size_t bytes);

template <class T>
bool readValue(std::uint64_t address, T& out) {
    return readMem(address, &out, sizeof(T));
}

} // namespace cb
