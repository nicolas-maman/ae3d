# The black hole demo

![the hole](black-hole.png)

A Kerr black hole with an accretion disc, drawn by integrating null geodesics
one per pixel. It is a tech demo rather than an engine feature, and it earns its
place twice over: it is the heaviest thing in the tree, so it is where the
engine gets pushed hardest, and every piece of its physics has an answer
somebody else derived, so it can be checked rather than admired.

```sh
./build.sh examples/black_hole.ae && ./build/black_hole
```

| key | |
|---|---|
| `SPACE` | hand the camera to WASD and the mouse, and back |
| `1` `2` `3` `4` | rays per pixel |
| `9` `0` | step budget, 220 or 400 |

The camera orbits by default. The lensing changes character with inclination —
steeper and the far side stops arcing over the shadow, nearly edge-on and it
closes towards a ring — and a still camera shows one slice of that.

## Where the code is

| | |
|---|---|
| `examples/lib/blackhole/module.ae` | the renderer: shaders, and the uniforms that drive them |
| `examples/black_hole.ae` | the scene — where the camera goes, what the keys do |
| `benchmarks/bench_black_hole.ae` | what a frame costs, and where |
| `tests/test_blackhole.ae` | the shadow, against general relativity |

The renderer is a module because three callers want it, not because it is engine
functionality — it is not, and it lives under `examples/lib` rather than in
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
motion — `pt` is the energy and `L` the angular momentum — so only `pr` and `pθ`
are carried. `H` is zero along the path, which is what lets the `dΣ` terms drop
out of the *r* and *θ* derivatives.

RK4, with the step set by arc length rather than by parameter: fine where the
path bends, coarse where it does not.

**The disc.** The Novikov-Thorne flux — Page & Thorne (1974), the solution of
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
dragging. Observed temperature is `T·g`, observed brightness `g⁴` — specific
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
observer's inclination — that number comes from the photon sphere and from
nothing in this code. Beside it sits a second closed form: light leaving the
ISCO at `r = 6 M` arrives with `b = r/sqrt(1 − 2/r) = 7.35 M`, *further out* than
the shadow, because the hole bends it outward on the way. Between the two is a
genuinely dim annulus, lit only by paths that wound round the hole and came back
redshifted. Where a brightness scan lands inside that gap depends on the
threshold; that it lands inside it at all does not.

```
  a=0:   dark to 47 px left, 46 px right; shadow 41.9 px, disc rim 59.5 px
  a=0.9: dark to 24 px left, 32 px right
```

At `a = 0` the two halves agree, because a Schwarzschild shadow is a circle. At
`a = 0.9` the shadow shrinks and stops being symmetric, because frame dragging
lets prograde photons escape from closer in.

Two details of that test are worth copying into any other one of its kind. It
runs with bloom off, because bloom spreads disc light into the shadow and moves
the edge being measured. And it finds the edge against **each side's own** peak
brightness rather than an absolute level: the two sides of the disc differ by a
factor of several — that is the beaming the renderer exists to show — so a fixed
threshold measures the beaming instead of the geometry. The first version of the
test made exactly that mistake and read the receding side as 319 px against the
approaching side's 46.

Setting `SPIN` to 0 turns the metric back into Schwarzschild. That limit has a
known answer, so it is the check the Kerr integrator was built against.

## What a frame costs

`benchmarks/bench_black_hole.ae` runs offscreen, because a windowed run reports
`engine_fps` and that is pinned to the display's refresh — on a 144 Hz panel it
hides everything under 6.9 ms. The example reported "144 fps" through an entire
rewrite from Schwarzschild to Kerr.

RTX 4070 Ti, 1280×800, `a = 0.9`:

```
  1 ray  400 steps:  4264 us per frame, 234 fps
  2 rays 400 steps:  7680 us per frame, 130 fps
  4 rays 400 steps: 15578 us per frame,  64 fps
  4 rays 140 steps: 12281 us per frame,  81 fps
```

The step budget is not where the time goes — halving it saves under 10%. Cost
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
  harmlessly, which is the worst outcome — it would have surfaced on somebody
  else's machine.

## Sources

- Page, D. N. & Thorne, K. S. (1974), *Disk-accretion onto a black hole* —
  the relativistic thin-disc flux.
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972) — the ISCO and photon
  orbits of a Kerr hole.
- Chandrasekhar, S. (1950), *Radiative Transfer* — the limb darkening law.
