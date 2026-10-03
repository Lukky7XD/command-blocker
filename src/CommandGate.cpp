#include "CommandGate.h"

#include "Memory.h"
#include "Offsets.h"

#include <algorithm>
#include <cstring>

namespace cb::gate {
namespace {

// 레드-블랙 트리라 수천 개여도 깊이는 30 안쪽이다 — 이만큼 내려가도 안 끝나면 모양이 아니다.
constexpr int kMaxDepth = 64;
// 이름 값의 상한(모양 검사) — 쓰레기 길이를 믿고 할당하지 않는다.
constexpr std::uint64_t kMaxNameBytes = 256;

// MSVC std::string: +0x00 버퍼(16바이트) 또는 힙 포인터 · +0x10 길이 · +0x18 용량. 용량 15 면 글자가 버퍼 안에 있다.
bool readString(std::uint64_t at, std::string& out) {
    std::uint64_t w[4] = {};
    if (!readMem(at, w, sizeof(w))) return false;
    const std::uint64_t size = w[2];
    const std::uint64_t cap = w[3];
    if (size > cap || size > kMaxNameBytes) return false;
    if (cap == 15) {
        out.assign(reinterpret_cast<const char*>(w), static_cast<std::size_t>(size));
        return true;
    }
    if (cap < 16) return false;
    out.resize(static_cast<std::size_t>(size));
    return size == 0 || readMem(w[0], out.data(), static_cast<std::size_t>(size));
}

// std::map 하나(`mapAt` = 머리 포인터 칸)에서 키로 노드를 찾는다. 머리 표식과 개수로 모양을 먼저 본다.
std::uint64_t findNode(std::uint64_t mapAt, std::string_view key) {
    std::uint64_t head = 0;
    std::uint64_t size = 0;
    std::uint8_t headNil = 0;
    if (!readValue(mapAt, head) || head == 0 || !readValue(mapAt + off::kMapSize, size) || size > off::kMaxSignatures ||
        !readValue(head + off::kNodeIsNil, headNil) || headNil != 1) {
        return 0;
    }
    std::uint64_t node = 0;
    if (!readValue(head + off::kNodeParent, node)) return 0;   // 머리의 부모 = 뿌리
    std::string nodeKey;
    for (int depth = 0; depth < kMaxDepth && node != 0 && node != head; ++depth) {
        std::uint8_t nil = 0;
        if (!readValue(node + off::kNodeIsNil, nil) || nil != 0) return 0;
        if (!readString(node + off::kNodeKey, nodeKey)) return 0;
        const int c = nodeKey.compare(key);
        if (c == 0) return node;
        if (!readValue(node + (c > 0 ? off::kNodeLeft : off::kNodeRight), node)) return 0;
    }
    return 0;
}

// 이름 → 정의 노드. 없으면 별칭 표의 본래 이름으로 한 번 더(게임의 findCommand 와 같은 순서).
std::uint64_t findSignatureNode(std::uint64_t registry, std::string_view name) {
    if (const std::uint64_t node = findNode(registry + off::kRegistrySignatures, name)) return node;
    const std::uint64_t alias = findNode(registry + off::kRegistryAliases, name);
    if (alias == 0) return 0;
    std::string target;
    if (!readString(alias + off::kNodeValue, target) || target.empty() || target == name) return 0;
    return findNode(registry + off::kRegistrySignatures, target);
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// 게임의 가상 함수 — this 하나만 받는다. `at` 은 vtable 안의 바이트 자리.
using GetPtrFn = void* (*)(void*);
using GetByteFn = std::uint8_t (*)(void*);

void* callPointer(void* object, std::uint32_t at) {
    void* const* vtable = *static_cast<void* const* const*>(object);
    return reinterpret_cast<GetPtrFn>(vtable[at / 8])(object);
}
std::uint8_t callByte(void* object, std::uint32_t at) {
    void* const* vtable = *static_cast<void* const* const*>(object);
    return reinterpret_cast<GetByteFn>(vtable[at / 8])(object);
}

} // namespace

std::string normalizeName(std::string_view text) {
    std::size_t b = 0;
    std::size_t e = text.size();
    while (b < e && (isSpace(text[b]) || text[b] == '/')) ++b;
    while (b < e && isSpace(text[e - 1])) --e;
    std::string out;
    for (std::size_t i = b; i < e && !isSpace(text[i]); ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

bool contains(const Blocklist& list, std::uint64_t registry, std::uint32_t symbol) {
    if (list.registry == 0 || registry != list.registry) return false;
    const std::uint32_t n = std::min<std::uint32_t>(list.count, kMaxBlocked);
    return std::find(list.symbols, list.symbols + n, symbol) != list.symbols + n;
}

bool sameList(const Blocklist& a, const Blocklist& b) {
    if (a.registry != b.registry || a.count != b.count || a.sources != b.sources || a.all != b.all) return false;
    const std::uint32_t n = std::min<std::uint32_t>(a.count, kMaxBlocked);
    return std::equal(a.symbols, a.symbols + n, b.symbols);
}

Resolved resolve(std::uint64_t registry, const std::vector<std::string>& wanted) {
    Resolved r;
    if (registry == 0) {
        r.why = "NoRegistry";
        return r;
    }
    // 표의 모양부터 — 안 맞으면 아무것도 믿지 않는다
    std::uint64_t head = 0;
    std::uint64_t size = 0;
    std::uint8_t nil = 0;
    if (!readValue(registry + off::kRegistrySignatures, head) || head == 0 ||
        !readValue(registry + off::kRegistrySignatures + off::kMapSize, size) || size == 0 || size > off::kMaxSignatures ||
        !readValue(head + off::kNodeIsNil, nil) || nil != 1) {
        r.why = "Shape";
        return r;
    }
    r.list.registry = registry;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const std::string name = normalizeName(wanted[i]);
        if (name.empty()) continue;
        const std::uint64_t node = findSignatureNode(registry, name);
        std::uint32_t symbol = 0;
        if (node == 0 || !readValue(node + off::kNodeValue + off::kSignatureSymbol, symbol)) {
            r.unknown.push_back(static_cast<int>(i));
            continue;
        }
        if (contains(r.list, registry, symbol)) continue;   // 별칭과 본래 이름을 둘 다 적었다
        if (r.list.count >= kMaxBlocked) break;
        r.list.symbols[r.list.count++] = symbol;
    }
    r.ok = true;
    return r;
}

void BlockBoard::write(const Blocklist& list) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    seq_.fetch_add(1, std::memory_order_acq_rel);   // 홀수 — 쓰는 중
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&list_, &list, sizeof list_);
    seq_.fetch_add(1, std::memory_order_release);   // 짝수 — 다 썼다
}

bool BlockBoard::read(Blocklist& out) const {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = seq_.load(std::memory_order_acquire);
        if ((before & 1u) != 0) continue;
        std::memcpy(&out, &list_, sizeof out);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) == before) return true;
    }
    return false;
}

CommandKind classifyCommand(const void* command, const Blocklist& list) {
    if (command == nullptr || list.registry == 0) return CommandKind::Elsewhere;
    const auto* bytes = static_cast<const std::uint8_t*>(command);
    std::uint64_t registry = 0;
    std::memcpy(&registry, bytes + off::kCommandRegistry, sizeof registry);
    if (registry != list.registry) return CommandKind::Elsewhere;
    std::uint32_t symbol = 0;
    std::memcpy(&symbol, bytes + off::kCommandSymbol, sizeof symbol);
    return contains(list, registry, symbol) ? CommandKind::Listed : CommandKind::Unlisted;
}

Requester findRequester(void* origin) {
    Requester who;
    void* current = origin;
    for (int step = 0; current != nullptr && step < 4; ++step) {
        void* receiver = callPointer(current, off::kOriginReceiver);
        if (receiver == nullptr) return who;
        const std::uint32_t type = callByte(receiver, off::kOriginType);
        if (type == off::kTypeVirtual && receiver != current) {   // 요청한 주체의 복사본을 든 쪽으로
            current = receiver;
            continue;
        }
        who.type = type;
        if (type == off::kTypePlayer) {
            if (void* entity = callPointer(receiver, off::kOriginEntity)) {
                who.hasPlayer = true;
                who.hosting = *(static_cast<const std::uint8_t*>(entity) + off::kPlayerHosting);
            }
        }
        who.readable = true;
        return who;
    }
    return who;   // Virtual 이 넷 넘게 이어졌다 — 못 읽음
}

Verdict judge(const Requester& who) {
    if (who.npc) return Verdict::Npc;   // 부른 자리가 곧 신원 — 주체를 못 읽었어도
    if (!who.readable) return Verdict::Unreadable;
    if (who.type == off::kTypePlayer) {
        if (!who.hasPlayer) return Verdict::OtherPlayer;   // 떠난 플레이어 — 호스트는 늘 있다
        if (who.hosting > 1) return Verdict::Unreadable;   // bool 모양이 아니다
        return who.hosting == 1 ? Verdict::Host : Verdict::OtherPlayer;
    }
    if (who.type == off::kTypeCommandBlock || who.type == off::kTypeCommandBlockMinecart) return Verdict::CommandBlock;
    if (who.type == off::kTypeAutomationPlayer || who.type == off::kTypeClientAutomation) return Verdict::Websocket;
    return Verdict::Other;
}

bool ignores(Verdict v, std::uint8_t sources, std::uint8_t all, bool listed) {
    std::uint8_t bit = 0;
    switch (v) {
    case Verdict::Host: bit = kHost; break;
    case Verdict::OtherPlayer: bit = kOtherPlayers; break;
    case Verdict::CommandBlock: bit = kCommandBlocks; break;
    case Verdict::Websocket: bit = kWebsockets; break;
    case Verdict::Npc: bit = kNpcs; break;
    case Verdict::Other:
    case Verdict::Unreadable: return false;
    }
    if ((sources & bit) == 0) return false;
    return listed || (all & bit) != 0;
}

} // namespace cb::gate
