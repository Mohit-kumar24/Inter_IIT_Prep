# Generated FJSP instances

This folder contains generator-produced instance JSON only. Schedules and
visualizations are stored under `results/` and `analysis/`, respectively.

## Reproducible batches

The checked-in batches were regenerated with the current C++ generator:

```powershell
python generator/generate_instances.py batch --jobs 10 --machines 5 --seeds 1,2,3 --output-dir instances/batch
python generator/generate_instances.py batch --jobs 20 --machines 8 --seeds 1,2,3 --output-dir instances/batch_test
```

Each batch contains 12 generator classes for each of seeds 1, 2, and 3:
`average`, `easy`, `hard`, `extreme`, `bottleneck_heavy`,
`high_flexibility`, `low_flexibility`, `unbalanced`, `high_variance`,
`machine_advantage`, `large_scale`, and `small_tight`.

## Standalone generator outputs

| File | Generator parameters |
| --- | --- |
| `test_average.json` | average, 10 jobs, 5 machines, seed 42 |
| `test_plot.json` | hard, 15 jobs, 6 machines, seed 77 |
| `extreme_8j6m_s77.json` | extreme, 8 jobs, 6 machines, seed 77 |
| `test_extreme_1000.json` | extreme, 1,000 jobs, 50 machines, seed 123 |

These JSON files use the current schema: string IDs, `machine_id`, and
`processing_time`. The prior legacy integer-ID batch schema has been replaced.
The 1,000-job file is a generator stress artifact; it is not included in the
fixed-budget metaheuristic experiment matrix.
