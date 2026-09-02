# String fields and who frees them

Aether frees a struct's `string` fields inside `heap.free`, but only for
assignments it saw written against a *local* struct pointer. An assignment made
through a pointer *parameter*, which is what a setter function does, is not
tracked, and that field is not freed.

The two halves of that rule pull in opposite directions:

```aether
make(v: string) -> *Entry {
    e = heap.new(Entry)
    e.path = string.copy(v)   // tracked: heap.free(e) releases path
    return e
}
```

```aether
set_path(e: *Entry, v: string) {
    e.path = string.copy(v)   // not tracked: heap.free(e) leaks path
}
```

Freeing the field by hand in the first case aborts with a double free. Not
freeing it in the second case leaks. Neither pattern is safe for both, so a
codebase that mixes them cannot write a correct destructor. Filed upstream as
aether-lang-dev/aether#1866.

## The rule here

**Every `string` field is assigned through a setter that takes the struct
pointer, and every destructor releases it with `core.free_owned`.**

That puts every field on the untracked side of the compiler's rule, so
ownership is uniform: the destructor frees, `heap.free` does not, and nothing is
freed twice. `core.set_owned` releases the previous value on reassignment.

Assigning a string field inline where the struct is allocated is the one thing
that breaks it, because that assignment *is* tracked and the destructor's
`free_owned` then becomes a double free. `renderer_load_texture` in both
backends did exactly that, which aborted the moment a test first freed a
renderer that still held textures.
