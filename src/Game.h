#pragma once

#include <cstdint>
#include <string>

namespace cb::game {

// 실행 중인 게임이 1.26.5203.0 이면 이미지 베이스, 아니면 nullptr 과 사유.
std::uint8_t* verifiedBase(std::string& why);

// MinecraftGame::tickInput 에 통과 디투어를 걸어 MinecraftGame* 를 잡는다.
bool installRootHook(std::uint8_t* gameBase, std::string& error);
void removeRootHook();
void* minecraftGame();

// 내 게임이 서버일 때(로컬 월드 · 친구가 들어온 내 월드)의 CommandRegistry 주소. 못 찾으면 0 과 why.
// noServer = 서버 사슬이 비어 있다(월드 밖이거나 남의 월드 · 서버).
std::uint64_t serverCommandRegistry(const char*& why, bool& noServer);

} // namespace cb::game
