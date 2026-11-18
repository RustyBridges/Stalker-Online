#pragma once
#pragma pack(push, 1)

#define DPNSEND_IMMEDIATELLY 0x0100

#if !defined(XR_PLATFORM_WINDOWS)
// Same values as in sdk/include/DPlay/dplay8.h, which is only included on Windows.
// Non-DirectPlay network backends translate these flags to their own.
#define DPNSEND_NOCOMPLETE    0x0002
#define DPNSEND_GUARANTEED    0x0008
#define DPNSEND_NONSEQUENTIAL 0x0010
#define DPNSEND_PRIORITY_HIGH 0x0080
#endif

IC u32 net_flags(
    bool bReliable = false, bool bSequental = true, bool bHighPriority = false, bool bSendImmediatelly = false)
{
    return (bReliable ? DPNSEND_GUARANTEED : DPNSEND_NOCOMPLETE) | (bSequental ? 0 : DPNSEND_NONSEQUENTIAL) |
        (bHighPriority ? DPNSEND_PRIORITY_HIGH : 0) | (bSendImmediatelly ? DPNSEND_IMMEDIATELLY : 0);
}

struct MSYS_CONFIG
{
    u32 sign1; // 0x12071980;
    u32 sign2; // 0x26111975;
};

struct MSYS_PING
{
    u32 sign1; // 0x12071980;
    u32 sign2; // 0x26111975;
    u32 dwTime_ClientSend;
    u32 dwTime_Server;
    u32 dwTime_ClientReceive;
};

#pragma pack(pop)
