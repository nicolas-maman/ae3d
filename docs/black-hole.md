# The black hole demo

![the hole](images/black-hole.png)

A Kerr black hole with an accretion disc, drawn by integrating null geodesics
one per pixel. It is a tech demo rather than an engine feature, and it earns its
place twice over: it is the heaviest thing in the tree, so it is where the
engine gets pushed hardest, and every piece of its physics has an answer
somebody else derived, so it can be checked rather than admired.

```sh
./build.sh examples/black_hole.ae && ./build/black_hole
```

It runs on OpenGL: the whole picture is one fragment shader written as
GLSL in `examples/lib/blackhole/module.ae`, and the Vulkan backend compiles
no GLSL at run time (its shaders are SPIR-V generated at build time). Asked
for Vulkan (`AE3D_API=vulkan`) the backend says so and draws the quad with
the scene shader.

| key | |
|---|---|
| `SPACE` | hand the camera to WASD and the mouse, and back |
| `1` … `6` | quality, 1 highest, and also takes it off the frame-time guard |

The camera orbits by default. The lensing changes character with inclination,
steeper and the far side stops arcing over the shadow, nearly edge-on and it
closes towards a ring, and a still camera shows one slice of that.

## Where the code is

| | |
|---|---|
| `examples/lib/blackhole/module.ae` | the renderer: shaders, and the uniforms that drive them |
| `examples/black_hole.ae` | the scene: where the camera goes, what the keys do |
| `benchmarks/bench_black_hole.ae` | what a frame costs, and where |
| `tests/test_blackhole.ae` | the shadow, against general relativity |

The renderer is a module because three callers want it, not because it is engine
functionality. It is not, and it lives under `examples/lib` rather than in
`src/ae3d` to say so. `build.sh` puts `examples/lib` on the module search path
beside `src`, so `import blackhole` resolves the same way `import ae3d.core`
does.

## What it computes

Lengths are in units of *M*, the gravitational radius. The horizon sits at
`1 + sqrt(1 - a²)`: 2 for a still hole, 1 for a maximally spinning one.

**The geodesics.** The metric is Kerr in Boyer-Lindquist coordinates, integrated
as Hamilton's equations for a null ray. Writing

```
Σ·2H = Δ·pr² + pθ² − P²/Δ + Q²
```

with `P = (r²+a²)pt + aL` and `Q = a·sin(θ)·pt + L/sin(θ)`, the whole system is
derivatives of that one expression. Two of the four momenta are constants of the
motion, `pt` being the energy and `L` the angular momentum, so only `pr` and `pθ`
are carried. `H` is zero along the path, which is what lets the `dΣ` terms drop
out of the *r* and *θ* derivatives.

RK4, with the step set by arc length rather than by parameter: fine where the
path bends, coarse where it does not.

**The disc.** The Novikov-Thorne flux, Page & Thorne (1974), the solution of
the relativistic thin-disc equations in this metric, not a Newtonian `r^(-3/4)`
with a boundary factor bolted on. It depends on the spin all the way through, so
the temperature is not pinned: spin the hole up and the inner disc genuinely
runs hotter and bluer, because the ISCO moves from 6 *M* in to 2.32 *M* at
`a = 0.9` and gas that close is heated differently.

The disc has thickness (`H/r = 0.015`, about what a radiatively efficient thin
disc runs at) and is marched through rather than crossed, so it obscures itself
and its silhouette is soft. It carries Chandrasekhar limb darkening.

**One factor for three effects.** Gravitational redshift, Doppler beaming and
frame dragging are not computed separately, because they are not separate:

```
g = 1 / (u^t (1 − ω L))
```

`u^t` is how fast the orbiting gas's clock runs against a distant one;
`ω L` projects its motion onto the photon; and `ω = 1/(r^{3/2} + a)` carries the
dragging. Observed temperature is `T·g`, observed brightness `g⁴`, since specific
intensity over frequency cubed is the invariant.

**The sky.** Stars are generated from the direction a ray finally leaves in, so
they are lensed by the same integration as everything else. The arcs near the
hole are the geodesic, not a post-process.

## What it does not compute

Each of these is absent for a reason, not by oversight.

- **Returning radiation.** Light the disc emits, that the hole bends back onto
  the disc, and that is re-radiated. It is real and it is large near the inner
  edge at this spin. It cannot come out of a single backwards trace: every point
  on the disc would need its own integration over the sky to find what is
  shining on it.
- **The jets as physics.** They are a shape with a Lorentz factor. A real jet is
  launched by magnetic fields threading the horizon, which is a simulation and
  not a closed form.
- **The plunging region.** The gas is on circular orbits with no radial drift,
  and the disc is cut at the ISCO rather than continuing inward as an inflow.

## Checking it

The output is a picture, and a picture can be wrong in ways that still look
plausible. `tests/test_blackhole.ae` asserts what relativity fixes rather than
what this renderer happens to draw.

A still hole's shadow is a circle of impact parameter `sqrt(27) M`, whatever the
observer's inclination. That number comes from the photon sphere and from
nothing in this code. Beside it sits a second closed form: light leaving the
ISCO at `r = 6 M` arrives with `b = r/sqrt(1 − 2/r) = 7.35 M`, *further out* than
the shadow, because the hole bends it outward on the way. Between the two is a
genuinely dim annulus, lit only by paths that wound round the hole and came back
redshifted. Where a brightness scan lands inside that gap depends on the
threshold; that it lands inside it at all does not.

```
  a=0:   dark to 61 px left, 46 px right; shadow 41.9 px, disc rim 59.5 px
  a=0.9: dark to 24 px left, 32 px right
```

Symmetry is deliberately not asserted, though a Schwarzschild shadow is a circle
and the frame contains one. What the test measures is a *brightness* edge, and
the disc's brightness is not symmetric even when the shadow is: beaming makes the
approaching side brighter and changes the shape of its radial falloff, so each
side crosses a fraction of its own peak at a different radius. Asserting symmetry
would be asserting something about the disc's emission while claiming to measure
the hole.

Three details of that test are worth copying into any other of its kind, and all
three came from getting it wrong first.

It runs with **bloom off**, because bloom spreads disc light into the shadow and
moves the edge being measured.

It runs with **the stars off**. Stars are lensed, so where one lands is a
geodesic, and two drivers need not agree on its last bit. A single star sitting
in the dim annulus reads exactly like the disc's edge, which is what failed the
test on Mesa's llvmpipe while passing on an NVIDIA card, and, once the stars were
removed, turned out to have been happening on the NVIDIA card too.

It finds the edge against **each side's own** peak brightness, and requires
**four lit pixels in a row** rather than one. The two sides differ by a factor of
several, so a fixed threshold measures the beaming instead of the geometry; the
first version of this test did exactly that and read the receding side as 319 px
against the approaching side's 46.

Setting `SPIN` to 0 turns the metric back into Schwarzschild. That limit has a
known answer, so it is the check the Kerr integrator was built against.

## Running it on a potato

Quality is a ladder rather than two loose knobs, because the two do not trade off
evenly. Rays per pixel buys edges. Render scale buys everything, because cost is
dominated by pixels shaded and half scale is a quarter of the work. So the ladder
spends rays first, and only then starts drawing the scene smaller than the window
it lands on.

| level | rays | steps | scale | RTX 4070 Ti |
|---|---|---|---|---|
| 1 | 4 | 400 | 1.0 | 63 fps |
| 2 | 2 | 400 | 1.0 | 130 fps |
| 3 | 1 | 400 | 1.0 | 257 fps |
| 5 | 1 | 220 | 0.5 | none |
| 6 | 1 | 140 | 0.35 | none |

For scale, `4 rays / 400 steps` costs 15662 µs at full size and 5469 µs at half,
2.9x for a change nothing in the frame's content notices. `2 rays / 220 steps` at
half scale runs at 411 fps, about a sixth of the top rung's cost.

`renderer_set_render_scale` is an engine feature rather than something this demo
does to itself: the scene is drawn into the post-processing buffer at a fraction
of the window, and the composite that was already there upscales it. Any scene
gets it. `tests/test_render_scale.ae` holds it to the part that matters, that a
scale of 1 is exactly a no-op, that half scale is the same picture rather than a
different one, and that the result fills the frame instead of sitting in a corner
at half size.

## What a frame costs

`benchmarks/bench_black_hole.ae` runs offscreen, because a windowed run reports
`engine_fps` and that is pinned to the display's refresh: on a 144 Hz panel it
hides everything under 6.9 ms. The example reported "144 fps" through an entire
rewrite from Schwarzschild to Kerr.

RTX 4070 Ti, 1280×800, `a = 0.9`:

```
  1 ray  400 steps:  4264 us per frame, 234 fps
  2 rays 400 steps:  7680 us per frame, 130 fps
  4 rays 400 steps: 15578 us per frame,  64 fps
  4 rays 140 steps: 12281 us per frame,  81 fps
```

The benchmark honours `AE3D_BENCH_FRAMES` like the rest of the suite, and probes
one cheap frame before the sweep: a per-pixel geodesic integration is orders of
magnitude more work than anything else here, and a software rasteriser needs
seconds where this needs milliseconds. If the probe is slow the sweep is skipped
with a line saying why, because a timing taken from llvmpipe would not describe
anything.

The example guards itself the same way from the other side. Quality falls back
when the frame runs long. The first frame is judged on its own, since that is
the expensive one on a slow machine and the point is not to render a second like
it. It never climbs back by itself: climbing needs an estimate of what one ray
costs, and under vsync there is none to be had, because the frame time is
quantised to the refresh and one ray and two can measure identical. The number
keys take over.

The step budget is not where the time goes: halving it saves under 10%. Cost
scales with rays per pixel and with the work done per step, which is why both
are uniforms rather than compile-time constants: the benchmark sweeps them, and
a slower machine turns them down with a keypress instead of a rebuild.

Turning pieces off says where the rest is: **85% is the integration itself**,
9% the disc, 6.5% the jets.

## What debugging this taught the engine

The demo exists partly to find things. What it has found so far:

- **An emissive surface could not carry a colour.** The shader's emissive branch
  returned hardcoded white, discarding `diffuseColor`, the texture and the
  per-instance tint alike, and skipped tone mapping so emissive surfaces sat in
  a different colour space from every lit surface beside them. Fixed in
  `src/ae3d/shaders`; covered by `tests/test_instance_colours.ae`.
- **The bulk instance-position path had no test.** `test_instances.ae` drives
  the per-index setter, so a swarm arriving on the GPU stacked in a single spot
  passed CI. Covered now by `tests/test_instance_positions.ae`.
- **Numeric readback has to come from an unfiltered frame.** Three debug renders
  were read back through the bloom pass, which brightens every value; the
  numbers were wrong and so were the conclusions drawn from them.
- **`smoothstep(hi, lo, x)` is undefined**, not reversed. One driver clamped it
  harmlessly, which is the worst outcome, since it would have surfaced on somebody
  else's machine.

## Sources

- Page, D. N. & Thorne, K. S. (1974), *Disk-accretion onto a black hole*,
  the relativistic thin-disc flux.
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972), the ISCO and photon
  orbits of a Kerr hole.
- Chandrasekhar, S. (1950), *Radiative Transfer*, the limb darkening law.
