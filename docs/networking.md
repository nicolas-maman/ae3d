# Networking

`ae3d.net` puts multiplayer in the engine (#413). A game marks what is networked, and the engine keeps it in step between one host and its clients. This page describes the first slice: transports, the handshake, and server-owned objects replicated and interpolated. The rest of #413 (client prediction, relevance, delta compression, the horde) is built on it.

## A session

```aether
import ae3d.net

session = net.host_tcp("0.0.0.0", 7777)          // or net.join_tcp("192.168.1.20", 7777)
car = engine.object(e, "Car", body)
net.networked(session, car)                       // the host sends it; a client draws it
net.attach(e, session)                            // stepped by the engine every frame
```

Both sides build the same scene and mark the same objects networked in the same order. That order is the net id, which is how a snapshot's object is found on the client. The host's scripts move the objects; a client runs none on them.

`examples/net_cars.ae` is the whole shape: eight cars round a ring.

```bash
./build.sh examples/net_cars.ae
./build/net_cars                              # host on 127.0.0.1:7777
AE3D_NET=join:127.0.0.1 ./build/net_cars      # a client
AE3D_NET_BIND=0.0.0.0 ./build/net_cars        # host for the network
```

## Transports

A transport moves messages between the host and each peer. A message is either reliable (never lost, in order: the handshake, and later events) or unreliable (a snapshot, which the next one supersedes, so waiting for a lost one would only make it later).

| Transport | Made with | For |
|---|---|---|
| Loopback | `hub_new`, `host_loopback(hub)`, `join_loopback(hub)` | Tests, and play as host and clients in one process. `hub_set_conditions(hub, latency, jitter, loss)` puts a real link's conditions on it, deterministically. |
| TCP | `host_tcp(address, port)`, `join_tcp(host, port)` | A real network today, over `std.tcp`. The stream is reliable and ordered, so both kinds of message travel the same way. |
| UDP | when Aether has datagrams ([aether#2201](https://github.com/aether-lang-dev/aether/issues/2201)) | A real network properly: a lost snapshot is not waited for. |
| Steam | SteamNetworkingSockets behind the same calls | Lobbies and NAT traversal, when a game wants them. |

## The protocol

| Message | Direction | Bytes |
|---|---|---|
| hello | client to host, reliable | `u8 1`, `u32 protocol` |
| welcome | host to client, reliable | `u8 2`, `u32 client id`, `u32 tick rate` |
| snapshot | host to clients, unreliable | `u8 3`, `u32 tick`, `f32 host time`, `u16 count`, then per object `u32 id`, `f32 x y z`, `f32 qx qy qz qw` |

Over TCP each message is framed by a `u32` length.

## Time and interpolation

The host sends a snapshot every network tick: 30 a second by default (`set_tick_rate`), stamped with its clock.

A client estimates the host's clock from the least-delayed snapshot it has seen: a snapshot can arrive later than its stamp, never earlier. It draws every object at that clock less the interpolation delay (`set_interpolation_delay`, 100 ms by default), between the two snapshots around that moment. `view_time(session, now)` is that moment, in the host's time. It is what lag compensation will rewind to.

The delay has to cover a snapshot interval and the link's jitter. When snapshots stop coming, the objects hold at the newest one.

## What it is held to

`tests/test_net.ae` runs a host and a client in one process. Sixteen objects go round a circle of 10 m at a radian a second, and the client is measured against where the host had each object at the client's `view_time`:

| Link | Worst error | The geometry's bound |
|---|---|---|
| perfect loopback | 1.389 mm | the chord between two snapshots, 10 (1 − cos 1/60) = 1.39 mm |
| 100 ms, 20 ms jitter, 2% loss | 5.555 mm | a lost snapshot doubles the interval, 10 (1 − cos 1/30) = 5.55 mm |
| TCP on this machine | 2.8 mm | real-time steps, ticks not aligned to them |

Sixteen objects cost a client 15.1 KB a second (32 bytes an object a snapshot, before delta compression).

## Next

As #413 lays out: input commands and client prediction with reconciliation, relevance and a per-client budget, delta compression against the acknowledged snapshot, spawning and despawning, reliable events and RPCs, UDP, the editor's host-and-clients play, and a horde that is simulated on every client instead of sent.
