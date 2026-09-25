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

## What it costs

An active ragdoll is for the figures a player is close to, not the horde:
the horde hands the dozen nearest over when they are struck.
`tests/test_motion_cost.ae` measures what that costs. It steps 0, 4, 16
and 32 `POWERED` figures standing on one ground, each figure's chest
bowing so it stays awake, for 120 fixed steps once they have settled:

| figures | the step | a figure | in a millisecond |
|---|---|---|---|
| 0 | 0.2 µs | | |
| 4 | 142 µs | 35 µs | 28 |
| 16 | 330 µs | 21 µs | 49 |
| 32 | 636 µs | 20 µs | 50 |

A figure standing still falls asleep and costs nothing, which is why the
figures bow: an awake figure is what the step pays for. A dozen awake
figures cost about a quarter of a millisecond a step. The test holds a
figure under 80 µs, a dozen within a millisecond, and every figure still
standing at the end.

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
- drawing a powered figure's bodies each frame rather than each step, for an animation that plays on while its muscles track it.
