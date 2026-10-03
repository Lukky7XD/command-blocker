# Command Blocker

마인크래프트 베드락(Windows) 전용 명령 차단기. **내가 호스트인 월드**(로컬 월드 · Xbox 친구가 들어온 내 월드)에서,
적어 둔 명령을 고른 주체가 내면 실행하지 않고 조용히 무시합니다. 요청한 사람에게는 거절 문구도 가지 않습니다.

- 막을 주체를 스위치로 고릅니다 — 플레이어(호스트 포함, 「호스트 허용」으로 뺄 수 있음) · 명령 블록(수레 포함) · NPC · 웹소켓
- 주체마다 「모든 명령 막기」 — 목록에 없는 명령까지 무시
- 별칭(`tp` · `msg` · `w`)과 `/execute … run …` 안의 명령도 같이 막힙니다
- 세계 설정 화면 · 행동 팩 · 함수 · 스크립트가 낸 명령은 막지 않습니다

UI 프레임워크 없이, 게임 프로세스에 붙는 콘솔 창에서 명령어로 다룹니다.

## 지원 버전

**Minecraft Bedrock 1.26.5203.0** (`Microsoft.MinecraftUWP`, x64) 전용입니다. 주소가 빌드마다 달라서,
게임 이미지의 크기와 빌드 시각이 맞지 않으면 훅을 걸지 않고 시작을 거부합니다.

## 사용법

1. 게임을 실행합니다.
2. `CommandBlocker.exe` 를 실행합니다 — 게임에 DLL 을 넣고, 게임 옆에 콘솔 창이 뜹니다.
3. 콘솔에서 명령어를 입력합니다.

```
status                  상태 · 스위치 · 막을 명령 목록 · 무시한 횟수
on | off                차단 켜기 / 끄기
add <명령>              막을 명령 추가 (최대 24개, 앞의 / 는 없어도 됨)
del <번호|명령>         막을 명령 삭제
clear                   막을 명령 모두 삭제
set <스위치> <on|off>   players · allowhost · allplayers
                        commandblocks · allcommandblocks
                        npcs · allnpcs · websockets · allwebsockets
unload                  차단을 풀고 DLL 을 내림
```

예) `/give` · `/tp` 를 나(호스트)만 쓰고, 다른 플레이어 · 명령 블록 · NPC · 웹소켓은 못 쓰게 하기:

```
add give
add tp
set allowhost on
```

### 주의

- **콘솔 창을 닫으면 게임도 함께 꺼집니다.** 콘솔이 게임 프로세스의 것이기 때문입니다 — 내릴 때는 `unload` 를 입력하세요.
- 남의 월드 · 외부 서버 · Realms 에서는 동작하지 않습니다(명령이 내 게임에서 실행되지 않습니다).
- `help` 처럼 각자의 게임에서 도는 명령은 막을 수 없습니다.
- 설정은 저장되지 않습니다 — 다시 넣으면 새로 입력해야 합니다.

## 동작 원리

모든 명령 실행이 지나는 `Command::run` 에 훅을 겁니다. 명령 객체가 든 (만든 CommandRegistry 주소, 명령 기호)가
목록에 있으면 요청한 주체를 `CommandOrigin::getOutputReceiver` 로 따라가 종류(플레이어 · 명령 블록 · 웹소켓 …)와
호스트 여부를 보고, 막을 주체면 원본을 부르지 않습니다. NPC 는 주체 종류로 가를 수 없어 호출한 자리(반환 주소)로 가립니다.

막을 이름은 0.5초마다 서버의 CommandRegistry(`MinecraftGame → ServerInstance → Minecraft → MinecraftCommands`)에서
명령 기호로 바꿉니다. 게임 메모리는 SEH 보호 아래에서 읽기만 하고, 사슬의 도착지마다 vtable 로 신원을 확인합니다.
주소와 오프셋은 [`src/Offsets.h`](src/Offsets.h) 에 있습니다.

## 빌드

Visual Studio 2022 (MSVC, x64) · CMake 3.20 이상.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

| 산출물 | 설명 |
|---|---|
| `build/Release/CommandBlocker.exe` | 런처 — DLL 이 박혀 있어 이것 하나만 있으면 됩니다 |
| `build/Release/command_blocker.dll` | 차단기 본체 (다른 인젝터로 넣어도 됩니다) |
| `build/Release/cb_tests.exe` | 게임 없이 도는 시험 — **Release 로 빌드해야** MSVC `std::map` 배치가 게임과 같습니다 |

## 라이선스

[MIT](LICENSE). [`vendor/minhook`](vendor/minhook) 은 [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause,
[LICENSE](vendor/minhook/LICENSE.txt)) 입니다.

Mojang Studios · Microsoft 와 관계없는 비공식 프로젝트입니다.
