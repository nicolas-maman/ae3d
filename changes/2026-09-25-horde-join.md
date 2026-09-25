### Multiplayer: a late joiner's horde quantised, every peer snapped to it

- A client that joins a networked horde late is sent its state quantised
  (#443): x and z on a 24-bit grid over the ground (7 µm across a 120 m
  street, a millimetre across 16 km), the heading in 16 bits of a turn and
  the walk's phase in 16 bits: 10 bytes a zombie where the doubles were
  32. 3,000 zombies are 30 KB where they were 95 KB, half a million
  4.8 MB where they were 16 MB. The velocity is a tick's scratch, not
  state; the shoves under way are few and go as their doubles. The
  start's layout is version 3.
- The joiner has to hold exactly what every other peer holds, so as the
  host sends a state it snaps its own horde to the quantised values and
  tells every client to at the next tick, an input like the others (8
  bytes a client). Packing an unpacked value gives the same steps, so a
  snap is exact, the same on every peer, and idempotent. A resync is
  quantised and snapped the same way.
- Measured against the other way, the joiner taking the quantised state
  as it is and the next hash check putting it back (`set_join_snap(horde,
  false)`, kept for the measurement): 3,000 zombies, a client joining 2 s
  in over 100 ms latency, 20 ms jitter and 2% loss, the seals hashed once
  a second. Snapping, the joiner holds the host's horde from its state on,
  267 ms after it joined, after 30,319 bytes, with no frame off, no
  divergence and no resync, and the client already there untouched. Not
  snapping, it holds it 1,083 ms after it joined, after 126,947 bytes
  (what puts it back is the doubles: a quantised resync would be off
  again), 37 frames off, 1 divergence and 1 resync.
- `tests/test_net_horde.ae` asserts the snap: the late client's state
  under 10.1 bytes a zombie (10.0 measured, 30,159 bytes for 2,980 zombies
  and four shoves), its horde the host's at every tick it steps, the
  nudged client's resync snapping the others with 0 divergences, both
  ways measured side by side, and over TCP a joiner's 30 KB state on the
  stream.
