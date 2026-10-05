# Flexible Job-Shop Scheduling Problem (FJSP)

A high-performance benchmark, validation, and optimization suite for the **Flexible Job-Shop Scheduling Problem (FJSP)**.

This repository implements a modular, reproducible pipeline:
```
Instance Generation ──► Independent Validation ──► Algorithmic Solvers ──► Stress Testing & Analysis
```

> **Note:** Current generator mechanics and parameter presets are documented in [`docs/GENERATOR_DESIGN.md`](docs/GENERATOR_DESIGN.md). The implemented problem, validator, and evaluator assumptions are summarized in [`docs/PROJECT_ASSUMPTIONS.md`](docs/PROJECT_ASSUMPTIONS.md). The finished report PDF is [`docs/report/Report.pdf`](docs/report/Report.pdf). The editable Overleaf sources remain in the local `docs/report/` folder but are intentionally excluded from Git; upload that local folder to Overleaf when source-level editing is needed.

---

## 📁 Repository Structure

```text
fjsp/
├── generator/            # Phase 1: Instance generator logic & orchestrator
├── environment/          # Phase 2: Strict validator/evaluator (C++) & metrics automation
├── algorithms/           # Phase 3: Scheduling heuristics (Greedy, LS, SA, Tabu, GA, Memetic) & visualizer
├── experiments/          # Phase 4: Auto-generated test cases & hand-built edge instances
├── analysis/             # Phase D: Experiment CSVs, Gantt chart images
├── instances/            # Generic JSON instances for standard testing
├── results/              # Computed scheduling JSON outputs
├── docs/                 # Detailed architectural documentation, reports & failure analysis
├── requirements.txt      # Python dependencies (analysis/visualization)
├── .env.example          # Local workflow configuration template
├── setup.ps1             # Windows setup, build, generation, and Part D helper
└── README.md             # This file (Project Overview)
```

---

## 🛠️ Environment Setup

### 1. Python Virtual Environment

Use the helper script to create `env/.venv`, install the Python packages, and
copy `.env.example` to a local `.env` file:

```powershell
.\setup.ps1 -Action Setup
```

The requirements file lists the direct third-party imports used in the
project: NumPy and pandas for analysis, and Matplotlib for plots and schedule
visualization. The generator, experiment runner, LaTeX exporter, and hand-built
instance utility otherwise use the Python standard library. Edit `.env` to
change compiler flags, experiment timeout, or the generator batch defaults;
it contains no credentials. The local virtual environment and `.env` are
excluded from Git.

The same script can build all C++ programs, generate a batch, run Part D,
export the report tables, or perform the full setup/build/Part-D/export
workflow:

```powershell
.\setup.ps1 -Action Build
.\setup.ps1 -Action Generate
.\setup.ps1 -Action PartD
.\setup.ps1 -Action Export
.\setup.ps1 -Action All
```

The `All` action uses the already checked-in `experiments/` inputs; it does not
regenerate or overwrite those curated instances. `Generate` writes to
`instances/generated_batch` by default. To skip package installation after
creating the environment, use `.\setup.ps1 -Action Setup -SkipInstall`.

### 2. C++ Compiler Requirements
* Requires a **C++17** compatible compiler (e.g., `g++` 8+, Clang, or MSVC).
* No external package managers required — header libraries like `json.hpp` are self-contained.

---

## 🚀 How to Run (Completed Work)

### Phase 1: Instance Generator (`generator/`)

The generator synthesizes controllable FJSP instances across 12 named classes (`average`, `easy`, `hard`, `extreme`, `bottleneck_heavy`, `high_flexibility`, `low_flexibility`, `unbalanced`, `high_variance`, `machine_advantage`, `large_scale`, and `small_tight`). Jobs and machines are specified independently; each class sets structural generation parameters.

#### 1. Compile the C++ Generator Core
```powershell
g++ -O3 -std=c++17 generator/gen_core.cpp -o generator/gen_core.exe
```

#### 2. Generate a Single Instance
Generate an instance with custom parameters:
```powershell
python generator/generate_instances.py single --jobs 15 --machines 5 --class bottleneck_heavy --seed 42 --output instances/test_instance.json
```

#### 3. Run a Batch Suite with Plots
Generate each configured class for the selected seeds. The orchestrator uses `--classes` and `--plot`; it does not define a named `standard` batch:
```powershell
python generator/generate_instances.py batch --jobs 10 --machines 5 --seeds 1,2,3 --output-dir instances/generated_batch --plot
```
* **Output instances:** `instances/generated_batch/<class>_j10_m5_s<seed>.json`
* **Distribution plots:** `instances/generated_batch/images/<instance_name>_dashboard.png`

To inspect an existing instance, use `python generator/generate_instances.py stats --input instances/test_instance.json`.

---

### Phase 2: Independent Schedule Validator (`environment/`)

The validator is a strict, independent referee for proposed schedules. It **never trusts the solver** and verifies that any candidate solution satisfies all physical and logical constraints of the problem statement.

#### Rules Enforced:
1. **Machine Eligibility:** Every operation is executed on a machine from its eligible set.
2. **Exact Processing Times:** $C_{ij} - S_{ij} = \text{processing\_time}(M_k)$.
3. **Precedence Constraints:** Operations within each job are executed strictly sequentially ($S_{i, j+1} \ge C_{i, j}$).
4. **Machine Non-Overlap:** No machine processes two operations simultaneously ($S_{b} \ge C_{a}$ or $S_{a} \ge C_{b}$).
5. **Complete & Unique Coverage:** All operations in the instance are scheduled exactly once (no missing, no duplicate ops).
6. **Non-Negative Timing:** $S_{ij} \ge 0$.
7. **Job Integrity:** Operation matches its assigned parent job ID.

#### Schedule JSON Schema Expected:
```json
[
  {
    "job_id": "J_0001",
    "op_id": "O_0001_0001",
    "machine_id": "M_0002",
    "start_time": 0,
    "completion_time": 14
  }
]
```

#### Running the C++ Validator (High-Throughput)
Compile the independent validator:
```powershell
g++ -O3 -std=c++17 environment/validator.cpp -o environment\validator.exe
```
Run validation:
```powershell
.\environment\validator.exe --instance instances/test_instance.json --schedule instances/schedule.json
```

The independent validator is implemented in C++ at
`environment/validator.cpp`; there is no Python validator in the current
checkout. Use the C++ compile/run commands above or `.\setup.ps1 -Action Build`.

#### Exit Codes & Behavior
* **Exit code `0` (VALID):** Outputs overall makespan ($C_{\max}$) and validation confirmation.
* **Exit code `1` (INVALID):** Outputs detailed, numbered diagnostics identifying exact offending jobs, machines, time overlaps, or missing operations.

### Schedule Evaluator (`environment/evaluator.cpp`)

The evaluator is the bridge between the scheduling algorithm and the validation environment. It processes **one candidate solution at a time**, computes its makespan, and tracks the best solution found so far.

#### Design
* Evaluates a single `Solution` (flat vector of `OpAssignment` structs), validates it fully, and returns its makespan.
* Internally maintains `best_solution` and `best_objective`. When a candidate is better, replaces the best with an O(N) copy.
* **Never stores all candidates** — only current + best exist in memory at any time.
* Operation order formatting/export happens **only once**, after the final best is determined via `export_best()`.
* Completely separate from scheduling/search logic; the algorithm in `algorithms/` drives the evaluator.

#### Compiling the Evaluator
```powershell
g++ -O2 -std=c++17 environment/evaluator.cpp -o environment\evaluator.exe
```

#### Standalone CLI (for testing a single schedule)
```powershell
.\environment\evaluator.exe --instance instances/test_instance.json --schedule instances/schedule.json
```

#### Programmatic API (called from algorithm code)
```cpp
#include "evaluator.cpp"  // or refactor into .h/.cpp

Evaluator eval("instances/test.json");
double obj = eval.evaluate(candidate);  // returns makespan or infinity if invalid
// ... repeat for many candidates from the search ...
eval.export_best("instances/best_schedule.json");
```

### Building the C++ Programs

Each algorithm is a standalone executable and should be compiled separately:
```powershell
g++ -O3 -std=c++17 generator/gen_core.cpp -o generator\gen_core.exe
g++ -O3 -std=c++17 algorithms/greedy_spt.cpp -o algorithms\greedy_spt.exe
g++ -O3 -std=c++17 algorithms/local_search.cpp -o algorithms\local_search.exe
g++ -O3 -std=c++17 algorithms/simulated_annealing.cpp -o algorithms\simulated_annealing.exe
g++ -O3 -std=c++17 algorithms/tabu_search.cpp -o algorithms\tabu_search.exe
g++ -O3 -std=c++17 algorithms/genetic_algorithm.cpp -o algorithms\genetic_algorithm.exe
g++ -O3 -std=c++17 algorithms/memetic_algorithm.cpp -o algorithms\memetic_algorithm.exe
g++ -O3 -std=c++17 environment/validator.cpp -o environment\validator.exe
g++ -O3 -std=c++17 environment/evaluator.cpp -o environment\evaluator.exe
```

Run the experiment driver after building the executables:
```powershell
python environment/run_legacy_metrics.py
```
This writes the summary CSV and Gantt charts under `analysis/`. Failed or timed-out runs are recorded with an infinite makespan and do not reuse a stale schedule.

### Python Program Index

Python utilities remain next to the project component they support. Their
names describe their task so the entry points are discoverable:

| Program | Purpose |
| --- | --- |
| `generator/generate_instances.py` | Generate single instances/batches or print instance statistics. |
| `generator/visualize_generated_instances.py` | Render optional instance dashboards; imported by the generator CLI for `--plot`. |
| `experiments/generate_hand_built_instances.py` | Recreate the ten checked-in hand-built edge-case inputs in `experiments/`. Running it overwrites matching JSON fixtures. |
| `environment/run_part_d_experiments.py` | Run the current experiment matrix with independent C++ validation. |
| `environment/export_report_tables.py` | Refresh packaged CSV and LaTeX report tables from Part D results. |
| `environment/run_legacy_metrics.py` | Older unvalidated metrics/Gantt runner retained for compatibility; use the Part D runner for current results. |
| `algorithms/visualize_schedule.py` | Render one solver schedule as a Gantt chart. |
| `analysis/plot_experiment_metrics.py` | Plot the older `experiment_summary.csv` metrics. |

For current Part D results, use `.\setup.ps1 -Action PartD` followed by
`.\setup.ps1 -Action Export`; the older metrics and plotting utilities are
separate paths and do not produce the current Part D evidence.

## 🗺️ Detailed Documentation

For the detailed, source-reviewed technical report (including per-algorithm assumptions, complexity, bottlenecks, trade-offs, benchmark limitations, and recommendations), see [`docs/report/Report.pdf`](docs/report/Report.pdf). The complete editable report source package is present in a local checkout under `docs/report/` but is intentionally ignored by Git except for the PDF.

The local LaTeX package uses its own preamble, settings, section sources, and data files, so it can be uploaded as a self-contained Overleaf project.

To refresh the Part D run table and its Overleaf data package:
```powershell
python environment/run_part_d_experiments.py --timeout 180
python environment/export_report_tables.py
```

The LaTeX report is a source-reviewed project assessment with packaged Part D evidence. The generator and model assumptions also remain available as focused developer notes in `docs/`.
