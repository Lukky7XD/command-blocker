#pragma once

#include <string>

// 게임 프로세스 자신의 콘솔 창 위의 명령줄.
namespace cb::cli {

bool open();    // 콘솔을 붙인다
void run(bool started, const std::string& startError);   // unload 를 칠 때까지 돈다
void close();   // 콘솔을 뗀다 — DLL 이 내려가기 전에 불러야 한다(Ctrl 처리기가 DLL 안에 있다)

} // namespace cb::cli
