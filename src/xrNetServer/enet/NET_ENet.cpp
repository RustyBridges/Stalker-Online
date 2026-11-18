#include "stdafx.h"

#include "NET_ENet.h"
#include "NET_Messages.h"

#include <atomic>
#include <mutex>

namespace xrENet
{
static std::mutex s_init_mutex;
static int s_init_count = 0;

bool Initialize()
{
    std::lock_guard lock(s_init_mutex);
    if (s_init_count == 0)
    {
        if (enet_initialize() != 0)
        {
            Msg("! ENet: initialization failed");
            return false;
        }
        Msg("* ENet %u.%u.%u initialized", ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH);
    }
    ++s_init_count;
    return true;
}

void Deinitialize()
{
    std::lock_guard lock(s_init_mutex);
    if (s_init_count > 0 && --s_init_count == 0)
        enet_deinitialize();
}

void TranslateFlags(u32 flags, enet_uint8& channel, enet_uint32& packet_flags)
{
    packet_flags = 0;
    if (flags & DPNSEND_GUARANTEED)
    {
        packet_flags |= ENET_PACKET_FLAG_RELIABLE;
        channel = CHANNEL_RELIABLE;
    }
    else
    {
        if (flags & DPNSEND_NONSEQUENTIAL)
            packet_flags |= ENET_PACKET_FLAG_UNSEQUENCED;
        channel = CHANNEL_UNRELIABLE;
    }

    if (flags & DPNSEND_PRIORITY_HIGH)
        channel = CHANNEL_PRIORITY;
}

bool SendRaw(ENetPeer* peer, const void* data, size_t size, u32 flags)
{
    if (!peer)
        return false;

    enet_uint8 channel;
    enet_uint32 packet_flags;
    TranslateFlags(flags, channel, packet_flags);

    ENetPacket* packet = enet_packet_create(data, size, packet_flags);
    if (!packet)
        return false;

    if (enet_peer_send(peer, channel, packet) != 0)
    {
        enet_packet_destroy(packet);
        return false;
    }
    return true;
}

pcstr ReasonToString(u32 reason)
{
    switch (reason)
    {
    case REASON_KICKED: return "st_kicked_by_server";
    case REASON_BANNED: return "Player banned by server!";
    case REASON_WRONG_PASSWORD: return "Wrong server password";
    case REASON_SERVER_FULL: return "Server is full";
    case REASON_VERSION_MISMATCH: return "Server runs a different game version";
    case REASON_IP_DENIED: return "Your IP does not present in server's subnet";
    case REASON_SHUTDOWN: return "Server shut down";
    default: return "Disconnected";
    }
}
} // namespace xrENet
