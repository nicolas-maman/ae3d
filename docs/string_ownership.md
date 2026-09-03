# String fields and who frees them

A struct owns its `string` fields. `heap.free` releases them, whether the
assignment was written against a local struct pointer or made through a setter
that takes the pointer as a parameter.

```aether
set_path(e: *Entry, v: string) {
    e.path = core.set_owned(e.path, v)
}

make(v: string) -> *Entry {
    e = heap.new(Entry)
    set_path(e, v)
    return e
}

heap.free(e)
```

That releases `e.path`. Nothing else has to.

## The rule here

**A destructor frees what the struct does not own, and nothing else.** Never
release a `string` field by hand before `heap.free`; that is a double free.
`core.set_owned` is the one place a string is released by hand, because
reassignment has to drop the previous value before taking the new one.

## History

Until aether 0.627.0 the compiler tracked ownership only for assignments written
against a local struct pointer, so a field assigned through a setter leaked while
a field assigned inline double-freed. Neither pattern was safe for both, and this
codebase carried a `free_owned` helper and hand-written destructor releases to
work around it. That is fixed (aether-lang-dev/aether#1866) and the workaround is
gone.
