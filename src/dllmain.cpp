#include "Blocker.h"
#include "Cli.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace {

// 설정 파일은 DLL 과 같은 폴더의 같은 이름 — command_blocker.dll → command_blocker.ini
std::wstring settingsPath(HMODULE module) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};   // 저장이 실패로 알려진다
    return std::filesystem::path(buf).replace_extension(L".ini").wstring();
}

// DllMain 은 로더 락 안이라 일을 하지 않는다 — 이 스레드가 전부 하고, 끝날 때 DLL 을 스스로 내린다.
DWORD WINAPI mainThread(LPVOID module) {
    if (cb::cli::open()) {
        const std::wstring settings = settingsPath(static_cast<HMODULE>(module));
        std::string error;
        const bool started = cb::blocker::start(settings, error);
        cb::cli::run(started, error, settings);
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
