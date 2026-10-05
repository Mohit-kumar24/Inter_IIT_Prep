# FJSP Generator: Current Design and Assumptions

This document describes the implementation currently in
[`generator/gen_core.cpp`](../generator/gen_core.cpp), not every design
proposal retained in the archive. The C++ generator is the source of truth for
the generation order, formulas, preset values, schema, and validation checks.
The Python CLI in `generator/generate_instances.py` provides single,
batch, and statistics commands; plotting is optional and uses
`generator/visualize_generated_instances.py`.

## Architecture and trade-offs

| Component | Responsibility | Advantages | Costs and limitations |
| --- | --- | --- | --- |
| `gen_core.cpp` | Parse arguments, apply presets, sample roles/jobs/operations, validate, and emit output | One seeded C++ pipeline; no external C++ distribution library; supports the checked-in 1,000-job stress instance | Parameter validation is incomplete: beyond positive job/machine counts and valid processing-time endpoints, not every numeric range or cross-parameter constraint is checked |
| `distributions.h` | Wrap C++ standard-library distributions and `std::mt19937_64`; sample subsets | Random draws share one explicitly seeded engine; distribution logic is centralized | `std::*_distribution` algorithms can vary between standard-library implementations; the same seed does not guarantee byte-identical output across toolchains |
| `fjsp_instance.h` | Hold instance data, validate structural invariants, format JSON and Brandimarte-style text | Keeps data model, validation, and serialization separate from sampling; can emit JSON, text, or both | JSON is hand-serialized rather than produced by a JSON library; additions to string-valued metadata need correct escaping |
| `generate_instances.py` | Invoke the C++ binary for single/batch generation and print statistics | Convenient batch naming and standard-library-only orchestration | Requires a compiled `gen_core`; it does not replace or independently validate the C++ sampling logic |
| `visualize_generated_instances.py` | Render optional instance dashboards | Makes eligibility and processing-time structure easier to inspect | Requires Matplotlib; plots are a view of generated data, not a validity check |

The historical V1/V2/V3 implementation files described in old notes are not
present in the current generator directory. Those version-by-version
comparisons are design history, not evidence of code that can still be built
or benchmarked. The current generator is the `gen_core_v1` implementation
described below.

## Instance model and output

The C++ model consists of jobs, ordered operations, and for each operation a
list of eligible `(machine_id, processing_time)` pairs. The JSON output uses
string IDs (for example, `J_01`, `M_2`, and `O_01_1`), carries instance
metadata including seed, class, generator version, parameters, and machine
roles, and preserves precedence through operation-array order. Each job
record also contains its weight class and operation count; each operation
contains its eligible-machine count. The text format is Brandimarte-style: a
job/machine count followed by each job's
operation count, eligibility count, and machine/time pairs. The text format
does not carry the JSON metadata.

IDs are formatted with a width derived from the corresponding maximum count.
JSON operation IDs are one-indexed within each job. The C++ data model keeps
operation indices zero-indexed internally and formats them at output time.

## Generation pipeline

The order matters because downstream random draws depend on earlier role and
job choices:

1. Initialize `DistEngine` with the supplied 64-bit seed.
2. Select bottleneck machines (only when both bottleneck count and
   bottleneck probability are positive) and specialist machines (when
   advantage probability is positive).
3. Draw each job's weight class: light, medium, or heavy.
4. For each job, draw an operation count from a Poisson distribution with a
   weight-adjusted mean and clamp it to `[1, ops_max]`.
5. For each operation, calculate position and effective flexibility; round
   the resulting fraction of machines to an eligibility count, clamped to
   `[1, num_machines]`; sample that many distinct machines by partial
   Fisher--Yates shuffle.
6. With the configured probability, add a selected bottleneck machine to
   the operation's eligible set if it is not already present.
7. For every eligible machine, sample a base processing time and apply the
   job, position, machine-role, specialization, and noise transformations
   described below.
8. Sort each operation's eligible machine list by machine ID, assemble
   metadata, validate the complete instance, then serialize it.

All random choices are made by the same `DistEngine`, whose engine is
`std::mt19937_64`. C++ standard-library distribution transforms are not
guaranteed to be bit-for-bit portable between different library vendors or
versions; reproducibility should therefore record the compiler and standard
library as well as the seed and parameters.

The bottleneck subset size is clamped to `[0,num_machines]`. When
`advantage_prob > 0`, the specialist subset size is
`max(1,round(0.3*num_machines))`; otherwise it is empty. A machine can be
selected for both roles.

## Sampling formulas

Let `w_j` be a job weight in `{-1,0,+1}` for light, medium, and heavy,
respectively. A categorical sampler uses probabilities
`max(0.05, 0.3 - job_weight_variance*0.15)`,
`max(0.05, 1 - p_light - p_heavy)`, and
`max(0.05, 0.3 + job_weight_variance*0.15)`. The categorical helper
normalizes these values before drawing.

The operation-count Poisson mean is `ops_mean*0.6` for light jobs,
`ops_mean` for medium jobs, and `ops_mean*1.5` for heavy jobs. Its result is
clamped to `[1, ops_max]`.

For an operation at zero-based position `k` in a job with `q` operations,
the position ratio is `r = k/(q-1)` when `q > 1`, otherwise `r=0`.
Effective flexibility is

```text
clamp(flexibility - 0.05*w_j - 0.08*r + Normal(0, flex_variance), 0.01, 1.0)
```

The eligibility count is `min(num_machines, max(1, round(effective_flex *
num_machines)))`. Partial Fisher--Yates chooses a subset without replacement.
Bottleneck injection can only add an eligible machine; it does not remove one.

For each eligible machine, the base processing time uses the selected
distribution, where `median=(pt_min+pt_max)/2` and
`half_range=(pt_max-pt_min)/2`:

| Distribution | Implemented sampling rule before rounding/clamping |
| --- | --- |
| `lognormal` | Log-normal with `mu=log(median)` and `sigma=0.1+0.9*pt_variance`; this centers the median, not the mean |
| `normal` | Normal with mean `median` and standard deviation `half_range*pt_variance*0.5` |
| `gamma` | Gamma shape `alpha=1+(1-pt_variance)*10` and scale `beta=median/alpha` |
| `uniform` | Uniform over `[median-half_range*pt_variance, median+half_range*pt_variance]` |

The implementation's final `else` branch uses the uniform sampler, so an
unrecognized custom `pt_distribution` string also falls back to uniform
instead of causing a CLI error. This is another reason to validate
distribution names before generation.

The base draw is rounded and clamped to `[pt_min, pt_max]`. It is then
multiplied by the job factor `1 + w_j*job_weight_variance*0.3` and positional
factor `1 + 0.15*r`. Bottleneck machines multiply it by
`bottleneck_strength`. For each eligible specialist machine independently,
the `advantage_prob` Bernoulli draw may apply `advantage_strength`. Finally,
uniform multiplicative noise in `[1-noise_level, 1+noise_level]` is applied
when noise is positive. The transformed value is rounded and clamped to
`[1, pt_max]`.

Consequently, `pt_min` bounds the **base** draw, but a speed multiplier can
make a final processing time lower than `pt_min`. The final output is still a
positive integer and does not exceed `pt_max`. Bottleneck/specialist
transforms are speed advantages, so they can make machines more attractive
and increase contention; the current model does not create bottlenecks by
making those machines slower.

## Preset parameter map

The table records the exact values assigned by `apply_class_preset`. The
`flex` and `pt-var` columns are means/spread controls, not guaranteed observed
instance statistics. Eligibility, durations, and roles remain random draws
for a seed.

| Class | Ops mean / max | Flex / SD | PT min--max | PT law / variance | Bottleneck p / scale / count | Advantage p / scale | Job-weight variance | Noise |
| --- | ---: | ---: | ---: | --- | --- | --- | ---: | ---: |
| `average` | 5 / 10 | 0.50 / 0.10 | 1--30 | lognormal / 0.50 | 0.10 / 0.70 / 1 | 0.10 / 0.40 | 0.30 | 0.05 |
| `easy` | 3 / 6 | 0.80 / 0.05 | 1--20 | normal / 0.20 | 0 / 1.00 / 0 | 0 / 1.00 | 0.10 | 0.03 |
| `hard` | 7 / 15 | 0.25 / 0.15 | 1--60 | lognormal / 0.80 | 0.30 / 0.40 / 2 | 0.25 / 0.25 | 0.50 | 0.08 |
| `extreme` | 10 / 20 | 0.15 / 0.20 | 1--100 | gamma / 1.00 | 0.50 / 0.25 / 3 | 0.35 / 0.15 | 0.70 | 0.12 |
| `bottleneck_heavy` | 5 / 10 | 0.40 / 0.10 | 5--40 | lognormal / 0.50 | 0.70 / 0.35 / 1 | 0 / 1.00 | 0.30 | 0.05 |
| `high_flexibility` | 5 / 10 | 0.90 / 0.05 | 1--30 | lognormal / 0.50 | 0.10 / 0.70 / 1 | 0.20 / 0.40 | 0.20 | 0.05 |
| `low_flexibility` | 5 / 10 | 0.12 / 0.05 | 1--30 | lognormal / 0.50 | 0.10 / 0.70 / 1 | 0 / 1.00 | 0.20 | 0.05 |
| `unbalanced` | 5 / 15 | 0.40 / 0.15 | 1--50 | gamma / 0.70 | 0.20 / 0.55 / 2 | 0.10 / 0.35 | 0.80 | 0.10 |
| `high_variance` | 5 / 10 | 0.50 / 0.10 | 1--100 | lognormal / 1.00 | 0.10 / 0.70 / 1 | 0.15 / 0.30 | 0.40 | 0.15 |
| `machine_advantage` | 5 / 10 | 0.50 / 0.10 | 5--40 | lognormal / 0.50 | 0 / 1.00 / 0 | 0.65 / 0.18 | 0.30 | 0.05 |
| `large_scale` | 6 / 12 | 0.35 / 0.12 | 1--50 | lognormal / 0.60 | 0.15 / 0.55 / 2 | 0.15 / 0.35 | 0.40 | 0.07 |
| `small_tight` | 3 / 5 | 0.20 / 0.05 | 1--15 | normal / 0.25 | 0 / 1.00 / 0 | 0 / 1.00 | 0.10 | 0.03 |

The number of jobs, machines, and seed are separate inputs. With a named
class, class parameters are applied after argument parsing and therefore
override supplied structural parameter values; the job/machine counts and
seed are retained. Without a class, the core parameters must be explicitly
supplied, while the optional extended parameters use defaults.

## Validity argument and boundary

For accepted positive job/machine counts and sane generation parameters:

1. Each job's operation count is clamped to at least one.
2. Each operation's eligibility count is clamped to `[1,m]`; the subset
   contains distinct machine IDs from `[1,m]`; bottleneck injection avoids
   duplicates.
3. Base and final processing times are rounded/clamped to positive integers.
4. The nested job/operation loops produce contiguous internal job IDs and
   operation indices; the operation vector order defines the precedence
   chain.
5. `FJSPInstance::validate()` checks the job count/IDs, nonempty operation
   lists, operation ownership and index, nonempty eligibility, machine range,
   positive processing times, duplicate machine choices, and metadata
   operation count before serialization. It also rejects zero jobs or
   machines and a mismatch between the jobs vector and declared job count.
6. Since every operation has at least one eligible machine, a serial schedule
   that processes all operations in job order is feasible: choose any eligible
   machine and run one operation at a time.

This is a proof of the generated instance's structural feasibility under
valid parameters, not a proof that every malformed or out-of-range CLI
configuration is rejected safely. Current CLI checks explicitly enforce
positive job/machine counts and `1 <= pt_min <= pt_max`; other parameter
ranges are not comprehensively validated. That should be closed with
parameter-range and cross-field checks plus negative tests before describing
the command line as fully hardened.

## Commands

On Windows, `setup.ps1` can create the local `env/.venv`, install the
declared Python analysis/visualization dependencies, and build the C++ core.
Its `.env` settings can adjust the compiler, build flags, and default batch
parameters. The configuration template is `.env.example`; the generated
`.env` and virtual environment are local-only and excluded from Git.

Build the core from the repository root:

```powershell
g++ -O3 -std=c++17 generator/gen_core.cpp -o generator/gen_core.exe
```

Generate one preset instance, generate batches, or inspect statistics:

```powershell
python generator/generate_instances.py single --jobs 15 --machines 5 --class bottleneck_heavy --seed 42 --output instances/example.json
python generator/generate_instances.py batch --jobs 10 --machines 5 --seeds 1,2,3 --output-dir instances/batch
python generator/generate_instances.py stats --input instances/example.json
```

The equivalent configured batch command is `.\setup.ps1 -Action Generate`;
it writes to `instances/generated_batch` by default. Use
`.\setup.ps1 -Action Build` to build the generator and solver executables.

The class list and checked-in batch inventory are catalogued in
[`instances/README.md`](../instances/README.md). For the implemented
schedule/validator model and measured generator dataset, see the
[project report PDF](report/Report.pdf). Editable Overleaf sources are kept
locally in `docs/report/`; Git intentionally excludes those sources and
generated tables while preserving the compiled PDF.
