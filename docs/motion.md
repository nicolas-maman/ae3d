# Natural motion

`ae3d.motion` is an active ragdoll (#414): a figure whose animation is played by its muscles rather than pasted onto its bones. It's what GTA IV and V get from NaturalMotion's Euphoria. Hit it, and it gives where it was hit and comes back; hit it harder than it can take, and it falls. It's an option on the engine's own motion system, a component on any ragdoll that wears a skinned figure (`physics.ragdoll_dress`), and off unless asked for.

It has muscles, balance, reactions to hits, a protective fall and getting up again. Balance that steps to catch a fall, and writhing, come next.

## A figure

```aether
import ae3d.motion

r = physics.ragdoll(e, "Zombie", position, 8.0, 8.0, 1.0)
physics.ragdoll_dress(r, rig)                       // the skinned figure it wears
body = motion.active_ragdoll(r)                     // ANIMATED to begin with
motion.attach(e, body)                              // its recovery stepped every fixed step
motion.set_mode(body, motion.POWERED)               // the muscles play the animation
motion.hit(body, human.BONE_SPINE_03, point, impulse)
```

| Mode | What drives the figure |
|---|---|
| `ANIMATED` | The animation, position and all. The bodies follow it through strong anchors and collide as they go, so what they strike is pushed aside and the figure isn't. |
| `POWERED` | Muscles. Every joint's motor drives it toward the pose the animation gives it, within a torque budget. Nothing holds the figure up but its own feet and a balance the pelvis keeps within a budget of its own. The figure is drawn as its bodies are. |
| `LIMP` | A ragdoll. The animation follows the bodies. |
| `GETTING_UP` | Not set but got to: `get_up(body)` from the ground. A way up keyed on the rig, the bodies following it as an `ANIMATED` figure's do. |

**Two poses.** A powered figure has two poses: the animation's, which its muscles track, and its bodies', which is what gets drawn. The rig is where an animation writes a pose and where the skin is drawn from, so the physics module keeps the animation's pose beside it. Before each step it takes whatever the animation has written to the rig since the bodies last did, bone by bone, and drives the joints toward that. After the step it writes the bodies' pose into the rig for drawing. A clip that moves one bone leaves the rest of the pose where the animation last put them, not where the bodies fell. Until this, a powered figure was drawn in its animation's pose whatever its bodies did: knocked down, it was drawn standing.

## Muscles

A joint's muscle is its motor. Each fixed step the motor is asked for the relative angular velocity that would close the gap between the joint's pose and the animation's in 50 ms, capped at 20 rad/s. It may use no more torque than its budget:

| Joints | Budget (N·m) |
|---|---|
| hips, knees | 300, 250 |
| lower back, chest | 250 |
| shoulders, elbows | 80, 60 |
| neck | 40 |

These are an adult's peak joint torques, round. A motor is a hard constraint within its budget, so the figure carries its own weight on its knees and gives only when what pushes it is more than they have.

The first version drove joint springs instead. A spring's stiffness is relative to the two bones' own small inertia, so at any stiffness the figure sagged to the ground.

**Balance.** The pelvis alone is held upright from outside, by an anchor capped at 1,200 N·m. This is the balance a standing figure keeps, and what a blow past it overcomes. It holds the figure upright but lets it turn about the vertical. The value was measured: at 600 N·m the figure couldn't hold itself up on its capsule feet, and at 2,500 N·m not even a 400 N·s blow felled it. The balance controller that steps to catch a fall replaces this in a later slice.

**Strength and hits.** `set_strength(body, s)` scales every budget (0 is limp, 1 full strength). `hit(body, bone, point, impulse)` applies the blow and takes 95% of the struck bone's budget and half of its neighbours'. It comes back over `set_recovery` seconds (0.8 by default), so a shoulder shot drops the arm and the arm comes back up.

## The protective fall

A `POWERED` figure that leans more than 0.35 rad (20°) from its pose is falling (`falling(body)`), and it protects itself, as a person does (`set_protective(body, false)` turns this off):

- it stops fighting for a balance that is lost: the pelvis's assist goes to 0;
- its arms reach toward where it is falling, 0.7 down to 1 along the fall and a little out to each side so the hands land apart, and straight, since a straight arm takes the landing through its joints where a bent one folds on its elbow's muscle;
- its head tucks 0.5 rad away from the fall, as far as the neck goes.

If it catches itself (leans less than half the threshold again), the reach and the tuck let go and the balance comes back. If it lands, meaning it leans past 1.2 rad (69°) and holds still for ten steps, it lets go and lies (`lying(body)`): the reach and the tuck release and the muscles keep 15% of their budget, so it lies instead of holding a pose on the ground.

**In the street.** `street_drive`'s bystanders are active ragdolls. The ones that stand hold themselves up on their muscles, and the walkers walk by their animation until struck. The car's blow is about 60 N·s per m/s it was closing at, along its heading. Past 4 m/s it lands on the pelvis, which takes the balance with it, and the figure goes down reaching for the road. Below that it lands on the spine, and the figure is knocked back and recovers. On the autopilot's run the three bystanders in the road are struck at 7 to 10 m/s and all three go down. Before this they went limp the moment they were touched.

The reach and tuck are **aims** (`physics.ragdoll_aim(ragdoll, bone, rotation)`): a bone's joint drives it to a rotation in the world, from wherever its parent is, instead of to the animation's pose. The bones below it keep the animation's pose relative to it. Any controller can aim a bone this way, for example to turn a head toward a threat. `ragdoll_clear_aim` and `ragdoll_clear_aims` hand the bones back to the animation.

The settings were chosen by measurement. Each setting was tried on falls backward, forward and sideways, against a twin that does not protect itself, measuring the head's speed as it met the ground. The one chosen did better in all three directions, and so did its neighbours. Softening the knees in a fall, the obvious idea, made every direction worse.

## On a figure

`motion.on_figure(e, object)` gives an animated figure (`ae3d.figure`,
#439) an active ragdoll:

```aether
zombie = figure.figure(e, "Zombie", "assets/zombie.glb")
figure.play(figure.figure_of(zombie), "Walk", 1.0, true)
body = motion.on_figure(e, zombie)           // ANIMATED: the clip plays, the bodies follow
motion.set_mode(body, motion.POWERED)        // the muscles play the clip
motion.set_get_up(body, 3.0)
```

The ragdoll is made where the object stands. It is turned to face the
way the figure's feet point before it is dressed, so the figure keeps the
turn the scene gave it. The figure's bones are found by name, in whichever
humanoid naming they follow: the engine's pipeline's (`Hips`, `Spine`,
`Chest`, `ThighL`...) or Mixamo's (`mixamorig:Hips`, and without the
prefix, as most packs that follow it write them), whose top spine bone,
`Spine2`, is the ragdoll's chest. A rig that is neither gets no ragdoll
(`physics.humanoid_scheme` is -1). `motion.on_skeleton(e, object,
skeleton)` does the same for a skeleton that is not a figure's.

The scene file carries it as the `motion` record of the figure's group:
`{"mode": "powered", "strength": 0.8, "protective": false, "get_up": 3}`.
`motion.from_record(e, object, spec)` puts it back on a figure a scene
read in. A figure getting up is written as the mode it gets up to.

`tests/test_motion_figure.ae` checks two humanoid rigs, one in each
naming, each turned a quarter turn by its object, and a two-bone rig:

- both humanoids are known, and the two bones are not;
- dressed, each faces within 3.7° of its object's turn;
- `POWERED`, both stand on their muscles for two seconds, the pelvis at
  1.0 m and leaning at most 1°;
- the scene file brings back each one's mode, strength, protection and
  get-up.

## Getting up

`get_up(body)` gets a figure on the ground back on its feet, whatever mode it is in, and `set_get_up(body, seconds)` has it do so by itself once it has lain still that long (0, the default, never). On the ground is leaning past 1 rad, or the pelvis within 0.45 m of the figure's lowest point, which covers a figure that crumpled where it stood. Lain still is the pelvis moving less than 2 mm a step for ten steps. `down(body)` says it is.

The way up matches how it lies. The chest's front says which: to the sky is face up, to the ground face down. The chest, not the pelvis, because a figure propped on its arms can have its pelvis pointing anywhere.

- **Face up**, it sits up with its knees drawn in and its hands behind it, crouches over its feet, and stands, facing where its feet lay.
- **Face down**, it pushes up onto its hands and knees, brings its feet under it, and stands, facing where its head lay.

Each is three stages of 0.7 s, eased in and out: the pose it lies in, the first key, a crouch, and the animation's own pose. The last key is moved to where the figure stands (its feet where the sitting feet or the kneeling knees were) and turned to face the way it rose, so the animation takes over with nothing to jump. Each key between is the figure's reference bones, bent in its own frame from standing: a lean in degrees for each bone about the axis across the figure, placed bone by bone through their joints from the pelvis out and set down with its lowest point on the ground. The axis across comes from the knees: the reference's figure faces the way its knees don't bend. Its toes splay 23° from that, and a bow about an axis that far off bent the knees sideways.

The way up is the rig's, drawn exactly, and the bodies follow it through the anchors an `ANIMATED` figure's follow, so it collides as it rises. It can't be played by the muscles: the reference ragdoll's hips have a 30° cone and its knees 60°, too little to sit or crouch.

A figure that was `POWERED` is `POWERED` again once up; any other ends `ANIMATED`.

**Blends.** Two hand-overs used to jump.

- **`POWERED` to `ANIMATED`.** The figure blends over a quarter second from its bodies' pose to the animation's.
- **Into `POWERED` from a rig that drove it.** This covers a walker struck, and a figure that has just got up. The drawing goes from the rig's pose to the bodies' over a quarter second. The bodies follow a rig of other proportions only so closely, about 10° on each limb, and a figure drawn first one way and then the other popped by that much.

`tests/test_get_up.ae` holds it to numbers. Four figures take part:

- one knocked over backwards;
- one tripped, its shins swept back and its chest pushed on, which lands face down;
- one limp from the start, crumpled where it stood;
- one standing and powered, set `ANIMATED`.

| | face up | face down | limp |
|---|---|---|---|
| gets up by itself, half a second after it settles | yes | yes | yes |
| the worst turn of any drawn bone in one step | 4.6° | 3.5° | 4.1° |
| the worst move of the drawn hips in one step | 20 mm | 20 mm | 20 mm |
| the hand-over to the animation | 0°, 0 mm | 0°, 0 mm | 0°, 0 mm |
| the lowest any body goes on the way up | 2 mm into the ground | 7 mm | 2 mm |
| up, the most it leans in the next second | 1.1° | 1.1° | 1.7° |
| facing, against where its feet (head) lay | 16° | 12° | |

The blended figure takes 15 steps, a quarter second, and no drawn bone turns more than 1.3° in a step. The limits the test holds are 6° a step, 40 mm a step for the hips, 1° and 5 mm at the hand-over, 3 cm into the ground, 10° of lean afterwards and 30° of facing.

## Handing over from the horde

A horde can't be active ragdolls. Its thousands of zombies are instances posed from a pose bank: a position, a heading and a phase each, and no bodies. An active ragdoll costs 20 to 40 µs a fixed step. So `ae3d.handover` keeps a pool of a dozen figures and hands the few zombies that are struck to them, and takes them back when they recover.

```aether
import ae3d.handover

pool = handover.pool_new(e, "zombie.glb", "Walk", 12)   // the horde's file and the clip its bank was baked from
handover.set_horde(pool, pos, yaw, phase, count)       // the horde's columns
handover.set_facing(pool, facing)                      // the turn the horde's draw adds to a heading
handover.set_colors(pool, tint)                        // optional: each figure wears its zombie's tint

// every frame, the horde's step passes by the zombies handed over
crowd.crowd_step_out(pos, vel, yaw, phase, handover.outs(pool), 0, count, delta, ...)

handover.strike(pool, zombie, point, impulse)          // POWERED, hit: falls, gets up, goes back
handover.kill(pool, zombie, point, impulse)            // LIMP: lies, and is despawned
```

- **The pool.** Each figure is made from the horde's own file with `figure.figure`, and given an active ragdoll with `motion.on_figure`. Then it is parked: hidden, its clip stopped, and every body and anchor taken out of the physics world (`physics.ragdoll_park`). Twelve parked figures cost under 2 µs a frame.
- **Handing over.** `strike` sets the zombie's `out` flag, which the horde's step (`crowd.crowd_step_out`, `horde.step_out`) passes by. It parks the instance a million metres off, past every cull and the far plane, so both the CPU sort and the device's drop it without a change to either. A free figure is stood where the instance was drawn, turned as it was turned, its clip at the time the phase is at. Its bodies are put where its rig is (`physics.ragdoll_to_rig`), rather than dragged there by their anchors. Then it is `POWERED` and hit at the bone nearest the point. `kill` sets it `LIMP` instead.
- **Taking back.** Every fixed step the pool looks at its figures. One that is back on its feet stands `POWERED`, not falling, lying or getting up, leaning under 0.2 rad, with every muscle back from its blow. After `set_settle` seconds of that (0.5 by default), its animation is moved to where its bodies stand and it is set `ANIMATED`, which blends it into its clip over a quarter second. Blended, its zombie goes back to the horde where the figure stands, facing as it faces, at the phase its clip is at. The figure is then parked. `set_on_return` is told just before.
- **The dead.** A killed figure lies `LIMP` and is never given back. It is parked after `set_despawn` seconds (10 by default; 0 leaves it lying). Its zombie stays out of the horde (`is_dead`).
- **A full pool.** A strike takes the figure farthest from it that is not mid-fall: a killed one lying, or one standing over its blow. That figure is given back, or despawned, first. When every figure is falling, lying or getting up, the strike is refused (-1) and the zombie stays in the horde.

**No pop.** The pose bank is the clip struck in place: the root's travel over the ground is taken out of every bone (`crowd.posebank_bake_in_place`). So a figure playing the clip at time t, turned as the instance is, with its hips over the instance's position plus their bind offset, is the instance's pose at phase t / duration. Read backwards, the same relation puts a figure's pose back into the horde. Before each hand-over, the figure's nodes are put back as the file has them. Dressing a ragdoll poses the rig as the ragdoll stands, and a figure that lay limp last time would otherwise be stood in the pose it lay in.

`tests/test_handover.ae` holds it to numbers. The horde is 48 of the box man fixture, baked from its Idle. The pool is four of the same figure. No window is needed.

| | Measured |
|---|---|
| handing over: every bone against the pose bank's pose on the instance | 0.007 mm |
| handed over: the instance out of every tier of the sort, and not moved by the step | yes |
| four struck at 400 N·s: the lowest pelvis | 0.15 m |
| each gets up by itself and is back in the horde | all four, 7.3 s after the blow |
| giving back: every bone against the instance's pose | 5.2 mm: the figure stands 5 mm lower than the horde's road, the rest 0.007 mm |
| the zombie given back, from its figure's pelvis | 14 mm |
| a fifth strike while all four fall | refused |
| a killed one | down and kept for 2 s, despawned at 3 s, its zombie out of the horde |
| four shoved at 40 N·s and standing: a fifth strike | reuses the farthest, given back first |
| the whole run twice | the same to the bit |
| a frame of 400 zombies: no pool, twelve parked, twelve handed over | 2.7 µs, 4.3 µs, 290 to 400 µs |
| so a figure handed over, a frame | 24 to 33 µs: its motion, its physics, the pool |

The limits it holds are 2 mm at the hand-over, 10 mm at the giving back, 12 s to come back, 0.3 ms a frame for twelve parked and 3 ms for twelve handed over.

`examples/horde_strike.ae` is a field of 300 of any humanoid glTF (the box man by default), with a pool of twelve. A left click strikes the zombie under the cursor, and a right click kills it. With no one clicking, a timer strikes the zombie in the middle of the view every second and a half, and every third blow kills.

The networked horde (`ae3d.nethorde`) keeps its own columns and hands nothing over yet.

## What it is held to

`tests/test_motion.ae` runs eleven figures on one ground, each dressed in a humanoid rig:

| Figure | Measured |
|---|---|
| the tracker, its rig bowing the chest 15° forward and back | the chest within 1.6° of its pose; the figure never leans more than 0.7° |
| the shoved, 60 N·s to the chest | knocked 28.7° off its pose, back within 0.1° and upright three seconds later |
| the felled, 400 N·s to the chest | down |
| the struck, 8 N·s to the right upper arm | its muscle at 5% (the forearm's at half), then back to full |
| the limp | down |
| three protected fallers, felled by 400 N·s from the front, from behind and from the side | the hands reach the ground first each time (6, 4 and 15 steps before the head); the head meets it at 1.82, 1.73 and 1.22 m/s |
| their unprotected twins | the head meets it at 4.53, 3.68 and 2.01 m/s |
| the protected fallers, landed | each lets go and lies |
| the felled, as drawn | its drawn hips at 0.19 m, with its pelvis body (a standing figure's are above 0.8 m) |

The shoved never takes itself for falling. Hands and head are measured by their capsules' lowest points (the forearm's hand end, and the neck bone's capsule, which is the head).

Pose error is measured per joint (a bone against its parent), which is what a muscle answers for. The lean is the pelvis's up against the animation's. A figure turned about the vertical is still on its pose.

## Next

As #414 lays out:
- balance by feedback on the hips and the stance foot, stepping when the centre of mass leaves the feet;
- stagger and writhe;
- get-up clips from the pipeline in place of the keyed ways up, once a figure has them;
- the inspector's section, with a Hit button in the viewport;
- handing over from the networked horde: the strike and the giving back as horde inputs, so every peer takes the same zombie out at the same tick;
- drawing a powered figure's bodies each frame rather than each step, for an animation that plays on while its muscles track it.
