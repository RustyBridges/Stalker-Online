#include "stdafx.h"

#include "NET_Client.h"
#include "NET_Common.h"
#include "NET_Log.h"
#include "NET_Messages.h"
#include "NET_Server.h"
#include "NET_ENet.h"
#include "xrCore/Threading/Lock.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// ENet transport state. Every enet_* call on `host`/`peer` must hold `host_lock`.
struct IPureClient::ENetClientState
{
    ENetHost* host{};
    ENetPeer* peer{};
    std::mutex host_lock;
    std::thread thread;
    std::atomic<bool> running{};

    // Reason text from a SYS_TERMINATE message, reported when the disconnect arrives.
    u32 terminate_reason{};
    string256 terminate_text{};
};

#include "xrGameSpy/xrGameSpy_MainDefs.h"

static INetLog* pClNetLog = nullptr;

#ifdef CONFIG_PROFILE_LOCKS
INetQueue::INetQueue()
    : pcs(xr_new<Lock>(MUTEX_PROFILE_ID(INetQueue)))
#else
INetQueue::INetQueue()
    : pcs(xr_new<Lock>())
#endif
{
    unused.reserve(128);
    for (int i = 0; i < 16; i++)
        unused.push_back(xr_new<NET_Packet>());
}

INetQueue::~INetQueue()
{
    pcs->Enter();
    u32 it;
    for (it = 0; it < unused.size(); it++)
        xr_delete(unused[it]);
    for (it = 0; it < ready.size(); it++)
        xr_delete(ready[it]);
    pcs->Leave();
    xr_delete(pcs);
}

//static u32 LastTimeCreate = 0;

NET_Packet* INetQueue::Create()
{
    NET_Packet* P = nullptr;

    if (unused.empty()) {
        ready.push_back(xr_new<NET_Packet>());
        P = ready.back();
        //---------------------------------------------
        // LastTimeCreate = SDL_GetTicks();
        //---------------------------------------------
    } else {
        ready.push_back(unused.back());
        unused.pop_back();
        P = ready.back();
    }
    // pcs->Leave();
    return P;
}

NET_Packet* INetQueue::Create(const NET_Packet& _other)
{
    NET_Packet* P = nullptr;
    pcs->Enter();
    if (unused.empty()) {
        ready.push_back(xr_new<NET_Packet>());
        P = ready.back();
        //---------------------------------------------
        //        LastTimeCreate = SDL_GetTicks();
        //---------------------------------------------
    } else {
        ready.push_back(unused.back());
        unused.pop_back();
        P = ready.back();
    }
    CopyMemory(P, &_other, sizeof(NET_Packet));
    pcs->Leave();
    return P;
}

NET_Packet* INetQueue::Retreive()
{
    NET_Packet* P = nullptr;

    if (!ready.empty())
        P = ready.front();
    //---------------------------------------------
    else {
        /*
    u32 tmp_time = SDL_GetTicks() - 60000;
    u32 size = unused.size();
    if ((LastTimeCreate < tmp_time) && (size > 32))
    {
        xr_delete(unused.back());
        unused.pop_back();
    }
    */
    }

    return P;
}

void INetQueue::Release()
{
    VERIFY(!ready.empty());
    //---------------------------------------------
    // u32 tmp_time = SDL_GetTicks() - 60000;
    //u32 size = unused.size();
    ready.front()->B.count = 0;
    /*
   * if ((LastTimeCreate < tmp_time) && (size > 32))
  {
      xr_delete(ready.front());
  }
  else
      unused.push_back(ready.front());
      */

    ready.pop_front();
}

void INetQueue::LockQ()
{
    pcs->Enter();
}

void INetQueue::UnlockQ()
{
    pcs->Leave();
}

const u32 syncQueueSize = 512;
//const int syncSamples = 256;

class XRNETSERVER_API syncQueue {
    u32 table[syncQueueSize];
    u32 write;
    u32 count;

public:
    syncQueue() { clear(); }

    void push(u32 value)
    {
        table[write++] = value;
        if (write == syncQueueSize)
            write = 0;

        if (count <= syncQueueSize)
            count++;
    }

    u32* begin() { return table; }
    u32* end() { return table + count; }
    u32 size() const { return count; }

    void clear()
    {
        write = 0;
        count = 0;
    }
} net_DeltaArray;

//-------
XRNETSERVER_API Flags32 psNET_Flags = { 0 };
XRNETSERVER_API int psNET_ClientUpdate = 30; // FPS
XRNETSERVER_API int psNET_ClientPending = 2;
XRNETSERVER_API char psNET_Name[32] = "Player";
XRNETSERVER_API bool psNET_direct_connect = false;

/****************************************************************************
 *
 * DirectPlay8 Service Provider GUIDs
 *
 ****************************************************************************/

static HRESULT WINAPI Handler(PVOID pvUserContext,
    DWORD dwMessageType,
    PVOID pMessage)
{
    IPureClient* C = (IPureClient*)pvUserContext;
    return C->net_Handler(dwMessageType, pMessage);
}

//------------------------------------------------------------------------------

void IPureClient::_SendTo_LL(const void* data,
    u32 size,
    u32 flags,
    u32 timeout)
{
    IPureClient::SendTo_LL(const_cast<void*>(data), size, flags, timeout);
}

//------------------------------------------------------------------------------

void IPureClient::_Recieve(const void* data, u32 data_size, u32 /*param*/)
{
    MSYS_PING* cfg = (MSYS_PING*)data;
    net_Statistic.dwBytesReceived += data_size;

    if ((data_size >= 2 * sizeof(u32)) && (cfg->sign1 == 0x12071980) && (cfg->sign2 == 0x26111975)) {
        // Internal system message
        if (data_size == sizeof(MSYS_PING)) {
            // It is reverted(server) ping
            u32 time = TimerAsync(device_timer);
            u32 ping = time - (cfg->dwTime_ClientSend);
            u32 delta = cfg->dwTime_Server + ping / 2 - time;

            net_DeltaArray.push(delta);
            Sync_Average();
            return;
        }

        if (data_size == sizeof(MSYS_CONFIG)) {
            net_Connected = EnmConnectionCompleted;
            return;
        }
        Msg("! Unknown system message");
    }
    if (net_Connected == EnmConnectionCompleted) {
        // one of the messages - decompress it

        if (psNET_Flags.test(NETFLAG_LOG_CL_PACKETS)) {
            if (!pClNetLog)
                pClNetLog = xr_new<INetLog>("logs//net_cl_log.log", timeServer());

            if (pClNetLog)
                pClNetLog->LogData(timeServer(), const_cast<void*>(data), data_size,
                    true);
        }

        OnMessage(const_cast<void*>(data), data_size);
    }
}

//==============================================================================
#ifdef CONFIG_PROFILE_LOCKS
IPureClient::IPureClient(CTimer* timer)
    : net_Statistic(timer)
    , net_csEnumeration(
          xr_new<Lock>(MUTEX_PROFILE_ID(IPureClient::net_csEnumeration)))
#else
IPureClient::IPureClient(CTimer* timer)
    : net_Statistic(timer)
    , net_csEnumeration(xr_new<Lock>())
#endif
{
    m_enet = xr_new<ENetClientState>();
    device_timer = timer;
    net_TimeDelta_User = 0;
    net_Time_LastUpdate = 0;
    net_TimeDelta = 0;
    net_TimeDelta_Calculated = 0;

    pClNetLog = nullptr; // new INetLog("logs//net_cl_log.log", timeServer());
}

IPureClient::~IPureClient()
{
    ENet_StopHost();
    xr_delete(m_enet);
    xr_delete(pClNetLog);
    pClNetLog = nullptr;
    psNET_direct_connect = false;
    xr_delete(net_csEnumeration);
}

bool IPureClient::Connect(pcstr options)
{
    R_ASSERT(options);
    net_Disconnected = false;

    if (!psNET_direct_connect) {
        string256 server_name = "";
        // xr_strcpy(server_name,options);
        if (strchr(options, '/'))
            strncpy_s(server_name, options, strchr(options, '/') - options);
        else
            xr_strcpy(server_name, options);
        if (strchr(server_name, '/'))
            *strchr(server_name, '/') = 0;

        string64 password_str = "";
        if (strstr(options, "psw=")) {
            const char* PSW = strstr(options, "psw=") + 4;
            if (strchr(PSW, '/'))
                strncpy_s(password_str, PSW, strchr(PSW, '/') - PSW);
            else
                xr_strcpy(password_str, PSW);
        }

        string64 user_name_str = "";
        if (strstr(options, "name=")) {
            const char* NM = strstr(options, "name=") + 5;
            if (strchr(NM, '/'))
                strncpy_s(user_name_str, NM, strchr(NM, '/') - NM);
            else
                xr_strcpy(user_name_str, NM);
        }

        string64 user_pass = "";
        if (strstr(options, "pass=")) {
            const char* UP = strstr(options, "pass=") + 5;
            if (strchr(UP, '/'))
                strncpy_s(user_pass, UP, strchr(UP, '/') - UP);
            else
                xr_strcpy(user_pass, UP);
        }

        int psSV_Port = START_PORT_LAN_SV;
        if (strstr(options, "port=")) {
            string64 portstr;
            xr_strcpy(portstr, strstr(options, "port=") + 5);
            if (strchr(portstr, '/'))
                *strchr(portstr, '/') = 0;
            psSV_Port = atol(portstr);
            clamp(psSV_Port, int(START_PORT), int(END_PORT));
        }

        bool bPortWasSet = false;
        int psCL_Port = START_PORT_LAN_CL;
        if (strstr(options, "portcl=")) {
            string64 portstr;
            xr_strcpy(portstr, strstr(options, "portcl=") + 7);
            if (strchr(portstr, '/'))
                *strchr(portstr, '/') = 0;
            psCL_Port = atol(portstr);
            clamp(psCL_Port, int(START_PORT), int(END_PORT));
            bPortWasSet = true;
        }
        (void)bPortWasSet; // ENet picks a free local port by itself
        (void)psCL_Port;

        net_Connected = EnmConnectionWait;
        net_Syncronised = false;
        net_Disconnected = false;

        if (!ENet_Connect(server_name, u32(psSV_Port), password_str, user_name_str, user_pass))
        {
            net_Connected = EnmConnectionFails;
            return false;
        }
    } // psNET_direct_connect
    // Sync
    net_TimeDelta = 0;
    return true;
}

void IPureClient::Disconnect()
{
    ENet_StopHost();

    // Clean up Host _list_
    net_csEnumeration->Enter();

    // XXX: Were we supposed to do something here? Because this code currently does absolutely nothing
    #if 0
    for (u32 i = 0; i < net_Hosts.size(); i++) {
        HOST_NODE& N = net_Hosts[i];
    }
    #endif

    net_Hosts.clear();
    net_csEnumeration->Leave();

    net_Connected = EnmConnectionWait;
    net_Syncronised = false;
}

HRESULT IPureClient::net_Handler(u32 dwMessageType, PVOID pMessage)
{
    // HRESULT     hr = S_OK;

    return S_OK;
}

void IPureClient::OnMessage(void* data, u32 size)
{
    // One of the messages - decompress it
    net_Queue.LockQ();
    NET_Packet* P = net_Queue.Create();

    P->construct(data, size);
    P->timeReceive = timeServer_Async(); // TimerAsync(device_timer);

    u16 m_type;
    P->r_begin(m_type);
    net_Queue.UnlockQ();
}

void IPureClient::timeServer_Correct(u32 sv_time, u32 cl_time)
{
    u32 ping = net_Statistic.getPing();
    u32 delta = sv_time + ping / 2 - cl_time;
    net_DeltaArray.push(delta);
    Sync_Average();
}

void IPureClient::SendTo_LL(void* data, u32 size, u32 dwFlags, u32 dwTimeout)
{
    if (net_Disconnected)
        return;

    if (psNET_Flags.test(NETFLAG_LOG_CL_PACKETS)) {
        if (!pClNetLog)
            pClNetLog = xr_new<INetLog>("logs" DELIMITER "net_cl_log.log", timeServer());
        if (pClNetLog)
            pClNetLog->LogData(timeServer(), data, size);
    }

    net_Statistic.dwBytesSended += size;

    std::lock_guard lock(m_enet->host_lock);
    if (!m_enet->peer)
        return;
    if (!xrENet::SendRaw(m_enet->peer, data, size, dwFlags))
        Msg("! IPureClient : failed to send %u bytes", size);
    if (dwFlags & DPNSEND_IMMEDIATELLY)
        enet_host_flush(m_enet->host);
}

void IPureClient::Send(NET_Packet& packet, u32 dwFlags, u32 dwTimeout)
{
    SendPacket(packet.B.data, packet.B.count, dwFlags, dwTimeout);
}

void IPureClient::Flush_Send_Buffer()
{
    FlushSendBuffer(0);
}

bool IPureClient::net_HasBandwidth()
{
    u32 dwTime = TimeGlobal(device_timer);
    u32 dwInterval = 0;
    if (net_Disconnected)
        return false;

    if (psNET_ClientUpdate != 0)
        dwInterval = 1000 / psNET_ClientUpdate;
    if (psNET_Flags.test(NETFLAG_MINIMIZEUPDATES))
        dwInterval = 1000; // approx 3 times per second

    if (psNET_direct_connect) {
        if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval) {
            net_Time_LastUpdate = dwTime;
            return true;
        }
        return false;
    }
    if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval) {
        // check queue for "empty" state
        u32 dwPending = 0;

        if (dwPending > u32(psNET_ClientPending)) {
            net_Statistic.dwTimesBlocked++;
            return false;
        }

        UpdateStatistic();

        // ok
        net_Time_LastUpdate = dwTime;
        return true;
    }
    return false;
}

void IPureClient::UpdateStatistic()
{
    // Query network statistic for this client
}

static constexpr u32 syncSamples = 256;

void IPureClient::Sync_Thread()
{
    // Ported from the DirectPlay version: ping the server until enough
    // round trips have been measured to estimate the server time offset.
    MSYS_PING clPing;
    net_DeltaArray.clear();

    while (!net_Disconnected && !net_Syncronised)
    {
        clPing.sign1 = xrENet::SIGN1;
        clPing.sign2 = xrENet::SIGN2;
        clPing.dwTime_ClientSend = TimerAsync(device_timer);
        clPing.dwTime_Server = 0;
        clPing.dwTime_ClientReceive = 0;

        {
            std::lock_guard lock(m_enet->host_lock);
            if (!m_enet->peer)
                break;
            // Raw, not through the multipacket buffer: the server checks for pings before decoding.
            xrENet::SendRaw(m_enet->peer, &clPing, sizeof(clPing), net_flags(false, false, true));
            enet_host_flush(m_enet->host);
        }

        // Wait for the reply (handled in _Recieve, which pushes to net_DeltaArray)
        const u32 old_size = net_DeltaArray.size();
        const u32 timeBegin = TimerAsync(device_timer);
        while (net_DeltaArray.size() == old_size && TimerAsync(device_timer) - timeBegin < 5000 && !net_Disconnected)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (net_DeltaArray.size() >= syncSamples)
        {
            net_Syncronised = true;
            net_TimeDelta = net_TimeDelta_Calculated;
        }
    }
}

void IPureClient::Sync_Average()
{
    //***** Analyze results
    s64 summary_delta = 0;
    const s32 size = net_DeltaArray.size();
    if (size == 0)
        return;
    for (u32* I = net_DeltaArray.begin(); I != net_DeltaArray.end(); ++I)
        summary_delta += *reinterpret_cast<int*>(I);

    s64 frac = summary_delta % s64(size);
    if (frac < 0)
        frac = -frac;
    summary_delta /= s64(size);
    if (frac > s64(size / 2))
        summary_delta += (summary_delta < 0) ? -1 : 1;
    net_TimeDelta_Calculated = s32(summary_delta);
    net_TimeDelta = (net_TimeDelta * 5 + net_TimeDelta_Calculated) / 6;
}

void IPureClient::net_Syncronize()
{
    net_Syncronised = false;
    net_DeltaArray.clear();
    Threading::SpawnThread("network-time-sync", [this]
    {
        Sync_Thread();
    });
}

bool IPureClient::net_isDisconnected() const
{
    return net_Disconnected;
}

void IPureClient::ClearStatistic()
{
    net_Statistic.Clear();
}
bool IPureClient::net_IsSyncronised()
{
    return net_Syncronised;
}

IPureClient::HOST_NODE::HOST_NODE()
{
}

IPureClient::HOST_NODE::HOST_NODE(const HOST_NODE& rhs)
{
    dpSessionName = rhs.dpSessionName;
}

IPureClient::HOST_NODE::HOST_NODE(HOST_NODE&& rhs) noexcept
{
    dpSessionName.swap(rhs.dpSessionName);
}

IPureClient::HOST_NODE::~HOST_NODE() noexcept
{
}

bool IPureClient::GetServerAddress(ip_address& pAddress, u32* pPort)
{
    std::lock_guard lock(m_enet->host_lock);
    if (!m_enet->peer)
        return false;
    CopyMemory(&pAddress.m_data.data, &m_enet->peer->address.host, sizeof(pAddress.m_data.data));
    if (pPort)
        *pPort = m_enet->peer->address.port;
    return true;
}

//==============================================================================
// ENet transport
//==============================================================================

bool IPureClient::ENet_Connect(
    pcstr server_name, u32 port, pcstr server_password, pcstr user_name, pcstr user_pass)
{
    ENet_StopHost();
    if (!xrENet::Initialize())
        return false;

    pcstr host_name = (!server_name[0] || xr_stricmp(server_name, "localhost") == 0) ? "127.0.0.1" : server_name;

    ENetAddress address{};
    if (enet_address_set_host(&address, host_name) != 0)
    {
        Msg("! IPureClient : can't resolve server address '%s'", host_name);
        xrENet::Deinitialize();
        OnInvalidHost();
        return false;
    }
    address.port = static_cast<enet_uint16>(port);

    auto fail = [this]
    {
        if (m_enet->host)
            enet_host_destroy(m_enet->host);
        m_enet->host = nullptr;
        m_enet->peer = nullptr;
        xrENet::Deinitialize();
        return false;
    };

    m_enet->host = enet_host_create(nullptr, 1, xrENet::CHANNEL_COUNT, 0, 0);
    if (!m_enet->host)
    {
        Msg("! IPureClient : can't create network host");
        return fail();
    }

    Msg("* IPureClient : connecting to %s:%u", host_name, port);
    m_enet->peer = enet_host_connect(m_enet->host, &address, xrENet::CHANNEL_COUNT, 0);
    if (!m_enet->peer)
        return fail();

    // 1. Transport connection
    ENetEvent event;
    if (enet_host_service(m_enet->host, &event, 5000) <= 0 || event.type != ENET_EVENT_TYPE_CONNECT)
    {
        Msg("! IPureClient : no answer from %s:%u", host_name, port);
        enet_peer_reset(m_enet->peer);
        fail();
        OnInvalidHost();
        return false;
    }

    // 2. Handshake: who we are, and the server password
    xrENet::Handshake hs{};
    hs.header = xrENet::MakeHeader(xrENet::SYS_HANDSHAKE);
    hs.version = xrENet::PROTOCOL_VERSION;
    hs.process_id = static_cast<u32>(GetCurrentProcessId());
    strncpy_s(hs.name, user_name, sizeof(hs.name) - 1);
    strncpy_s(hs.user_pass, user_pass, sizeof(hs.user_pass) - 1);
    strncpy_s(hs.server_password, server_password, sizeof(hs.server_password) - 1);
    xrENet::SendRaw(m_enet->peer, &hs, sizeof(hs), DPNSEND_GUARANTEED);
    enet_host_flush(m_enet->host);

    // 3. Wait for accept / reject. Stop right after SYS_ACCEPT so that game
    //    messages queued behind it are left for the network thread.
    u32 reject_reason = xrENet::REASON_NONE;
    string256 reject_text = "";
    bool accepted = false, rejected = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!accepted && !rejected && std::chrono::steady_clock::now() < deadline)
    {
        if (enet_host_service(m_enet->host, &event, 50) <= 0)
            continue;

        if (event.type == ENET_EVENT_TYPE_RECEIVE)
        {
            const u32 kind = xrENet::GetSystemKind(event.packet->data, event.packet->dataLength);
            if (kind == xrENet::SYS_ACCEPT)
                accepted = true;
            else if (kind == xrENet::SYS_TERMINATE)
            {
                const auto* t = reinterpret_cast<const xrENet::Terminate*>(event.packet->data);
                reject_reason = t->reason;
                strncpy_s(reject_text, t->text, sizeof(t->text) - 1);
            }
            enet_packet_destroy(event.packet);
        }
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
        {
            rejected = true;
            if (reject_reason == xrENet::REASON_NONE)
                reject_reason = event.data;
        }
    }

    if (!accepted)
    {
        if (!rejected)
            Msg("! IPureClient : server did not answer the handshake");
        else
            Msg("! IPureClient : connection refused: %s",
                reject_text[0] ? reject_text : xrENet::ReasonToString(reject_reason));
        fail();
        switch (reject_reason)
        {
        case xrENet::REASON_WRONG_PASSWORD: OnInvalidPassword(); break;
        case xrENet::REASON_SERVER_FULL: OnSessionFull(); break;
        default: OnConnectRejected(); break;
        }
        return false;
    }

    Msg("* IPureClient : connected to %s:%u", host_name, port);
    m_enet->terminate_reason = xrENet::REASON_NONE;
    m_enet->terminate_text[0] = 0;
    m_enet->running = true;
    m_enet->thread = Threading::RunThread("network-client", &IPureClient::ENet_ThreadProc, this);
    return true;
}

void IPureClient::ENet_StopHost()
{
    if (!m_enet || !m_enet->host)
        return;

    m_enet->running = false;
    if (m_enet->thread.joinable())
    {
        if (m_enet->thread.get_id() == std::this_thread::get_id())
            m_enet->thread.detach(); // called from a network callback: the loop exits on its own
        else
            m_enet->thread.join();
    }

    std::lock_guard lock(m_enet->host_lock);
    if (m_enet->peer && m_enet->peer->state == ENET_PEER_STATE_CONNECTED)
    {
        enet_peer_disconnect(m_enet->peer, xrENet::REASON_NONE);
        enet_host_flush(m_enet->host);

        ENetEvent event;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < deadline && enet_host_service(m_enet->host, &event, 10) >= 0)
        {
            if (event.type == ENET_EVENT_TYPE_RECEIVE)
                enet_packet_destroy(event.packet);
            else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                break;
        }
    }
    enet_host_destroy(m_enet->host);
    m_enet->host = nullptr;
    m_enet->peer = nullptr;
    xrENet::Deinitialize();
}

void IPureClient::ENet_ThreadProc()
{
    struct Event
    {
        ENetEventType type;
        u32 data;
        xr_vector<u8> payload;
    };
    xr_vector<Event> events;

    while (m_enet->running)
    {
        {
            std::lock_guard lock(m_enet->host_lock);
            if (!m_enet->host)
                break;
            ENetEvent event;
            while (enet_host_service(m_enet->host, &event, 0) > 0)
            {
                Event& e = events.emplace_back();
                e.type = event.type;
                e.data = event.data;
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    e.payload.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                }
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                    m_enet->peer = nullptr;
            }
        }

        for (Event& e : events)
        {
            if (e.type == ENET_EVENT_TYPE_RECEIVE)
            {
                if (xrENet::GetSystemKind(e.payload.data(), e.payload.size()) == xrENet::SYS_TERMINATE)
                {
                    const auto* t = reinterpret_cast<const xrENet::Terminate*>(e.payload.data());
                    m_enet->terminate_reason = t->reason;
                    strncpy_s(m_enet->terminate_text, t->text, sizeof(t->text) - 1);
                }
                else
                    RecievePacket(e.payload.data(), static_cast<u32>(e.payload.size()));
            }
            else if (e.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                ENet_HandleTerminate(m_enet->terminate_reason ? m_enet->terminate_reason : e.data, m_enet->terminate_text);
                m_enet->running = false;
            }
        }

        if (events.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        events.clear();
    }
}

void IPureClient::ENet_HandleTerminate(u32 reason, pcstr text)
{
    // Same as DPN_MSGID_TERMINATE_SESSION in the DirectPlay version.
    net_Disconnected = true;
    pcstr message = text && text[0] ? text : xrENet::ReasonToString(reason);
    Msg("- IPureClient : session terminated: %s", message);
    OnSessionTerminate(message);
}
