### Balance that steps

- A pushed `POWERED` figure steps to catch itself (#414): the capture
  point leaves the ground its feet cover, and a foot swings under it.
  - Pushed forward, the foot behind swings through.
  - Pushed sideways, the foot on that side steps out.
  - Pushed back, it doesn't step, since the reference's hips hardly extend.
  - A push past a stride and a half is left to the protective fall.
  - `set_stepping`, `stepping` and `steps_taken`.
- `tests/test_balance.ae`:
  - a nudge takes no step;
  - 270 N·s from behind fells a figure that can't step, and is caught in
    4 steps by one that can;
  - 180 N·s from the side is caught in one step;
  - a push onto the heels holds as it did.
  - `test_motion` still passes 25/25, its protected falls as soft as
    before.
