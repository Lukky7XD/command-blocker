cmake_minimum_required(VERSION 3.20)
project(command_blocker LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "x64 로만 빌드합니다 (cmake -A x64)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 정적 CRT — 게임 프로세스에 맞는 VC 런타임 DLL 이 없어도 뜬다
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC vendor/minhook/include)

# 게임 없이 시험하는 부분까지 — DLL 과 시험이 같이 쓴다
add_library(cb_core STATIC
    src/Blocker.cpp
    src/CommandGate.cpp
    src/CommandHook.cpp
    src/Game.cpp
    src/Hook.cpp
    src/Memory.cpp)
target_include_directories(cb_core PUBLIC src)
target_compile_options(cb_core PUBLIC /utf-8 /W4 /permissive-)
target_compile_definitions(cb_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(cb_core PUBLIC minhook)

add_library(command_blocker SHARED src/dllmain.cpp src/Cli.cpp)
target_link_libraries(command_blocker PRIVATE cb_core)

# 런처 — DLL 을 RCDATA 101 로 박은 exe 하나(Kaldrin.exe 와 같은 방식). 리소스 파일은 구성마다 DLL 경로가 달라 file(GENERATE) 로 만들고,
# OBJECT_DEPENDS 가 없으면 DLL 만 바뀌었을 때 리소스가 낡은 채로 남는다.
set(dll_rc "${CMAKE_CURRENT_BINARY_DIR}/generated/command_blocker_dll_$<CONFIG>.rc")
file(GENERATE OUTPUT "${dll_rc}" CONTENT "101 RCDATA \"$<TARGET_FILE:command_blocker>\"\n")
set_source_files_properties("${dll_rc}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "$<TARGET_FILE:command_blocker>")
add_executable(launcher launcher/main.cpp "${dll_rc}")
set_target_properties(launcher PROPERTIES OUTPUT_NAME "CommandBlocker")
target_compile_options(launcher PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_dependencies(launcher command_blocker)

enable_testing()
add_executable(cb_tests tests/test_gate.cpp)
target_link_libraries(cb_tests PRIVATE cb_core)
add_test(NAME cb_tests COMMAND cb_tests)
