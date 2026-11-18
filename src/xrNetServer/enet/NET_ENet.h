#pragma once

// Internal helpers shared by the ENet client and server.
// Only included from xrNetServer/enet/*.cpp, so enet.h never leaks into xrGame.

#include <enet/enet.h>

namespace xrENet
{
// Bump when the handshake or system messages change, so mismatched builds
// refuse each other with a clear message instead of misbehaving.
constexpr u32 PROTOCOL_VERSION = 1;

enum Channel : enet_uint8
{
    CHANNEL_RELIABLE = 0, // reliable, ordered
    CHANNEL_UNRELIABLE = 1, // unreliable (sequenced or not)
    CHANNEL_PRIORITY = 2, // high priority traffic, kept apart from bulk data
    CHANNEL_COUNT
};

// Same signature as MSYS_PING / MSYS_CONFIG (NET_Messages.h), so raw system
// messages are recognised before multipacket decoding.
constexpr u32 SIGN1 = 0x12071980;
constexpr u32 SIGN2 = 0x26111975;

enum SystemKind : u32
{
    SYS_HANDSHAKE = 0x58520001, // client -> server, first message after connect
    SYS_ACCEPT = 0x58520002, // server -> client, handshake accepted
    SYS_TERMINATE = 0x58520003, // server -> client, reason text, followed by disconnect
};

enum RejectReason : u32
{
    REASON_NONE = 0,
    REASON_KICKED,
    REASON_BANNED,
    REASON_WRONG_PASSWORD,
    REASON_SERVER_FULL,
    REASON_VERSION_MISMATCH,
    REASON_IP_DENIED,
    REASON_SHUTDOWN,
};

#pragma pack(push, 1)
struct SystemHeader
{
    u32 sign1;
    u32 sign2;
    u32 kind;
};

struct Handshake
{
    SystemHeader header;
    u32 version;
    u32 process_id;
    char name[64];
    char user_pass[64];
    char server_password[64];
};

struct Accept
{
    SystemHeader header;
    u32 version;
};

struct Terminate
{
    SystemHeader header;
    u32 reason; // RejectReason
    char text[256];
};
#pragma pack(pop)

inline SystemHeader MakeHeader(SystemKind kind) { return { SIGN1, SIGN2, kind }; }

// Returns the system message kind if data is one of ours, 0 otherwise.
inline u32 GetSystemKind(const void* data, size_t size)
{
    if (size < sizeof(SystemHeader))
        return 0;
    const auto* h = static_cast<const SystemHeader*>(data);
    if (h->sign1 != SIGN1 || h->sign2 != SIGN2)
        return 0;
    switch (h->kind)
    {
    case SYS_HANDSHAKE: return size == sizeof(Handshake) ? h->kind : 0;
    case SYS_ACCEPT: return size == sizeof(Accept) ? h->kind : 0;
    case SYS_TERMINATE: return size == sizeof(Terminate) ? h->kind : 0;
    default: return 0;
    }
}

// Reference counted enet_initialize / enet_deinitialize.
bool Initialize();
void Deinitialize();

// Translate DPNSEND_* style flags (see net_flags()) to an ENet channel and packet flags.
void TranslateFlags(u32 flags, enet_uint8& channel, enet_uint32& packet_flags);

// Send a raw (non-multipacket) buffer. Caller must hold the host lock.
bool SendRaw(ENetPeer* peer, const void* data, size_t size, u32 flags);

pcstr ReasonToString(u32 reason);
} // namespace xrENet
