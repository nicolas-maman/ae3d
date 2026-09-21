# Writing Aether in this engine

The conventions the engine's Aether follows, and the edges of the language
it is written around. Each edge that is the language's to fix is filed
against it and linked; the engine does not work around a compiler bug
silently.

## Strings and who frees them

A struct owns its `string` fields. `heap.free` releases them, whether the
assignment was written against a local struct pointer or made through a
setter that takes the pointer as a parameter:

```aether
set_path(e: *Entry, v: string) {
    e.path = core.set_owned(e.path, v)
}

make(v: string) -> *Entry {
    e = heap.new(Entry)
    set_path(e, v)
    return e
}

heap.free(e)        // releases e.path; nothing else has to
```

**A destructor frees what the struct does not own, and nothing else.**
Never release a `string` field by hand before `heap.free`; that is a double
free. `core.set_owned` is the one place a string is released by hand,
because reassignment has to drop the previous value before taking the new
one. (Until aether 0.627 the compiler tracked ownership only for
assignments against a local pointer, and this codebase carried a
`free_owned` helper to work around it; that was fixed in
[aether#1866](https://github.com/aether-lang-dev/aether/issues/1866) and
the workaround is gone.)

A `@heap` string returned through a wrapper is freed at the wrapper's
exit, so a string that has to outlive the call that made it is built at
the call site, not returned from a helper.

A local that holds a heap string releases it when it is reassigned or
goes out of scope, so `text = string.replace_all(text, a, b)` is the
whole idiom: a helper that also frees its argument frees it twice. A
struct that keeps a string a caller handed it takes its own copy
(`string.copy`), because the caller's local goes with the caller; a
field assigned from a local in the same function (`mk.name = key`) is a
move and needs no copy. `list.add` takes its own reference to a string
(`list_add_string_owned`) and `list.free` releases it; `list.add_raw`
borrows.

A string's bytes are not addressable from Aether: a literal is a C string
and a string off the heap is an `AetherString`, a header and then its
bytes, and an `extern` declared with a `string` parameter is handed
whichever the caller has. To get at the bytes of a string that may be
either -- `zlib.deflate`'s answer, a chunk `tcp.read_n` received -- copy it
into a `std.bytes` buffer with `bytes.copy_from_string`, which reads both
kinds, and take `bytes.data`. The other direction is
`bytes.string_from_ptr`. Declaring `memcpy` with a `string` source copies
the header of a heap string and is how every snapshot was once a PNG
whose data began with a magic number.

## Names

- `in`, `self` and `callback` are reserved; the engine uses `keys`, `who`,
  `body`.
- A local or a parameter named like an imported module shadows it for
  the rest of the function: no locals called `bytes`, `mesh`, `parallel`,
  `blob`, `native`, `data` in a function that uses the module.
- A local named `floor` shadows libm's and misattributes the error to
  something else.
- A function parameter named `run` hijacks a program's `run`
  ([aether#2130](https://github.com/aether-lang-dev/aether/issues/2130));
  the parallel loops' callbacks are named `body`.
- Two structs with the same name in different modules silently merge
  ([aether#2129](https://github.com/aether-lang-dev/aether/issues/2129)),
  so a module's private structs carry its prefix: `HordeStepJob`,
  `WeatherStepJob`, `JobsFixedTask`, `NavSteerJob`.
- Functions are never named `spawn_*`
  ([aether#2126](https://github.com/aether-lang-dev/aether/issues/2126)).

## Types and casts

- There are no typed locals; a local's type is its initialiser's. A
  module-level `var` takes a constant initialiser and a type:
  `var g_last_size: long = 0`.
- Casts are strict and explicit: `(x as long) * (1597334677 as long)`,
  `n as int`. Integer flags are `int`, not `bool`, where they cross to C.
- No exponent literals: `1000000000.0`, not `1e9`.
- No 32-bit float ([aether#2134](https://github.com/aether-lang-dev/aether/issues/2134)).
  Every buffer a GPU reads is float32, and writing one from Aether is a
  runtime call per element (`std.mem.set_float32`), which is why the
  renderers' upload loops, the mesh store, the skin palettes and the mesh
  file reader are still C. `std.mem`'s byte accessors are runtime calls
  too: a hot loop over bytes is written over `int` arrays (the flow field's
  marks: as bytes the flood took twice as long).
- A string does not cast to a pointer or back; see above.
- A raw C function pointer is called through a cast:
  `sym as fn(ptr, ptr, float) -> void` (how `ae3d.script` calls into a
  loaded library). The other way, an Aether function handed to C as a
  callback, is a `@c_callback("name")` function beside an `extern name(...)`
  declaration, passed as `(&name) as ptr`: an Aether function value is a
  closure, which is not what a C caller expects (`ae3d.platform`'s GLFW
  callbacks). Such a callback touches no string and no `std.mem`: a script
  is a shared library of Aether that links no runtime of its own, and on
  Windows a shared library resolves everything at its link.

## Modules and the standard library

- `import x as y` works only for the standard library.
- `os.platform()` answers `"windows"`, `"darwin"` or `"linux"`;
  `os.exec` and `os.run_capture` run a command (the shader generator's
  `glslc`).
- `std.worker.run_detached` starts a fresh thread per call; a pool is
  `ae3d.jobs`, on aephysics's scheduler.
- `std.tcp.listen` binds every interface and has no poll on a listening
  socket ([aether#2136](https://github.com/aether-lang-dev/aether/issues/2136)),
  which is why the channel's listening side is still C and its asking side
  (`ae3d.probe`) is Aether.
- An `extern` whose name the runtime's headers already declare with other
  types (`setsockopt`, `aether_string_data`) fails the C compile; use the
  standard library's wrapper instead.

## Kernels

A pass over a crowd is written the way the C it replaced was: packed
arrays, a counting sort into a CSR grid where the scan needs neighbours,
one scattered write per element, integers where a byte would be, the
approximate trigonometry the C had. Written that way the Aether runs at
the C's speed (the horde's separation 51.4 ms against 51.5 for half a
million, the flood 8.2 against 8.3), and every port on this branch was
measured that way before the C was deleted. A pass over the job pool is
written so that one thread and many give the same answer, and its test
runs at both.

## Tooling

- The toolchain is `ae` and `aetherc`; `ae cflags` gives the compile flags
  the generated C needs (`-fwrapv` among them, which is what makes `int`
  arithmetic wrap as the reference says).
- On Windows the MSYS2 UCRT64 gcc is the compiler; the WinLibs gcc the
  installer downloads links a different runtime and fails on zlib.
- Profiling on Windows is by `-finstrument-functions` and a sampler
  (aephysics's `scripts/profile.sh`); gprof does not work under MinGW.
