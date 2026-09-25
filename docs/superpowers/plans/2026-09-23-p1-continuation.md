# P1 continuation prototype implementation plan

> **Historical baseline (2026-09-23).** This is the original P1 plan, preserved as written. The production continuation work and current status are documented in the later phase reports.

Goal: execute an isolated Snowboard Kids generated call chain, serialize a blocked receive, destroy its process, restore in another process, and compare all guest registers and 8 MiB RDRAM with uninterrupted native generated execution.

Scope is the explicitly requested P1 of `docs/NATIVE-SAVESTATES-DESIGN.md`. No application integration, gameplay changes, renderer/audio work, or savestate product format.

1. Add an independent test runner with export/import/reference modes and exact byte comparison. First run must fail because the prototype does not exist.
2. Implement an experimental backend through N64Recomp's `recompile_function_custom` extension. Delegate ordinary instruction semantics to CGenerator; emit owned frames, stable main-section function IDs and call-PC continuation IDs, save all four generated locals, and dispatch without recursive host calls. Reject unsupported calls and jump-table locals.
3. Extract the real `requestControllerPakFreeSpaceUpdate` instructions from the local matching ELF at test build time; generate two synthetic MIPS callers stressing live locals, alongside the ordinary CGenerator baseline. Keep proprietary instructions/output untracked.
4. Add an owned context and explicit scalar codec (including FPR bits and rounding), rebind f_odd, validate frame chains and blocked receive state, and model a single-thread message queue using guest addresses. No host object representations in the payload.
5. Test fresh-process reconstruction, repeated empty polling, exactly-once completion, live-local sensitivity, malformed continuation rejection, generator-order ID stability, and both FR modes. Compile/run with appropriate sanitizer checks.
6. Document evidence, limitations, and upstream changes needed for general code generation and scheduler integration. Review diff to ensure application targets and dependencies are untouched.

The probe is isolated under tests/continuation and uses local pinned N64Recomp sources. It does not replace either installed generator or runtime.
