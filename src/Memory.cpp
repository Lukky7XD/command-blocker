#include "Memory.h"

#include <windows.h>

#include <cstring>

namespace cb {
namespace {

struct CopyCtx {
    const void* from;
    void* to;
    std::size_t bytes;
};

void copy(void* p) {
    auto* c = static_cast<CopyCtx*>(p);
    std::memcpy(c->to, c->from, c->bytes);
}

} // namespace

// __try 는 소멸자가 있는 객체를 쓰는 함수 안에 둘 수 없다 — 그래서 일은 함수 포인터로 받는다.
bool runGuarded(void (*fn)(void*), void* arg) {
    __try {
        fn(arg);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool readMem(std::uint64_t address, void* out, std::size_t bytes) {
    if (address == 0) return false;
    CopyCtx ctx{reinterpret_cast<const void*>(address), out, bytes};
    return runGuarded(&copy, &ctx);
}

} // namespace cb
