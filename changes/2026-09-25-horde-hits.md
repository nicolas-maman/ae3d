### Multiplayer: zombies hit and shoved, the same on every peer

- A zombie hit is a horde input beside a target change and a kill (#442):
  `nethorde.hit(horde, index, dx, dz, stun, now)` changes its velocity by
  (dx, dz) m/s and stuns it for `stun` seconds (whole ticks, one at least),
  on the host at once and on every client at the next tick, sent reliably
  like the others: 25 bytes (an event with bytes, `u32` tick, `u32`
  zombie, `f32` dx and dz, `u16` stun ticks). The change is kept as the
  32-bit float the wire carries, on the host too.
- The horde's velocity is each tick's scratch, so a shove is a velocity of
  its own on the few zombies hit. For the stun a zombie keeps its heading
  and its walk's phase, stands in the step (the others are still pushed
  off it), and its shove carries it after the step, fading in a straight
  line to nothing on the stun's last tick: v dt (n + 1) / 2, 1.07 m for
  4 m/s over half a second, measured to the nanometre. A second hit while
  stunned adds to what is left of the shove and keeps the longer stun; a
  kill's swap carries the last zombie's shove to its new index.
- The shoves are in the horde's hash and in a late joiner's state (32
  bytes each; the start's layout is version 2, 207 bytes of rules).
  `stunned(horde, index)` is the seconds of stun a zombie has left, what a
  game draws a stagger by; `shoved(horde)` how many are under a shove.
- `tests/test_net_horde.ae` hits sixty zombies over the run, eight a
  second, one of them twice a second while its last hit still holds it:
  0 divergences on client 2 and the late client (224 and 101 ticks
  checked, up to four zombies under a shove at once), every side's horde
  the host's to the bit at the end, and the hits cost a client 200 bytes a
  second of the horde's 780.
