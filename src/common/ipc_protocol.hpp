#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace liteime::ipc {

constexpr uint32_t kProtocolMagic = 0x4C494D45; // 'LIME'
constexpr uint16_t kProtocolVersionV1 = 1;
constexpr uint16_t kProtocolVersionV2 = 2;
constexpr uint16_t kProtocolVersion = 3;
constexpr size_t kMaxPreeditBytesV1 = 256;
constexpr size_t kMaxCommitBytesV1 = 512;
constexpr size_t kMaxPreeditBytes = 512;
constexpr size_t kMaxCommitBytes = 4096;
constexpr size_t kMaxOverlayCandidates = 8;
constexpr size_t kMaxOverlayHeaderBytes = 128;
constexpr size_t kMaxOverlayCandTextBytes = 64;
constexpr size_t kMaxOverlayCommentBytes = 32;

enum class MsgType : uint16_t {
    KeyTest = 1,
    KeyEvent = 2,
    FocusChanged = 3,
    ResetComposition = 4,
    Ping = 5,
    UpdateCaret = 6,
    SelectCandidate = 7,
    PrevPage = 8,
    NextPage = 9,
};

enum RequestFlags : uint8_t {
    ReqFlagInProcUi = 1u << 0,
};

enum ModifierFlags : uint32_t {
    ModShift = 1u << 0,
    ModCtrl  = 1u << 1,
    ModAlt   = 1u << 2,
    ModCaps  = 1u << 3,
};

#pragma pack(push, 1)
struct Request {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersion;
    MsgType type = MsgType::Ping;
    uint32_t vk = 0;
    uint32_t scanCode = 0;
    uint32_t modifiers = 0;
    uint8_t isKeyUp = 0;
    uint8_t hasCaret = 0;
    uint8_t focusGained = 0;
    uint8_t reserved = 0;
    int32_t caretX = 0;
    int32_t caretY = 0;
    int32_t caretHeight = 20;
    uint64_t hwnd = 0;
};

struct ResponseV1 {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersionV1;
    uint8_t consumed = 0;
    uint8_t isEnglish = 0;
    uint8_t isFullWidth = 0;
    uint8_t isCnPunct = 1;
    uint16_t preeditLen = 0;
    uint16_t commitLen = 0;
    uint16_t caretPos = 0;
    char preedit[kMaxPreeditBytesV1] = {};
    char commit[kMaxCommitBytesV1] = {};
};

struct ResponseV2 {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersionV2;
    uint8_t consumed = 0;
    uint8_t isEnglish = 0;
    uint8_t isFullWidth = 0;
    uint8_t isCnPunct = 1;
    uint8_t isComposing = 0;
    uint8_t reserved = 0;
    uint16_t preeditLen = 0;
    uint16_t commitLen = 0;
    uint16_t caretPos = 0;
    char preedit[kMaxPreeditBytes] = {};
    char commit[kMaxCommitBytes] = {};
};

struct OverlayCandidate {
    char text[kMaxOverlayCandTextBytes] = {};
    char comment[kMaxOverlayCommentBytes] = {};
    uint8_t textLen = 0;
    uint8_t commentLen = 0;
    int8_t pinnedSlot = -1;
    uint8_t reserved = 0;
};

struct Response {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersion;
    uint8_t consumed = 0;
    uint8_t isEnglish = 0;
    uint8_t isFullWidth = 0;
    uint8_t isCnPunct = 1;
    uint8_t isComposing = 0;
    uint8_t reserved = 0;
    uint16_t preeditLen = 0;
    uint16_t commitLen = 0;
    uint16_t caretPos = 0;
    char preedit[kMaxPreeditBytes] = {};
    char commit[kMaxCommitBytes] = {};
    uint8_t showCandidates = 0;
    uint8_t selectedIndex = 0;
    uint8_t candCount = 0;
    uint8_t skin = 0;
    uint8_t layout = 0;
    uint8_t squareFrame = 1;
    uint16_t page = 0;
    uint16_t totalPages = 0;
    uint16_t headerLen = 0;
    char header[kMaxOverlayHeaderBytes] = {};
    OverlayCandidate candidates[kMaxOverlayCandidates]{};
};

struct NotifyHello {
    uint32_t magic = kProtocolMagic;
    uint32_t pid = 0;
    uint64_t hwnd = 0;
};

enum class NotifyType : uint16_t {
    ApplyResponse = 1,
};

struct NotifyPacket {
    uint32_t magic = kProtocolMagic;
    uint16_t type = static_cast<uint16_t>(NotifyType::ApplyResponse);
    uint16_t reserved = 0;
    uint64_t targetHwnd = 0;
    Response response{};
};
#pragma pack(pop)

static_assert(sizeof(ResponseV1) == 16 + kMaxPreeditBytesV1 + kMaxCommitBytesV1,
              "ResponseV1 wire size must match legacy packed protocol layout");
static_assert(sizeof(ResponseV2) == 18 + kMaxPreeditBytes + kMaxCommitBytes,
              "ResponseV2 wire size must match v2 packed protocol layout");
static_assert(sizeof(Response) == sizeof(ResponseV2) + 12 + kMaxOverlayHeaderBytes +
                                  kMaxOverlayCandidates * sizeof(OverlayCandidate),
              "Response wire size must match packed v3 protocol layout");

inline std::wstring makePipeName(uint32_t sessionId) {
    return L"\\\\.\\pipe\\LiteIME_Host_" + std::to_wstring(sessionId);
}

inline std::wstring makeNotifyPipeName(uint32_t sessionId) {
    return L"\\\\.\\pipe\\LiteIME_Notify_" + std::to_wstring(sessionId);
}

} // namespace liteime::ipc
