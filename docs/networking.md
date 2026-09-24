# Networking

`ae3d.net` puts multiplayer in the engine (#413). A game marks what is networked, and the engine keeps it in step between one host and its clients. This page describes what is built: transports, the handshake, server-owned objects replicated and interpolated, snapshots sent as deltas against what each client has acknowledged, relevance, players, each moved by its client's commands with prediction and reconciliation, events, and objects the host creates and destroys while the game runs. The rest of #413 (UDP, the horde) is built on it.

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

A transport moves messages between the host and each peer. A message is either reliable (never lost, in order: the handshake, events, objects created and destroyed) or unreliable (a snapshot, which the next one supersedes, so waiting for a lost one would only make it later).

| Transport | Made with | For |
|---|---|---|
| Loopback | `hub_new`, `host_loopback(hub)`, `join_loopback(hub)` | Tests, and play as host and clients in one process. `hub_set_conditions(hub, latency, jitter, loss)` puts a real link's conditions on it, deterministically. |
| TCP | `host_tcp(address, port)`, `join_tcp(host, port)` | A real network today, over `std.tcp`. The stream is reliable and ordered, so both kinds of message travel the same way. |
| UDP | when Aether has datagrams ([aether#2201](https://github.com/aether-lang-dev/aether/issues/2201)) | A real network properly: a lost snapshot is not waited for. |
| Steam | SteamNetworkingSockets behind the same calls | Lobbies and NAT traversal, when a game wants them. |

## The protocol

| Message | Direction | Bytes |
|---|---|---|
| hello | client to host, reliable | `u8 1`, `u32 protocol` (3) |
| welcome | host to client, reliable | `u8 2`, `u32 client id`, `u32 tick rate` |
| snapshot | host to each client, unreliable | `u8 3`, `u32 tick`, `f32 host time`, `u32 ack` (the client's newest command the host has applied), `u32 baseline` (the snapshot this one is a delta against; 0 for none), `u16 objects`, `u16 changed`, then per changed object `u16 index`, `f32 x y z`, `f32 qx qy qz qw`, `u8 character`, and for a character `f32 vy`, `u8 grounded` |
| input | client to host, unreliable | `u8 4`, `u32 first seq`, `u8 count`, then per command `f32 walk x z`, `f32 jump` |
| spawn | host to client, reliable | `u8 5`, `u32 net id`, `u32 owner` |
| ack | client to host, unreliable | `u8 6`, `u32 tick`: a snapshot the client has, so the next can be a delta against it |
| event | either way, reliable | `u8 7`, `u16 event`, `u8 count`, then `count` `f32` numbers |
| create | host to client, reliable | `u8 8`, `u32 net id`, `u16 kind`, `f32 host time`, `f32 x y z`, `f32 qx qy qz qw` |
| destroy | host to client, reliable | `u8 9`, `u32 net id`, `f32 host time`, `f32 x y z`, `f32 qx qy qz qw`: when it went, and where it was |

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

## Events

An event is a game's own message: a horn honked, a hit, a door opened. Both sides register the same events in the same order, each with a handler:

```aether
honked(context: ptr, call: *NetCall) {
    game = context as *Game
    horn(game, call.sender, call.a)               // who honked, and how loud
}

net.on_event(session, "honk", honked, game as ptr)
```

and either side sends one:

```aether
net.send_event(session, "honk", net.HOST, now, 0.8)            // a client, to the host
net.send_event(session, "hit", net.EVERYONE, now, id as float, 25.0)   // the host, to every client
net.send_event(session, "whisper", 2, now, 1.0)                // the host, to client 2
```

- **Reliable and in order.** An event travels the reliable way, so it arrives once, and in order with everything else reliable the same side sent the same peer. An event the host sends after creating an object arrives after the object is there.
- **Four numbers.** An event carries up to four numbers (an id, an amount, a place), 32-bit floats on the wire, and only as many as the last one that isn't 0: a honk with one number is 8 bytes, with none 4. A handler reads them as `call.a` to `call.d`, 0 where the sender gave fewer.
- **An id on the wire, not the name.** An event is its order among the registered: 2 bytes. That's why both sides register in the same order, as they mark the same objects networked.
- **Who sent it.** `call.sender` is the client's id on the host, and `net.HOST` (0) on a client. A client sends only to the host; the host sends to one client or to `net.EVERYONE` it has welcomed, never to itself (its game calls its own code). A client that wants to reach the others sends to the host, which passes it on.
- **When.** A handler is called in the step the event arrives. `send_event` is false when there is no such event or no one to send it to yet: a client before its welcome.

## Spawning

The scene's networked objects are known by the order both sides mark them. An object the host creates while the game runs, a rocket or a car driven in, is known by the id the host gives it (the next after every id so far) and by its kind. Both sides register the same kinds in the same order, each with a maker and an unmaker:

```aether
make_rocket(context: ptr, id: int) -> *GameObject {
    e = context as *Engine
    return engine.object(e, "Rocket ${id}", rocket_model())
}

unmake_rocket(context: ptr, o: *GameObject) {
    engine.destroy(context as *Engine, o)
}

net.object_kind(session, "rocket", make_rocket, unmake_rocket, e as ptr)
```

The host creates one, moves it as it moves anything, and destroys it:

```aether
rocket = net.create_object(session, "rocket", muzzle, aim, now)
...
net.destroy_object(session, rocket, now)
```

- **Create.** The host makes the object with the kind's maker, places it, networks it, and tells every client its id, its kind and where it is. Each client makes its own with the same maker and places it the same. From the next tick the snapshots carry it like any object. A client keeps where it was made as its first sample, so it is drawn from there to wherever the snapshots take it. It exists the moment the word arrives, so an event that names it (`net.id_of`, `net.object_of`) finds it, and it waits at that first place until the client's view reaches the moment it was made.
- **Destroy.** The host tells every client when the object went and where it was then, and lets its own go. A client draws the object to that place and lets it go when its view reaches that moment, so the object lives the same stretch of the host's time on every side. Letting it go when the word arrived would cut the last 100 ms or so of every rocket's flight. A snapshot can't say an object is gone (one it leaves out is as it was), so a sample from after that moment is the old state carried on, and the client drops it.
- **Late join.** A client the host welcomes is told every created object still alive, and none destroyed before it came.
- **Ids aren't given again.** A destroyed object's id stays empty, so a snapshot or a delta that still names it, against an older baseline, finds nothing there, and a later object is never read as a change against the old one's state. A session creates up to 65,535 objects over its life, the limit of the snapshot's `u16` index.
- **Letting go.** The unmaker is the game's (`engine.destroy`, as a game does); `net_free` calls it for every created object still alive. A null unmaker leaves the object to the game.

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

`tests/test_net_events.ae` runs a host and two clients, and a third that joins 2.67 s in, each with a world of its own, over 100 ms latency, 20 ms jitter and 2% loss. The clients honk to the host, the host sends scores to everyone and whispers to client 2. It fires nine rockets on an arc, a quarter second apart, each flying 0.9 s, and drives two cars round a circle, the first gone after a second:

| | Measured | Held to |
|---|---|---|
| events: 192 honks, 80 scores to each client, 24 whispers | every one once, in order, from its sender, with its numbers; client 1 had none of client 2's whispers | all |
| an event, sent to handled | 133 ms at most | a one-way trip, its jitter and a step: 137 ms |
| an object, created on the host to made on a client | 133 ms at most | 137 ms |
| a created object drawn against where the host had it at the client's view time | within 6 mm on 1,539 of 1,546 object-steps, 12.5 mm at worst | 6 mm on 99%, 13 mm always |
| destroyed on the host to gone on a client | 217 ms at most, never drawn past its moment | the view's lag, 200 ms, and a step |
| the late client | told the four objects alive when it joined, none of the seven gone before; the scores from its welcome on, in order | all |
| a client's 132 honks and its acknowledgements | 0.5 KB a second over the run (a honk with three numbers is 16 bytes) | 1 KB |
| the host, to three clients | 7.3 KB a second | 10 KB |

The drawing error is the geometry's, as in `tests/test_net.ae`: a rocket's arc is off the chord between two snapshots by g dt²/8, 1.36 mm at 30 a second and 5.45 mm across a lost one; a car's circle 1.39 and 5.55 mm. The seven steps over 6 mm are two snapshots lost in a row, 10 (1 − cos 1/20) = 12.5 mm. Over TCP on this machine, 20 honks reach the host once and in order, and a rocket is made on the client and goes when the host destroys it.

## Next

As #413 lays out: a per-client budget and priorities within it, quantised positions, UDP (with a reliable channel of its own for events and created objects, which TCP and the loopback give for free), a destroyed object's id given again once every client has a snapshot without it, the editor's host-and-clients play, and a horde that is simulated on every client instead of sent.
