# Generated continuation corpus inventory

Run from the repository root (Python standard library only):

```sh
python3 -m unittest discover -s tests/continuation_audit -v
python3 scripts/audit_continuations.py --summary --output docs/P1.5-CORPUS-AUDIT.json
python3 scripts/audit_continuations.py --output /tmp/continuation-audit-full.json
```

The full report includes every function's source file, counters and generated local names. The summary records a SHA-256 of sorted C filenames plus their bytes, making stale inventories detectable. `--corpus` selects another generated directory. No generator or runtime edits, corpus regeneration, compiler, ROM or game execution are involved.

Function feature counts overlap. `exclusive_call_profiles` alone partitions all functions into direct only, indirect only, both, and no emitted C calls. Each instruction site is deduplicated by PC **within a function**; repeated delay-slot comments do not inflate MIPS instruction/branch counts. Emitted C call occurrences are a separate population. Direct/indirect regular/tail C call categories partition recognized call occurrences. Tail detection means the emitted call is immediately followed by `return;`, ignoring comments. `recomp_entrypoint` contains the corpus's one indirect tail call. Backedges require an in-function numeric or `L_` address target <= the branch PC: these are candidate edges, not natural-loop counts or proven reachable loops.

The current generated locals are `hi`, `lo`, `result`, and `c1cs` in all 1,981 functions, plus 84 distinct `jr_addend_*` locals. Declaration counts do not establish value liveness across a suspension. HI/LO counters use instruction opcodes rather than counting unconditional boilerplate declarations. FP context, `CHECK_FR`, double accesses, `f_odd`, and `c1cs` uses are syntactic inventories; they do not establish runtime FR mode, FP semantics, or sufficient serialized state.

Metadata currently has two section-table entries, 2,035 function entries (including external/HLE symbols), `num_sections = 266`, and overlay mapping `[-1]`. Thus 266 is not an active-overlay count, and metadata function entries are not the 1,981 emitted C function definitions.

Growth estimates are explicitly adjustable source-byte assumptions:

`additional bytes = function_count * bytes_per_function + candidate_sites * bytes_per_site`

Defaults are 128 bytes per function and 64 bytes per candidate. Entry-only uses no additional instruction sites; link-and-backedge uses their union; all-transfers includes links, branches, jumps and returns. Each transfer boundary must be interpreted **after its delay slot**, not between the transfer and delay instruction. Legal suspension, FP/HI/LO/local spills, persistent frames, switch lowering, runtime helpers and optimized machine-code sizes require independent design and measurement. These estimates are not measured transformed output or performance predictions.

Regexes target this generated format rather than arbitrary C or MIPS assembly. Fixtures cover duplicate delay comments, backward label branches, emitted regular/indirect-tail calls, switch scratch locals, FP/HI/LO markers, missing delay slots, external targets and conflicting duplicate comments. They test audit logic, not continuation semantic correctness. Unsupported generated syntax needs parser updates; a future instruction format must be reviewed before interpreting its counts.
