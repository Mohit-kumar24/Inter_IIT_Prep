# FJSP Project Comprehensive Context (For LLM / Claude)

This document provides a complete architectural and philosophical breakdown of the Flexible Job-Shop Scheduling Problem (FJSP) ecosystem built in this repository. Use this to quickly understand the design decisions, mathematical models, and structural constraints of the project.

---

## 1. High-Level Architecture
The project strictly isolates concerns into four distinct phases. An algorithm cannot "cheat" because the generator, validator, and algorithm are completely decoupled C++ programs communicating only via standardized JSON files.

- **Phase A (`generator/`)**: Stochastically generates highly realistic, structured FJSP instances.
- **Phase B (`environment/`)**: Provides an independent, zero-trust Validator and a fast $O(N)$ Evaluator.
- **Phase C (`algorithms/`)**: 6 distinct scheduling algorithms (Greedy, Local Search, SA, Tabu, GA, Memetic) aiming to minimize makespan.
- **Phase D (`analysis/` & `experiments/`)**: Batch runs edge cases, outputs Gantt charts via Matplotlib, and computes summary statistics.

---

## 2. Phase A: The Generator (`generator/gen_core.cpp`)
The generator is not a naive uniform-random wrapper. It models real-world factory floor dynamics using C++ `<random>`.

### 2.1 Job Weights & Operation Counts
- Jobs are classified categorically into **Light, Medium, or Heavy** weights.
- The number of operations per job is drawn from a **Poisson distribution** ($\lambda$). Heavy jobs inherently draw a higher $\lambda$, giving them more operations. This mimics complex products taking more steps to assemble.

### 2.2 Flexibility & Machine Selection
- **Flexibility:** How many machines can process a given operation.
- **Positional Decay:** Later operations in a job slightly lose flexibility, mimicking specialized finishing/inspection steps.
- **Machine Selection:** A partial **Fisher-Yates shuffle** picks an exact unbiased subset of eligible machines.

### 2.3 Processing Time Distributions
Rather than using `Uniform` random times, the generator uses specific distributions based on the instance class:
- **LogNormal:** Used for realistic factories. Most times cluster around a median, but right-skewed tails allow for rare, massive "outlier" delays (e.g., severe setup time).
- **Gamma:** Tunable skewness. Used when times must remain strictly positive but heavy-tailed.
- **Normal:** Used for highly predictable, low-variance instances.

### 2.4 Injecting Structural Features (The "Hard" Parts)
1. **Bottlenecks:** With probability $P$, a specific "bottleneck machine" is force-added to an operation's eligible list, and its processing time is scaled down (made faster). This forces a greedy algorithm to pile operations onto this machine, creating severe traffic jams.
2. **Specialist (Machine Advantage):** Certain machines are randomly chosen as specialists. They execute specific operations exponentially faster than standard machines, rewarding algorithms that successfully locate and route to these specialists.
3. **Noise Injection:** A continuous uniform noise multiplier (e.g., $\pm 5\%$) is applied at the very end to all processing times to prevent artificial integer-rounding patterns that an algorithm might exploit.

---

## 3. Phase B: Validation & Evaluation
- **Zero-Trust Validator (`validator.cpp`):** Does not assume operations are listed in order. It independently verifies that operations don't overlap on machines, precedence constraints within jobs are respected, and times match the problem instance exactly.
- **Evaluator API (`evaluator.cpp`):** Algorithms `#include` this C++ file. It operates in strict $O(N)$ memory, evaluating one candidate schedule at a time. It purely calculates makespan and tracks the `best_solution` encountered during the search, without doing any scheduling itself.

---

## 4. Phase C: The Algorithms
To solve the dual problem of **Machine Assignment (MA)** and **Operation Sequencing (OS)**, six algorithms were built:

1. **Greedy (Shortest Processing Time):** A single-pass dispatch rule. Picks the operation that can finish earliest. **Flaw:** Extremely short-sighted; fills bottleneck machines with low-priority tasks, stalling critical path operations.
2. **Local Search (LS):** Takes the Greedy solution and applies random OS (topological) and MA (machine) swaps. **Flaw:** Gets stuck in deep local minima easily.
3. **Simulated Annealing (SA):** A thermodynamic wrapper over LS. Accepts worse solutions with probability $P = e^{-\Delta / T}$. **Flaw:** Static cooling rates can freeze the algorithm before it explores massive $50 \times 10$ spaces.
4. **Tabu Search (TS):** Explores a full neighborhood of size $K$ explicitly, using a Tabu list to prevent cycling back to recent states. **Flaw:** Extremely computationally expensive due to evaluating $K$ neighbors per iteration.
5. **Genetic Algorithm (GA):** Maintains a population of schedules, applying Job-Based Crossover (JBX). **Flaw:** Standard crossover destructively slices tight machine packings in complex schedules, producing offspring worse than both parents. Fails completely on large instances.
6. **Memetic Algorithm:** Combines GA (global) with LS (local exploitation). Extremely slow but effectively patches the GA's destructiveness.

---

## 5. Phase D: Edge-Case Failure Analysis
We ran the algorithms against 12 edge-case presets (e.g., `extreme`, `bottleneck_heavy`, `low_flexibility`).
- **The Flexibility Floor:** On `low_flexibility` (15% flexibility), every meta-heuristic converged to the exact same makespan ($419.0$). This proved a structural boundary: when flexibility is near zero, the FJSP devolves into a classic JSP, and a single dominant critical path mathematically dictates the minimum possible makespan, regardless of algorithmic capability.
- **SA & Tabu Dominance:** SA and Tabu consistently dominated across highly flexible and high-variance environments by meticulously packing schedules to avoid idle gaps.
