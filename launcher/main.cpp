// CommandBlocker.exe — 실행 중인 게임에 command_blocker.dll 을 넣는 런처(Kaldrin.exe 의 기본 동작과 같다).
//
// 산출물은 exe 하나다. DLL 은 RCDATA 101 로 박혀 있다 — 게임 쪽 LoadLibraryW 는 파일 경로가 필요하므로
// `%LOCALAPPDATA%\CommandBlocker` 에 꺼내 놓고 그 경로를 넣는다. 게임은 풀트러스트 데스크톱 앱이라 ACL 부여도 관리자 권한도 필요 없다.

#include <windows.h>
#include <conio.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kProcess = L"Minecraft.Windows.exe";
constexpr const wchar_t* kModule = L"command_blocker.dll";
constexpr int kDllResource = 101;
constexpr DWORD kRemoteTimeoutMs = 15000;   // 게임이 로더 락을 오래 쥘 수 있어 넉넉히

std::string win32Error(const char* what) { return std::string(what) + " (Win32 오류 " + std::to_string(GetLastError()) + ")"; }

DWORD findProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    for (BOOL more = Process32FirstW(snap, &entry); more && pid == 0; more = Process32NextW(snap, &entry)) {
        if (_wcsicmp(entry.szExeFile, kProcess) == 0) pid = entry.th32ProcessID;
    }
    CloseHandle(snap);
    return pid;
}

bool moduleLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Module32FirstW(snap, &entry); more && !found; more = Module32NextW(snap, &entry)) {
        found = _wcsicmp(entry.szModule, kModule) == 0;
    }
    CloseHandle(snap);
    return found;
}

bool sameContent(const std::wstring& path, const void* data, DWORD size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    bool same = GetFileSizeEx(h, &length) && length.QuadPart == size;
    if (same) {
        std::vector<unsigned char> have(size);
        DWORD read = 0;
        same = ReadFile(h, have.data(), size, &read, nullptr) && read == size && std::memcmp(have.data(), data, size) == 0;
    }
    CloseHandle(h);
    return same;
}

// 박힌 DLL 을 꺼내 그 경로를 준다. 실패하면 빈 문자열과 error.
std::wstring extractDll(std::string& error) {
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(kDllResource), RT_RCDATA);
    HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = found ? SizeofResource(nullptr, found) : 0;
    if (!data || size == 0) {
        error = "이 exe 에 DLL 이 박혀 있지 않습니다";
        return {};
    }
    wchar_t appData[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        error = "%LOCALAPPDATA% 를 찾지 못했습니다";
        return {};
    }
    const std::wstring dir = std::wstring(appData) + L"\\CommandBlocker";
    CreateDirectoryW(dir.c_str(), nullptr);   // 이미 있으면 실패한다 — 상관없다
    const std::wstring path = dir + L"\\" + kModule;

    // 주입돼 있는 동안 그 파일은 잠겨 덮어쓸 수 없다 — 같은 내용이면 건드리지 않는다
    if (sameContent(path, data, size)) return path;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "DLL 을 꺼내 놓을 수 없습니다 — 게임에 예전 DLL 이 들어가 있으면 콘솔에서 unload 한 뒤 다시 하세요";
        return {};
    }
    DWORD wrote = 0;
    const bool ok = WriteFile(h, data, size, &wrote, nullptr) && wrote == size;
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(path.c_str());
        error = "DLL 을 꺼내 쓰다가 실패했습니다";
        return {};
    }
    return path;
}

// 게임 안에서 LoadLibraryW(dllPath) 를 원격 스레드로 부른다.
bool inject(DWORD pid, const std::wstring& dllPath, std::string& error) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                  PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        error = win32Error("게임 프로세스를 열지 못했습니다");
        return false;
    }
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    bool timedOut = false;
    if (!remote) {
        error = win32Error("게임에 메모리를 잡지 못했습니다");
    } else if (!WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr)) {
        error = win32Error("DLL 경로를 쓰지 못했습니다");
    } else {
        // kernel32 는 세션 안의 모든 프로세스에서 같은 주소에 올라온다 — 우리 쪽 주소를 그대로 쓴다
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
        HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!thread) {
            error = win32Error("원격 스레드를 만들지 못했습니다");
        } else if (WaitForSingleObject(thread, kRemoteTimeoutMs) != WAIT_OBJECT_0) {
            timedOut = true;
            error = "원격 스레드가 15초 안에 끝나지 않았습니다 — 게임이 멈춰 있을 수 있습니다";
        } else {
            DWORD exitCode = 0;   // x64 에서 HMODULE 이 32비트로 잘린다 — 0 인지만 본다
            GetExitCodeThread(thread, &exitCode);
            ok = exitCode != 0;
            if (!ok) error = "게임 안의 LoadLibraryW 가 실패했습니다";
        }
        if (thread) CloseHandle(thread);
    }
    // 원격 스레드가 시간을 넘겼으면 아직 그 경로를 읽는 중일 수 있다 — 그때는 풀지 않는다
    if (remote && !timedOut) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

int run() {
    std::string error;
    const std::wstring dll = extractDll(error);
    if (dll.empty()) {
        std::printf("실패: %s\n", error.c_str());
        return 1;
    }
    const DWORD pid = findProcess();
    if (pid == 0) {
        std::printf("게임(Minecraft.Windows.exe)을 찾지 못했습니다 — 게임을 먼저 실행하세요.\n");
        return 1;
    }
    // 두 번 넣으면 참조가 2 가 되어 콘솔의 unload 로 내려오지 않는다
    if (moduleLoaded(pid)) {
        std::printf("이미 주입돼 있습니다 — 게임 옆의 Command Blocker 콘솔 창을 보세요.\n");
        return 1;
    }
    if (!inject(pid, dll, error)) {
        std::printf("주입 실패: %s\n", error.c_str());
        return 1;
    }
    if (!moduleLoaded(pid)) {
        std::printf("주입 실패: DLL 이 올라오자마자 내려갔습니다.\n");
        return 1;
    }
    std::printf("주입 성공 (PID %lu) — 새로 뜬 Command Blocker 콘솔 창에서 help 를 입력하세요.\n", pid);
    return 0;
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    const int code = run();
    // 성공하면 곧바로 닫는다 — 게임 옆에 뜬 콘솔 창이 곧 성공의 표시다. 실패했고 탐색기에서 더블클릭했으면
    // 이 창은 우리만의 것이라, 이유를 읽기 전에 닫히지 않게 기다린다.
    DWORD processes[2];
    if (code != 0 && GetConsoleProcessList(processes, 2) == 1) {
        std::printf("\n아무 키나 누르면 닫힙니다...");
        _getch();
    }
    return code;
}
