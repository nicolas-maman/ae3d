# Networking

`ae3d.net` puts multiplayer in the engine (#413). A game marks what is networked, and the engine keeps it in step between one host and its clients. This page describes what is built: transports, the handshake, server-owned objects replicated and interpolated, snapshots sent as deltas against what each client has acknowledged, relevance, and players, each moved by its client's commands with prediction and reconciliation. The rest of #413 (UDP, events, the horde) is built on it.

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
| snapshot | host to each client, unreliable | `u8 3`, `u32 tick`, `f32 host time`, `u32 ack` (the client's newest command the host has applied), `u32 baseline` (the snapshot this one is a delta against; 0 for none), `u16 objects`, `u16 changed`, then per changed object `u16 index`, `f32 x y z`, `f32 qx qy qz qw`, `u8 character`, and for a character `f32 vy`, `u8 grounded` |
| input | client to host, unreliable | `u8 4`, `u32 first seq`, `u8 count`, then per command `f32 walk x z`, `f32 jump` |
| spawn | host to client, reliable | `u8 5`, `u32 net id`, `u32 owner` |
| ack | client to host, unreliable | `u8 6`, `u32 tick`: a snapshot the client has, so the next can be a delta against it |

Over TCP each message is framed by a `u32` length.

## Time and interpolation

The host sends a snapshot every network tick: 30 a second by default (`set_tick_rate`), stamped with its clock.

A client estimates the host's clock from the least-delayed snapshot it has seen: a snapshot can arrive later than its stamp, never earlier. It draws every object at that clock less the interpolation delay (`set_interpolation_delay`, 100 ms by default), between the two snapshots around that moment. `view_time(session, now)` is that moment, in the host's time. It is what lag compensation will rewind to.

The delay has to cover a snapshot interval and the link's jitter. When snapshots stop coming, the objects hold at the newest one.

## Deltas and relevance

A snapshot carries only what the client doesn't already have. Each is a **delta** against the newest snapshot the client has acknowledged: the objects that changed since then are listed, and everything else is as it was. The client keeps the last 32 snapshots it received. It rebuilds each new one from its baseline, keeps it, and acknowledges it (`ack`), and the host's next snapshot is a delta against that. A lost snapshot costs nothing: the host never deltas against one the client hasn't confirmed. An object that stands still is sent until the client's first acknowledgement comes back, and never again.

**Relevance.** `set_relevance(session, metres)` sends a client only what is within that distance of its player. What lies farther keeps the state the client was last told, so it costs nothing until it comes back within reach, and then it's sent as any change is. Its own player is always relevant. Without a radius, or without a player, everything is sent.

`objects_sent(session)` is how many object states the host has written, all clients together.

## Players

A player is a character controller ([physics.md](physics.md#characters)) owned by one client. The game says how one is made, the same on both sides:

```aether
make_player(context: ptr, client: int) -> *CharacterController {
    e = context as *Engine
    o = engine.object(e, "Player ${client}", body)
    behaviour.object_set_position(o, spawn_point(client))
    return physics.character_controller(o, 0.3, 1.8)
}

net.set_player_maker(session, make_player, e as ptr)
```

The host makes a player for every client it welcomes and tells every client about it (spawn); each client makes the players it is told of, its own among them (`my_player`). Then, every fixed step, a client gives its command:

```aether
net.player_input(session, walk, jump, now)        // walk in m/s, jump in m/s up
```

- **Prediction.** The command moves the client's own player at once, in the client's world, the way `physics.character_move` would on the host: no round trip before the player walks.
- **Commands to the host.** Each input message carries the newest four commands the host has not acknowledged, so a lost message costs nothing, and four lost in a row cost one correction. The host queues them and applies one a host step, in order, to its player for that client, so the player moves in the host's world at the pace it walked, whatever the link's jitter did to when the commands arrived; a queue grown past three commands catches up a command a step.
- **Reconciliation.** Every snapshot tells the client the newest of its commands the host has applied, and where that left its player (with its vertical speed and whether it stands). The client puts its player there and replays the commands the host has not applied yet. When both worlds agree, as they should, that moves nothing; `prediction_error(session)` is the most it ever moved.
- **Everyone else's.** The other players are drawn interpolated, like any networked object.
- **The host plays too.** `host_play(session, now)` gives the host a player of its own (client 0), told to every client like any other; on the host, `player_input` moves it directly, since the host's world is the authority. Without it the host is a dedicated server.

Networked scene objects are marked before hosting or joining, so their ids come first and the players' after them.

`examples/net_walk.ae` is players on a plaza: the host plays too, every client joins with a capsule of its own, W/A/S/D walk and space jumps, and the camera follows your own player.

```bash
./build.sh examples/net_walk.ae
./build/net_walk                              # host and a player, on 127.0.0.1:7777
AE3D_NET=join:127.0.0.1 ./build/net_walk      # a client
```

With `AE3D_NET_AUTOWALK=1` each walks five seconds round a circle by itself, and prints where every player is when it closes. A host and a client run side by side over TCP print the same positions for both players.

## What it is held to

`tests/test_net.ae` runs a host and a client in one process. Sixteen objects go round a circle of 10 m at a radian a second, and the client is measured against where the host had each object at the client's `view_time`:

| Link | Worst error | The geometry's bound |
|---|---|---|
| perfect loopback | 1.389 mm | the chord between two snapshots, 10 (1 − cos 1/60) = 1.39 mm |
| 100 ms, 20 ms jitter, 2% loss | 5.555 mm | a lost snapshot doubles the interval, 10 (1 − cos 1/30) = 5.55 mm |
| TCP on this machine | 2.8 mm | real-time steps, ticks not aligned to them |

Sixteen moving objects cost a client 14.9 KB a second (31 bytes an object a snapshot). With 48 more that stand still, over the lossy link, it costs 19.2 KB a second. The still ones are drawn exactly where they stand, and are sent only until the first acknowledgement is back: 1,232 object states in two seconds, where sending every object every snapshot would be 3,840.

`tests/test_players.ae` runs a host and two clients, each with a world of its own, over 100 ms latency, 20 ms jitter and 2% loss. The host plays and walks; client 1 walks, turns and jumps; client 2 walks:

| | Measured | Held to |
|---|---|---|
| a command moves the client's player the step it is given | 50 mm at 3 m/s | 49-51 mm |
| the most a reconciliation moved a player | 0.0004 mm | 1 cm |
| a client's own player at rest against the host's | 10⁻¹¹ mm | 1 mm |
| the other client's player at rest | 10⁻¹¹ mm | 1 cm |
| the other client's player while walking, against the host at view time | within 1 cm on 128 of 131 steps, 35 mm at worst | 1 cm on 95%, a step (50 mm) always |
| the host's own player at rest, drawn by a client | 10⁻¹¹ mm | 1 cm |
| a jump's peak, client against host | 0.9172 m and 0.9172 m | 1 cm |
| a client's commands (and its acknowledgements) | 3.08 KB a second | 4 KB |
| relevance 40 m: a crate among the players, one 200 m off | the near one where the host has it; the far one never sent | |

The walking error is under a centimetre except at three steps, where the host caught up a queue the link had let grow, applying two commands in one step. Applying commands as they arrived, instead of one a host step, made it 52.7 mm.

## Next

As #413 lays out: a per-client budget and priorities within it, quantised positions, spawning and despawning, reliable events and RPCs, UDP, the editor's host-and-clients play, and a horde that is simulated on every client instead of sent.
