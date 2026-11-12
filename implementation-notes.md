# Implementation Notes — Immutable Lisp

**Non-normative.** This file gives advice for implementers. Nothing in it is a requirement or is tested: the specification (`core-specification.md`, `standard-library.md`) and the conformance files are the only reference. An implementation that ignores this file but passes the conformance cases is correct.

## 1. Memory

The core requires only what can be observed: tail calls in constant memory (core §8.4), and lists of at least 1,000,000 elements that can be built, traversed and discarded without error (core §8.6). How memory is managed is left to the implementation. A host language with a garbage collector can rely on it.

**Reference counting is enough.** An implementation that manages memory itself does not need a tracing collector. The heap is acyclic by construction:

- values are immutable, so no value can be changed to point to a value created after it;
- a recursive function calls itself through its global name, resolved at call time (core §8.2), instead of holding a reference to itself. There is no named `lambda` and no local recursion that would make a closure refer to itself.

So counting references frees everything that becomes unreachable.

**Free long lists iteratively.** Releasing the last reference to a list of a million elements must not recurse once per element, or it overflows the stack. Free the elements in a loop, for example with an explicit worklist of nodes whose count dropped to zero.

## 2. Position table

Positions live in a side table keyed by node identity (core §6). When identity is a memory address, a freed node's address may be reused by a new node, which would then appear to have the old node's position. Core §6.4 forbids this. Some ways to avoid it:

- keep the table alive exactly as long as the datums it describes;
- remove a node's entry when the node is freed;
- use weak references as keys.
