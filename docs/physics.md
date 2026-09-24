# Physics

The engine's physics is [aephysics](https://github.com/aether-lang-dev/aephysics),
a rigid body engine written in Aether on Box3D's design, and `ae3d.physics`
is the seam that puts its world in the engine's loop in the shape Unity
gives it: a `Rigidbody` component on a game object, colliders on it, the
engine's fixed step driving the world, the world's transforms driving the
draw.

![A car on wheel joints through the zombie street at night, its bystanders standing on the pavements](images/street-drive.png)

<sub>`examples/street_drive.ae`: the buildings, kerbs and road collide as their own triangles, every prop as the convex hull of its own mesh, the car as a chassis and four wheels on wheel joints, the bystanders as ragdolls sprung upright until struck. 501 bodies, stepped four times a fixed step over every hardware thread; 144 fps hidden on an RTX 4070 Ti at 1280×720 with ray-traced shadows on, the physics 0.09 ms of the frame.</sub>

## The engine underneath

aephysics is a port of Box3D, chosen by a bake-off against Jolt's design
(`bench/RESULTS.md` there): hulls, triangle meshes, height fields and
compounds; a character mover; joints of every kind (revolute, prismatic,
spherical, weld, distance, wheel); sensors and events; continuous
collision; a soft-step contact solver with graph colouring and islands;
a wide contact solver on native float lanes; and a persistent scheduler
that steps the world in stages over any number of threads and gives the
same answer to the bit at every count. Every layer is tested against the
reference and benchmarked beside it; the cross-platform tests prove the
bit-exactness across Linux, macOS and Windows. Its twelve-bone human
(`aephysics.human`) is the ragdoll.

The engine lends the world its own job pool (`WorldDef.scheduler`), so the
physics step and everything else a frame spreads over the cores share one
set of threads ([architecture.md](architecture.md#the-job-pool)).

## Using it

```aether
p = physics.attach(e)                                              // the world, as a behaviour on the engine
ground = engine.object(e, "Ground", loader.cube(1.0))
physics.box_collider(ground, core.vec3(20.0, 0.5, 20.0), physics.material(0.6, 0.0))  // no Rigidbody: static
crate = engine.object(e, "Crate", loader.cube(1.0))
physics.rigidbody(crate, physics.DYNAMIC)
physics.box_collider(crate, core.vec3(0.5, 0.5, 0.5), physics.material(0.6, 0.0))
```

`attach` puts the world on the engine: gravity down, four sub-steps a
fixed step. Every `fixed_update` it drives each kinematic body toward
where its object was put (as a velocity, so the solver sees it move and
what rides on it comes along), steps the world, and writes each dynamic
body's transform onto its object and each ragdoll's bones onto theirs.
The object keeps no transform of its own -- it points at its model's --
so the draw reads what the world wrote. A body lives in the world and
an object may live in a group (its model parented to another's), so the
transforms cross in world space: `object_world_position` in, and back
to the object's local place under its parent
(`object_set_world_position`); a group can be moved and its bodies still
fall where it is.

| | |
|---|---|
| `rigidbody(o, DYNAMIC \| KINEMATIC \| STATIC)` | the component; a collider on an object without one makes a static body |
| `box_collider`, `sphere_collider`, `capsule_collider` | shapes about the object's origin, with a `material(friction, restitution)` and a density |
| `hull_collider(o, hull, m)`, `convex_collider_of_model(o, m)` | a convex hull, or the hull of the object's own mesh at the model's scale |
| `mesh_collider(o, mesh, m)`, `mesh_collider_of_model(o, m)` | the object's triangles themselves, for ground and buildings |
| `rigidbody_set_velocity`, `rigidbody_apply_impulse`, `rigidbody_apply_force`, `rigidbody_set_damping` | the usual |
| `rigidbody_enable_hit_events(rb, true)`; `hit_event_count(p)`, `hit_event(p, i)` | contacts above the world's hit speed, reported after each step with the shapes, the point, the normal and the approach speed |
| `physics_body_count`, `physics_step_count`, `physics_set_gravity` | the world's counters and gravity |
| `physics_free(p)` | the world and its shape data; the objects stay the scene's, without their Rigidbody components |
| `spec_of(o)`, `apply_spec(o, spec)`, `attachments(p)` | the scene file's record of a body, and a body from one (below) |

Hulls and meshes are held by the world by reference; `physics_free`
releases them after the world, so a program never frees one itself.

### The scene file

A body is recorded on its model in the scene file (`ae3d.scene`,
`PhysicsSpec`): the body's kind, its first collider's kind as one of the
five the object's own mesh can make -- `box` (its bounds), `sphere`,
`capsule`, `hull` (its vertices), `mesh` (its triangles) -- and the
surface's friction and restitution and the density. `spec_of(o)` answers
it for an object with a body; `apply_spec(o, spec)` gives an object a body
and a collider from one, which is what the editor's Simulate does for
every object with a record. `attach` registers the module as one of the
engine's attachment providers, so `engine_save_scene` -- and
`AE3D_SCENE_OUT=path`, which writes any program's scene on its first
frame -- carries the record of every body, and the street opens in the
editor with its 299 bodies ([docs/editor.md](editor.md)). A ragdoll's
bones are the ragdoll's and are not recorded one by one.

## Ragdolls

`ragdoll(e, name, position, friction_torque, hertz, damping_ratio)` is
aephysics's twelve-bone human with a game object per bone (named
`"<name>/<bone>"`, drawn as the bone's capsule), following its bones every
step. `ragdoll_kick` and `ragdoll_set_velocity` act on all of it.

`ragdoll_stand(r, ground, hertz, damping_ratio)` makes the figure stand:
its pelvis is sprung upright to the ground's body and every bone is held
at its pose by a kinematic anchor through a parallel joint -- the pose
drive of an active ragdoll, rotation only -- so it stands, and stays
standing, until `ragdoll_release` lets the springs and anchors go and it
falls as the figure it is. `ragdoll_turn(r, yaw)` turns the whole figure
about its pelvis before it stands. `ragdoll_of_shape(p, shape)` answers
which ragdoll a hit event's shape belongs to, which is how a car knows
what it struck.

`ragdoll_dress(r, skeleton)` puts a skinned figure on the ragdoll: a
`skin.Skeleton` whose bones are named as the engine's Blender pipeline
names them (`Hips`, `Spine`, `Chest`, `Neck`, `ThighL`, `KneeL`,
`ShoulderL`, `ElbowL`, ...; `ragdoll_dress_named` takes another rig's
names). The two rest poses are not the same figure, so at dressing each
rig bone is first turned to point where the body's bone does -- from its
joint toward its child's, or along its capsule -- after the figure has
been faced the ragdoll's way from where each one's foot points; the
rotation left between the body's frame and the bone's is what the body
carries from then on. Every fixed step the root rides the pelvis and each
mapped bone takes its body's rotation, parents before children, and the
unmapped bones (wrists, toes, the crown) follow their parents, so the
mesh weighted to the rig stands, falls and lies as the ragdoll does with
its own bone lengths intact. The ragdoll's capsule models go unseen.

`ragdoll_follow(r)` turns it round: the rig drives the ragdoll. Every
fixed step each mapped bone's kinematic anchor is driven to where the
rig has the bone (the dressing's offsets, inverted), and the bodies
follow through their joints' springs -- the pose drive of an active
ragdoll. So a rig animated by a clip walks its figure as an animated
character while the figure collides as the ragdoll it is, and
`ragdoll_release` -- on the car's hit event -- lets the anchors go and
the bodies drive the rig again from wherever they were: the character
animated until the moment it is struck is the ragdoll that falls. The
street's walkers are this: the export's gait cycle on a player per bone,
the root placed each step where the walker has got to at the clip's own
pace (0.85 m/s), the ragdoll a step behind.

`ragdoll_power(r, torque, assist)` plays the rig through the joints'
motors instead, within a torque budget, and `ragdoll_aim(r, bone,
rotation)` drives one bone to a rotation in the world from wherever its
parent is, instead of to the rig's pose (`ragdoll_clear_aim`,
`ragdoll_clear_aims`). Balance, the protective fall and the rest of the
NaturalMotion line are built on these in `ae3d.motion`
([docs/motion.md](motion.md)).

## Vehicles

`vehicle(chassis, wheels, mounts, suspension_hertz, suspension_damping,
travel, max_torque, max_speed, max_steer)` builds a car from a chassis
(a rigidbody with its colliders on it) and four wheel objects, placed at
their mounts in the chassis's frame -- front left, front right, rear
left, rear right -- and joined by aephysics's wheel joints: a spring
along the chassis's up with a travel limit (the suspension), a motor
about the axle (the drive), and steering on the front pair with its own
spring and limit. `vehicle_drive(v, throttle, steer, brake)` sets the
motors each step: the rear wheels drive, the brake is every motor holding
its wheel still at full torque, and a wheel neither driven nor braked
rolls free. `vehicle_speed` is the chassis's speed along its heading.

Two things learned building it, both now in the module rather than in the
program: a wheel's collider is a sphere, because a faceted cylinder sits on
a facet and will not roll; and both joint frames share one basis with the
wheel body unrotated, so the wheel rests where the joint has nothing to
undo. A first car crawled because its free wheels had motors at speed zero
-- a motor holding a wheel still is a brake.

## Characters

`physics.character_controller(object, radius, height)` gives a game object a
player's body (#420). It's a capsule that walks the world rather than a rigid
body that tumbles through it, and the object's position is its feet.
`character_move(controller, walk, jump, delta)` takes the horizontal velocity
asked for and a vertical speed to jump at (only from the ground). Gravity
comes from the world.

The mover is Box3D's, driven the way its documentation lays out (`reference/box3d/docs/character.md` in aephysics):
- cast the capsule along what is left of the move and move as far as the world allows;
- gather the planes it then touches and solve them for no move at all, only out of any overlap;
- clip what is left of the move, and the velocity, against those planes, so a wall is slid along.

The first version solved the whole move against the planes before casting. The solver's slop then took the capsule a few millimetres into a wall each step, and through a half-metre step in a second.

The capsule rides a step height (0.35 m) above the feet, so a kerb or a stair lower than that passes under it. A ray from there down finds the ground, and walkable ground (flatter than the slope limit, 45°) puts the feet on it. To a character on the ground, a plane too steep to walk on is a wall. Its normal is laid flat, keeping the separation it measures, so the round bottom of the capsule against a step's edge doesn't lift it over. In the air a steep slope is what it is, and is slid down. The snap down onto the ground is a cast of the capsule, not a jump: a ray can find a floor the capsule can't reach, like the pavement at the foot of a gap narrower than the capsule between two buildings, and snapping to it put the capsule 72 mm into their walls. Now the capsule is cast down the drop first and stops where it touches.

`tests/test_character.ae` holds it to numbers:
- 3.00 m walked in a second at 3 m/s;
- up a 0.3 m step, feet at 0.30 m;
- stopped by a 0.5 m step at 2.745 m (the face at 3 m, less the radius);
- no drift in 2.75 s on a 40° ramp, and 4.6 m slid down a 50° one;
- a 5 m/s jump rising 1.29 m against v²/2g = 1.27 (within 2%);
- no character more than 4.3 mm into the world at the end, within the solver's 5 mm slop.

`AE3D_ON_FOOT=3 street_drive` wanders: 10,000 random moves through the street, strolling to running, turning every half second, jumping now and then, into the kerbs, steps, buildings and props. After every move it measures how deep the capsule is into anything and whether it fell through the street. The last run: deepest 5 mm (the slop), 787 moves between 1 and 5 mm, none past 6 mm, no falls. `ci.sh` runs it and fails on any move past 6 mm or any fall (#420).

`street_drive` uses it. Press E by the car, stopped, to get out, and walk the street first-person: W/S and A/D where you look, the mouse to look, shift to run, space to jump. Press E by the car to get back in. `AE3D_ON_FOOT=1` starts on foot, and `AE3D_ON_FOOT=2` walks on its own for a run without a keyboard: 4.8 m in three seconds at a walk, on the road. A dynamic body the character walks into is pushed with a person's force (`push_force`, 400 N). The push is capped at what brings the body up to the character's pace, so a person leans on a crate at a walk rather than throwing it: a 20 kg crate slides ahead with its near face at the character's front. What 400 N can't move against its friction, such as an 800 kg crate, stops the character at its face like a wall.

**In the scene file.** A model's physics record with body `"character"` (the editor's Character kind) is a character: `apply_spec` makes it from the mesh -- a capsule as wide as the mesh's widest half and as tall as it -- with the object's origin `lift` above the feet, the mesh's lowest point, so a model centred on its origin stands with its bottom on the ground. `spec_of` and the scene's attachments write it back the same way. The world keeps a list of its characters; one that nothing has moved yet (no `character_move` from a script) is stood each fixed step, under gravity and on the ground. `character_of(object)` finds an object's character. `tests/test_character.ae`: a 1.8 m box centred on its origin, dropped from 2 m with a character record, stands with its middle at 0.90 m.

## The scenes

**`examples/physics.ae`** runs four of the reference's own scenes rather
than scenes made to look good, every body a game object whose transform is
the world's each step: `AE3D_PHYSICS_SCENE=pyramid` (seventy-eight crates
and an iron ball through them), `pile` (spheres, capsules and crates on a
wave of ground), `ragdolls` (eight figures falling onto a torus) and
`cloth` (a grid of spheres on spherical joints with a ball rolled into it).

**`examples/street_drive.ae`** is the street driven. The city block from
the Blender pipeline is loaded three tiles long, its ground, buildings and
kerbs as mesh colliders, its crates, bins and benches as dynamic bodies
with the convex hull of their own mesh (built to the vertex budget a hull
holds: a bevelled crate is 56 corners and more edges than the limit, so
its hull is the tightest 32-vertex one); a car is built from hulls and
driven on wheel joints; thirteen bystanders -- the pipeline's zombie
figure, skin and clothes weighted to a rig of its own for each, worn by
a ragdoll -- stand on the pavements and in the road or walk the
pavements on the export's gait cycle, sprung or driven until the car's
hit events name them, and fall as their ragdolls fall. The street is
five blocks into a fog whole by 230 m; `AE3D_CAMX/Y/Z` and
`AE3D_AIMX/Y/Z` place the camera by number. Left
alone for three seconds the car drives itself up and down the street with
a lane controller and a U-turn on the open tarmac at each end, so the
scene runs unattended and `AE3D_FRAMES=n` gives a fixed run; `AE3D_DIAG=1`
prints the car's state, a ray cast under a wheel and every hit each step.

```
W / S   throttle and reverse      A / D   steer
space   brake                     shift   handbrake turn
```

## Which examples use it

Every example was audited for bodies the physics engine should own.
`physics` and `street_drive` are physics scenes. The others are not, each
for a reason: `spinning_cube`, `models`, `lights`, `backend_switch`,
`blender_pipeline` and `gltf_viewer` show the renderer and the loaders
and have nothing that falls or collides; `caustics`, `smooth_terrain`,
`voxel_world` and `black_hole` are environments with no moving body;
`sand` is a million point instances slumping to an angle of repose, a
particle rule of its own that a rigid body per grain would be a
thousand times the cost of; and the hordes of `zombie_city` and
`gltf_crowd` are the crowd's packed columns, separated and steered by
`ae3d.horde` and `ae3d.nav`, which is what a crowd of that size can be.
A figure that meets a car is a ragdoll, and the street shows that seam.

## What is checked

`tests/test_physics.ae` drives a scene for a fixed number of steps and
checks, among others: a collider without a Rigidbody makes a static body;
the world stepped as often as the fixed step ran; a crate fell onto the
slab, rests on it, followed its body every frame and is asleep at rest; a
kinematic lift rose and its rider rode it; a ball knocked a crate along; a
ragdoll fell; a sprung ragdoll is still standing after four seconds; a
scaled hull rests on its half height; a car drove forward on its wheel
joints and straight; its hits were reported and the figure in its way let
go; the rig that figure wears sits on its pelvis when dressed and still
does after the strike, its head down with the ragdoll's neck; a crate
made from a scene record fell and rests like the one made by hand; the
world written as a scene reads back with a record on every body; and
after `physics_free` the objects are still the scene's, with no
Rigidbody left on them. The
physics engine's own suites (`scripts/test.sh` in the submodule) hold each
layer to the reference.

## Where the time goes

The step is stages over the pool: the broad phase, the contact begin, the
constraint solve by graph colour, the finalize, the continuous pass.
`AE3D_PERF=1` reports the fixed updates' time in `update_ms`: the street's
501 bodies at four sub-steps are 0.09 ms of a frame (277 steps over 420
frames, so about 0.13 ms a step) against 1.8 ms of device time for the
picture, on an RTX 4070 Ti with the rays on. At the reference's
benchmarks (`bench/RESULTS.md` in aephysics: pyramids, a joint grid,
ragdolls, the large pile) the parallel layer scales as the reference's
does.
