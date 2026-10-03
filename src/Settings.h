#pragma once

#include "Blocker.h"

#include <string>
#include <string_view>

// 설정 파일 — DLL 과 같은 폴더의 command_blocker.ini. 사람이 고쳐도 되는 꼴이다:
//   [CommandBlocker]
//   enabled=on
//   commands=give,tp
//   players=on   (그 밖의 스위치도 콘솔의 set 이름 그대로)
namespace cb::settings {

std::string toIni(const blocker::Config& c);
// 적힌 칸만 바꾼다 — 없는 칸 · 모르는 칸 · 잘못된 값은 그대로 둔다.
void fromIni(std::string_view text, blocker::Config& c);

bool load(const std::wstring& path, blocker::Config& c);   // 파일이 없으면 false
bool save(const std::wstring& path, const blocker::Config& c);

} // namespace cb::settings
