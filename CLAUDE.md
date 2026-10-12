# rbtree-lab: project rules

## Commands
- Build and unit tests: `make test`
- Sanitizers: `make asan`
- Valgrind: `make memcheck`
- A change is DONE only when all three pass. Always run them; show the output. If a command is unavailable or fails because of the environment, explain that instead of claiming it passed.
- Always start sessions in plan mode.
- Route all allocation in `src/` through `rb_malloc` and `rb_free`. Do not bypass the allocation seam.
- Keep the Assignment 1 regression suite passing throughout HW2.

## Hard constraints
- NEVER modify `include/rbtree.h`. It is the graded contract.
- Check every allocation. `rb_malloc` can return NULL; handle allocation failure according to the documented contract.
- Failed operations must be failure-atomic: leave the tree unchanged and preserve ownership of the caller's value when the contract requires it.
- Identify the commit point of every operation that allocates. Perform fallible preparation before modifying the tree whenever possible.
- NEVER weaken, skip, or delete a test to make the suite pass. If a test looks wrong, stop and explain why instead.
- Make the smallest change that passes; do not refactor unrelated code.
- Preserve all existing functionality and tests from Assignment 1.
- For the pooled implementation, nodes must use the pool while variable-length key copies continue to use the allocation seam.
- Pool slabs must be 4096 bytes. Respect the specification's allocation, deallocation, alignment, and storage constraints.
- Maintain the pool invariant: `live + free_objs == slabs * objs_per_slab`.
- Pool allocation and deallocation must meet the required O(1) complexity.
- Pool destruction must release every slab, including when live objects remain, as required by the specification.
- Tree teardown must not allocate memory or use recursion. Meet the specified auxiliary-space and stack-size constraints.
- Do not assume sanitizers detect use-after-free inside a slab. Preserve the required debug poisoning behavior for freed pool slots.
- Follow the assignment specification whenever it conflicts with these rules or with the companion guide.

## Style
- C23. `-Wall -Wextra -Werror` must stay clean. No VLAs.
- Error handling: use the goto-cleanup pattern for multi-allocation functions when appropriate.
- Prefer the smallest diff that passes. Do not refactor unrelated code.
- Every non-obvious loop gets a one-line invariant comment.
- Keep allocation and ownership logic in small, focused helper functions.
- Keep slab geometry calculations in one place and test them directly.
- Document alignment, slab layout, free-list ownership, and poisoning decisions where they are implemented.
- Every non-obvious teardown or traversal loop must document its invariant and termination measure.
- Do not introduce recursion or hidden allocations into code that must operate under O(1) auxiliary-space constraints.

## Code map
- `include/rbtree.h`: frozen public interface and contract.
- `src/rbtree.c`: tree operations, rotations, fixups, validation, allocation helpers, teardown, and traversal.
- `src/pool.c`: slab allocation, slot geometry, free-list management, pool statistics, and destruction.
- `tests/test_rbtree.c`: tree unit tests and Assignment 1 regression tests.
- `tests/fuzz.c`: randomized tree operations checked against a reference model.
- `tests/fault_alloc.c`: allocation-failure injection and sweep tests.
- Check the actual repository layout before relying on these paths; do not invent files or move existing code without approval.

## Workflow
- For any multi-file or algorithmic change: propose a plan and wait for approval before editing.
- Start each milestone by rereading the relevant assignment-spec section and reviewing the existing implementation.
- Work in small, independently testable slices. Add or identify the failing test before implementing a fix when practical.
- When debugging, reproduce the failure, identify the responsible function or allocation site, and explain the cause before changing code.
- For allocation failures, verify that the tree, its size, its stored values, and caller ownership remain correct.
- For pool changes, test the pool API independently before debugging through the tree.
- For teardown and traversal changes, test large trees, restoration of the tree structure where required, and the specified stack constraints.
- Commit only from a green state; message format `M<n>: <what>`.
- Get explicit permission from me (the user) to commit.
- Ensure every diff is compact and warn me when it looks too large.
- Verify mutual understanding of a code change or bug fix with me before considering the work complete.
- Never claim a test, sanitizer, fuzzer, or memory check passed unless it was actually run and its result was inspected.

## Testing
- Run the complete test battery after each milestone, not just the new tests.
- Use deterministic seeds for fuzzing and compare behavior against a reference model.
- Fault-injection tests must use a fixed scenario and sweep allocation-failure positions until the scenario completes without further injected failures.
- Verify that each failed operation leaves the tree unchanged and does not incorrectly consume the caller's value.
- Test pool geometry, alignment, allocation, reuse, deallocation, statistics, and destruction with live objects.
- Check the pool invariant throughout pool tests and relevant debug builds.
- Test teardown with a large tree and the required constrained stack. Keep expensive tests configurable so routine memory-check runs remain practical without silently weakening required coverage.
- Never treat one passing test as proof that the entire milestone is correct.

## Process Artifacts
Keep development aligned with the assignment's required process artifacts:
- `CLAUDE.md`
- `PROMPTLOG.md`
- `REFLECTION.md`
- `DEVLOG.md`
- Raw Claude Code `.jsonl` transcripts
- Git history
