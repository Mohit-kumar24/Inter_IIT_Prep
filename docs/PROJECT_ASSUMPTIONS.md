# Project Model, Validator, and Evaluator Assumptions

This note separates the implemented scheduling model from optional extensions
and records the current validator/evaluator behavior. It replaces the
corresponding merged material in
[`archive/legacy_report.md`](archive/legacy_report.md); historical statements
that cannot be verified in current source are not repeated here.

## Base scheduling model

- Every job is available at time zero and is a fixed, ordered chain of one or
  more operations.
- Each operation is non-preemptive, has one or more eligible machines, and
  has a deterministic positive processing time for each eligible machine.
- A completed operation may wait without a buffer-capacity limit before its
  successor starts; machines may sit idle.
- A schedule selects exactly one eligible machine and one start/completion
  interval per operation. Completion equals start plus the selected
  machine's processing time.
- Job precedence follows the operations' order in the instance JSON. Idle
  time between successive operations is allowed.
- Each machine processes at most one operation at a time. Back-to-back
  intervals are allowed.
- The objective is makespan, the maximum completion time.
- The current model excludes release dates, due dates, setup/transport times,
  machine breakdowns, dynamic arrivals, preemption, and stochastic processing
  times at execution. These may be considered only as explicitly identified
  extensions.

The generator produces JSON instance IDs as strings. The C++ schedule
validator expects a JSON array whose entries contain string `job_id`,
`op_id`, and `machine_id`, plus numeric `start_time` and `completion_time`.

## Independent C++ validator

`environment/validator.cpp` reads the instance and proposed schedule
independently of the solver. For a parseable instance with the expected
schema, it checks:

1. The schedule is an array of objects with the required fields.
2. IDs are strings, operation IDs exist in the instance, and each operation's
   declared job matches its owner.
3. Every instance operation is present exactly once; duplicates and missing
   operations are reported.
4. Times are numeric and finite, starts are nonnegative, and completion is
   not before start.
5. The selected machine is eligible and the scheduled duration differs from
   the instance duration by no more than `1e-6`.
6. Precedence in the instance's array order is respected.
7. Machine timelines do not overlap; equality at an interval boundary is
   valid.
8. The makespan is computed only when all checks pass.

The validator builds job/machine schedule views and tracks job completion
times for its validation report. Checks are phase-ordered and short-circuit
after foundational errors: it
does not attempt precedence/overlap analysis when IDs, completeness, times,
or machine assignments have already failed. The implementation assumes a
parseable, structurally usable instance file; malformed-instance hardening
and dedicated negative tests remain development work.

## Candidate evaluator

`environment/evaluator.cpp` serves the algorithms and is not a replacement
for the independent validator. It loads instance lookups once, accepts a
flat candidate with one assignment per operation, and checks completeness,
operation ownership, finite/nonnegative times, machine eligibility,
duration, precedence, and machine non-overlap. A valid candidate's makespan
is returned; invalid candidates return infinity and diagnostics are written
to standard error. A newly best solution is retained as a full copy, which
costs linear time and space in the number of operations. Algorithms own their
population, tabu state, and other search memory. The retained best candidate
can be converted to a chronological operation order on demand; non-best
candidates do not need this export formatting step.

The internal evaluator uses numerical tolerances (`1e-6` for duration and
`1e-9` for precedence/overlap comparisons). The independent validator is the
acceptance authority for reported experiment rows; its boundary comparisons
are implemented separately, so the two components should continue to be
tested independently.

## Reproducibility and evidence limits

An experiment record should preserve instance seed and parameters, generator
and solver versions, compiler/toolchain, solver seed and budget, hardware,
runtime, and independent validation status. Current Part D has one recorded
run per case/algorithm and source-level fixed solver seeds, so it describes
that configured run matrix; it does not estimate seed robustness, confidence
intervals, or statistical significance. Current solvers do not emit
iteration-wise convergence traces.

See the current-source [generator design](GENERATOR_DESIGN.md) and the
[Overleaf report](report/main.tex) for implementation details and measured
results.
