# Portable network backend (replacing DirectPlay)

Status: design draft for the `stalker-online` fork. Nothing here is implemented yet.

## Goal

Make X-Ray multiplayer work on Linux (and every other non-Windows platform),
as the foundation for later co-op / persistent-world work.

First milestone: **two instances of the game on one Linux machine play a
Deathmatch round on a stock Call of Pripyat MP map.**

## Where things stand today

### Single player never touches the network layer

Single player runs a server and a client in the same process
(`psNET_direct_connect`). The client hands messages straight to the server
object, e.g. `CLevel::Send` in `src/xrGame/Level_network.cpp`:

```cpp
if (psNET_direct_connect)
{
    ClientID _clid;
    _clid.set(1);
    Server->OnMessage(P, _clid);
}
```

That is why single player works on Linux even though networking does not.

### Multiplayer goes through `xrNetServer`, which is stubbed out on Linux

`src/xrNetServer/CMakeLists.txt` (the build used on Linux) compiles
`empty/NET_Client.cpp` and `empty/NET_Server.cpp` instead of the real
`NET_Client.cpp` / `NET_Server.cpp`, which are built on Microsoft DirectPlay 8
(`IDirectPlay8Client`, `IDirectPlay8Server`). DirectPlay is Windows-only and
long deprecated.

`net_flags()` in `src/xrNetServer/NET_Messages.h` also returns `0` on every
non-Windows platform, so reliability/ordering information is thrown away
before it reaches the transport.

### The transport boundary is small

Everything above `xrNetServer` (xrGame's `xrServer`, `CLevel`, game modes,
entity replication) talks to two classes:

| Class | Transport-specific parts |
| --- | --- |
| `IPureClient` | `Connect(options)`, `Disconnect()`, `SendTo_LL()` (raw send), `net_Handler()` feeding `MultipacketReciever::RecievePacket()`, host enumeration, `GetServerAddress()` |
| `IPureServer` | `Connect(session, descr)` (= start listening), `Disconnect()`, `SendTo_LL(ClientID, …)`, `net_Handler()` handling connect / disconnect / receive, `GetClientAddress()`, `DisconnectClient()`, ban/IP-filter checks |

Packet batching (`MultipacketSender` / `MultipacketReciever`), compression
(`NET_Compressor`), time sync pings (`MSYS_PING`), client bookkeeping
(`PlayersMonitor`), bans and statistics are already transport-independent and
can be reused unchanged.

The DirectPlay server callback maps cleanly onto a modern UDP library:

| DirectPlay event (`NET_Server.cpp`) | What it does | New backend equivalent |
| --- | --- | --- |
| `DPN_MSGID_INDICATE_CONNECT` | ban / IP-filter / password check, can reject | handshake message after transport connect; reject → disconnect with reason |
| `DPN_MSGID_CREATE_PLAYER` | reads `SClientConnectData`, calls `new_client()` | same, once the handshake is accepted |
| `DPN_MSGID_DESTROY_PLAYER` | `OnCL_Disconnected()` + `client_Destroy()` | transport disconnect / timeout event |
| `DPN_MSGID_RECEIVE` | answers `MSYS_PING`, else `RecievePacket(data, size, sender)` | receive event, same code |
| `DPN_MSGID_ENUM_HOSTS_*` | LAN server browser | out of scope at first; connect by address |

## Choice of library: ENet

Candidates that are packaged in nixpkgs (checked: `enet` 1.3.18,
`gamenetworkingsockets` 1.6.1):

- **ENet**: small C library on UDP with reliable and unreliable delivery and
  ordered channels. This is almost exactly DirectPlay's feature set
  (guaranteed vs. "no complete", sequential vs. non-sequential), so
  `net_flags()` maps one-to-one. Easy to vendor as a git submodule like the
  engine's other `Externals/`, which matters for the Windows build and for
  getting it accepted upstream.
- **GameNetworkingSockets** (Valve): more features (encryption, NAT traversal
  through Steam relays), but much heavier (protobuf, OpenSSL) and its
  connection model is further from DirectPlay's.

Recommendation: **ENet** for the first version. Encryption and NAT traversal
can come later and are not needed for LAN play or a dedicated server with an
open port.

### Flag mapping

`net_flags(bReliable, bSequental, bHighPriority, bSendImmediatelly)` gets real
bit values on all platforms (the DirectPlay values can stay as the constants
so Windows is unaffected), and the ENet backend translates them:

| X-Ray intent | ENet |
| --- | --- |
| reliable (`DPNSEND_GUARANTEED`) | `ENET_PACKET_FLAG_RELIABLE` on the ordered channel |
| unreliable, sequential | no flags (ENet's default unreliable-sequenced) |
| unreliable, non-sequential | `ENET_PACKET_FLAG_UNSEQUENCED` |
| high priority | own channel, so it is not queued behind bulk data |
| send immediately | `enet_host_flush()` after queuing |

## Implementation outline

New directory `src/xrNetServer/enet/` with `NET_Client.cpp/.h` and
`NET_Server.cpp/.h` implementing the same `IPureClient` / `IPureServer`
interface as `empty/`. A CMake option picks the backend:

```cmake
set(XRAY_NET_BACKEND "enet" CACHE STRING "Network backend: enet or empty")
```

DirectPlay stays the default on Windows until the ENet backend has proven
itself there too.

### Threading

DirectPlay delivered events on its own worker threads, and the engine is
written for that: receives go into the locked `INetQueue`, and server
callbacks take the `PlayersMonitor` lock. ENet instead has to be pumped with
`enet_host_service()` and is not thread-safe. Plan:

- one network thread per host that calls `enet_host_service()` in a loop and
  dispatches events exactly as `net_Handler()` does today;
- every ENet call (including sends from game threads) goes through one mutex
  owned by that host;
- `SendTo_LL` only queues the packet; the network thread or an explicit flush
  sends it.

### Connection handshake

ENet's connect carries only a 32-bit value, so after the transport connects,
the client's first message is a handshake packet containing
`SClientConnectData` (name, password, process id) plus a protocol version. The
server runs the existing ban, IP-filter and password checks on it, then calls
`new_client()` or disconnects the peer with a reason code that the client turns
into `OnInvalidPassword()` / `OnSessionFull()` / `OnConnectRejected()`.

`ClientID` values are assigned by the server from a counter and stored in
`ENetPeer::data`.

### Listen server

When a player hosts, their own client also connects to their server over
loopback (`127.0.0.1`), exactly as it did through DirectPlay. Nothing special
is needed.

### Server browser

The original browser used DirectPlay host enumeration and GameSpy, and the
GameSpy master servers have been shut down. For the first version, join by
address from the console:

```
start server(mp_pool/dm/maxplayers=4) client(localhost/name=host)
start client(192.168.1.20/name=player2)
```

(Sketch of the existing `start` console command; the exact option syntax and
map names still have to be confirmed against `CCC_Start` and the installed MP
maps when testing.)

A LAN broadcast or a small master-server list can come later.

## Work plan

1. **Flags on all platforms.** Give `net_flags()` real values outside Windows.
   No behaviour change yet. (Small, could go upstream on its own.)
2. **ENet as a dependency.** Add it as a git submodule under `Externals/` (or
   use the system package where available) and add `enet` to the Nix flake.
3. **ENet backend.** Server first (listen, accept, receive, send, disconnect),
   then client, then time sync and statistics.
4. **Milestone test.** Start a dedicated server and a client on one machine
   and play a Deathmatch round on an MP map. Fix whatever breaks above the
   transport: the MP code has never run on Linux or 64-bit Linux builds, so
   expect issues in xrGame (struct packing, `sizeof(long)`, GameSpy auth
   checks, which may need `sv_disable_auth_check`-style switches).
5. **Headless dedicated server on Linux** (`-dedicated`), then a NixOS module
   for running it as a service.
6. **Co-op research** (separate design): shared A-Life world, multiple actors,
   quest and save ownership. See the next section.

## Beyond Deathmatch: co-op

Notes for the later design, not part of this backend work:

- The single-player game is already server-authoritative: A-Life
  (`alife_simulator.cpp`) runs on the server side. That is the main reason
  co-op is feasible at all.
- The hard parts are above the network layer: Lua scripts assume one actor
  (`db.actor`), objects go online/offline by distance from that one actor, and
  dialogs, the PDA, story progress, trading and saving all assume a single
  player.
- A realistic first co-op target is free roam with the story disabled: shared
  A-Life, mutants, anomalies, artefacts and trading. Story co-op later, if
  ever.

## Before writing code

Check OpenXRay's issue tracker and Discord for existing plans to replace
DirectPlay, so this work can line up with upstream instead of diverging.
