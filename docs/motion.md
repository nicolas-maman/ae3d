# Natural motion

`ae3d.motion` is an active ragdoll (#414): a figure whose animation is played by its muscles rather than pasted onto its bones. It's what GTA IV and V get from NaturalMotion's Euphoria. Hit it, and it gives where it was hit and comes back; hit it harder than it can take, and it falls. It's an option on the engine's own motion system, a component on any ragdoll that wears a skinned figure (`physics.ragdoll_dress`), and off unless asked for.

This page describes the first slice. The behaviours built on it (balance that steps to catch a fall, protective arms, writhing, getting up) come next.

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
| `POWERED` | Muscles. Every joint's motor drives it toward the pose the animation gives it, within a torque budget. Nothing holds the figure up but its own feet and a balance the pelvis keeps within a budget of its own. |
| `LIMP` | A ragdoll. The animation follows the bodies. |

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

If it catches itself (leans less than half the threshold again), the reach and the tuck let go and the balance comes back.

The reach and tuck are **aims** (`physics.ragdoll_aim(ragdoll, bone, rotation)`): a bone's joint drives it to a rotation in the world, from wherever its parent is, instead of to the animation's pose. The bones below it keep the animation's pose relative to it. Any controller can aim a bone this way, for example to turn a head toward a threat. `ragdoll_clear_aim` and `ragdoll_clear_aims` hand the bones back to the animation.

The settings were chosen by measurement. Each setting was tried on falls backward, forward and sideways, against a twin that does not protect itself, measuring the head's speed as it met the ground. The one chosen did better in all three directions, and so did its neighbours. Softening the knees in a fall, the obvious idea, made every direction worse.

## What it is held to

`tests/test_motion.ae` runs eleven figures on one ground, each dressed in a humanoid rig:

| Figure | Measured |
|---|---|
| the tracker, its rig bowing the chest 15° forward and back | the chest within 2.4° of its pose; the figure never leans more than 0.7° |
| the shoved, 60 N·s to the chest | knocked 28.7° off its pose, back within 0.1° and upright three seconds later |
| the felled, 400 N·s to the chest | down |
| the struck, 8 N·s to the right upper arm | its muscle at 5% (the forearm's at half), then back to full |
| the limp | down |
| three protected fallers, felled by 400 N·s from the front, from behind and from the side | the hands reach the ground first each time (6, 4 and 15 steps before the head); the head meets it at 1.82, 1.73 and 1.22 m/s |
| their unprotected twins | the head meets it at 4.53, 3.68 and 2.01 m/s |

The shoved never takes itself for falling. Hands and head are measured by their capsules' lowest points (the forearm's hand end, and the neck bone's capsule, which is the head).

Pose error is measured per joint (a bone against its parent), which is what a muscle answers for. The lean is the pelvis's up against the animation's. A figure turned about the vertical is still on its pose.

## Next

As #414 lays out:
- balance by feedback on the hips and the stance foot, stepping when the centre of mass leaves the feet;
- stagger, writhe, and get up, blending back to the animation without a pop;
- the inspector's section, with a Hit button in the viewport;
- the street's bystanders reacting to the car instead of going limp.
