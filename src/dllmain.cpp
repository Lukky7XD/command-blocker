#include "Blocker.h"
#include "Cli.h"

#include <windows.h>

#include <string>

namespace {

// DllMain 은 로더 락 안이라 일을 하지 않는다 — 이 스레드가 전부 하고, 끝날 때 DLL 을 스스로 내린다.
DWORD WINAPI mainThread(LPVOID module) {
    if (cb::cli::open()) {
        std::string error;
        const bool started = cb::blocker::start(error);
        cb::cli::run(started, error);
        if (started) cb::blocker::stop();
        cb::cli::close();
    }
    FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (HANDLE thread = CreateThread(nullptr, 0, &mainThread, module, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
