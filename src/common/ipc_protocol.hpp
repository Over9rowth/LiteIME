#pragma once

#include <cstdint>

namespace liteime::ipc {

constexpr uint32_t kProtocolMagic = 0x4C494D45; // 'LIME'
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kMaxPreeditBytes = 256;
constexpr size_t kMaxCommitBytes = 512;

enum class MsgType : uint16_t {
    KeyTest = 1,
    KeyEvent = 2,
    FocusChanged = 3,
    ResetComposition = 4,
    Ping = 5,
    UpdateCaret = 6,
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

struct Response {
    uint32_t magic = kProtocolMagic;
    uint16_t version = kProtocolVersion;
    uint8_t consumed = 0;
    uint8_t isEnglish = 0;
    uint8_t isFullWidth = 0;
    uint8_t isCnPunct = 1;
    uint16_t preeditLen = 0;
    uint16_t commitLen = 0;
    uint16_t caretPos = 0;
    char preedit[kMaxPreeditBytes] = {};
    char commit[kMaxCommitBytes] = {};
};
#pragma pack(pop)

} // namespace liteime::ipc
