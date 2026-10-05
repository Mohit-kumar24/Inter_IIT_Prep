"""Run the Part D experiment matrix and collect independently checked metrics."""

import argparse
import csv
import json
import math
import re
import subprocess
import time
from collections import defaultdict
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
EXPERIMENTS_DIR = BASE_DIR / "experiments"
ALGORITHMS_DIR = BASE_DIR / "algorithms"
VALIDATOR = BASE_DIR / "environment" / "validator.exe"
RESULTS_DIR = BASE_DIR / "results" / "part_d"
ANALYSIS_DIR = BASE_DIR / "analysis"
SUMMARY_CSV = ANALYSIS_DIR / "part_d_results.csv"
LOG_DIR = ANALYSIS_DIR / "part_d_logs"

ALGORITHMS = (
    "greedy_spt",
    "local_search",
    "simulated_annealing",
    "tabu_search",
    "genetic_algorithm",
    "memetic_algorithm",
)

PURPOSES = {
    "average": "Normal expected behavior",
    "balanced": "Balanced workload distribution",
    "bottleneck": "Generated machine-contention case",
    "bottleneck_hand": "Hand-built single-machine bottleneck",
    "extreme": "Extreme generated processing-time and routing stress",
    "extreme_time_gaps": "Hand-built 1 versus 10000 processing-time alternatives",
    "high_flexibility": "Routing-rich machine-selection decisions",
    "high_variance": "Wide processing-time variation",
    "identical_processing_times": "Tie-heavy symmetric routing",
    "large": "Scale stress",
    "long_critical_chains": "Long precedence chains",
    "low_flexibility": "Constrained routing",
    "machine_advantage": "Generated specialist advantage",
    "machine_advantage_hand": "Hand-built disjoint machine specialization",
    "many_very_short_operations": "Many short operations and high sequencing density",
    "near_total_flexibility": "Near-total machine flexibility",
    "near_zero_flexibility": "Near-zero machine flexibility",
    "one_job": "Single-job precedence-only edge case",
    "single_machine": "Single-machine scheduling edge case",
    "small": "Small-scale correctness and runtime check",
}


def load_instance(path):
    with path.open(encoding="utf-8") as stream:
        instance = json.load(stream)
    jobs = instance.get("jobs")
    if not isinstance(jobs, list) or not jobs:
        raise ValueError(f"{path}: expected a non-empty jobs array")

    op_by_id = {}
    job_sequences = {}
    eligible_machines = set()
    processing_times = []
    minimum_job_work = []
    forced_machine_work = defaultdict(float)

    for job in jobs:
        job_id = job["job_id"]
        sequence = []
        job_work = 0.0
        for operation in job["operations"]:
            op_id = operation["op_id"]
            alternatives = operation["eligible_machines"]
            if not alternatives:
                raise ValueError(f"{path}: operation {op_id} has no eligible machine")
            machines = {}
            for alternative in alternatives:
                machine_id = alternative["machine_id"]
                duration = float(alternative["processing_time"])
                if not math.isfinite(duration) or duration <= 0:
                    raise ValueError(f"{path}: invalid processing time for {op_id}")
                if machine_id in machines:
                    raise ValueError(f"{path}: duplicate machine {machine_id} for {op_id}")
                machines[machine_id] = duration
                eligible_machines.add(machine_id)
                processing_times.append(duration)
            if op_id in op_by_id:
                raise ValueError(f"{path}: duplicate operation ID {op_id}")
            op_by_id[op_id] = (job_id, machines)
            sequence.append(op_id)
            shortest = min(machines.values())
            job_work += shortest
            if len(machines) == 1:
                only_machine = next(iter(machines))
                forced_machine_work[only_machine] += shortest
        job_sequences[job_id] = sequence
        minimum_job_work.append(job_work)

    metadata = instance.get("metadata", {})
    machine_count = int(metadata.get("num_machines", len(eligible_machines)))
    job_count = len(jobs)
    operation_count = len(op_by_id)
    flexibility = sum(
        len(machines) / machine_count
        for _, machines in op_by_id.values()
    ) / operation_count
    lower_bound = max(
        max(minimum_job_work, default=0.0),
        max(forced_machine_work.values(), default=0.0),
    )
    processing_time_mean = sum(processing_times) / len(processing_times)
    processing_time_stddev = math.sqrt(
        sum((duration - processing_time_mean) ** 2 for duration in processing_times)
        / len(processing_times)
    )
    machine_roles = metadata.get("machine_roles", {})
    return {
        "instance": instance,
        "metadata": metadata,
        "op_by_id": op_by_id,
        "job_sequences": job_sequences,
        "machine_count": machine_count,
        "job_count": job_count,
        "operation_count": operation_count,
        "mean_flexibility": flexibility,
        "processing_time_min": min(processing_times),
        "processing_time_max": max(processing_times),
        "processing_time_mean": processing_time_mean,
        "processing_time_stddev": processing_time_stddev,
        "bottleneck_machines": ",".join(machine_roles.get("bottleneck_machines", [])),
        "specialist_machines": ",".join(machine_roles.get("specialist_machines", [])),
        "lower_bound": lower_bound,
    }


def machine_ids_for(instance_data):
    found = {
        machine_id
        for _, alternatives in instance_data["op_by_id"].values()
        for machine_id in alternatives
    }
    machine_count = instance_data["machine_count"]
    if all(re.fullmatch(r"M_\d+", machine_id) for machine_id in found):
        width = max(1, len(str(machine_count)))
        return {f"M_{index:0{width}d}" for index in range(1, machine_count + 1)}
    return found


def schedule_metrics(schedule_path, instance_data):
    with schedule_path.open(encoding="utf-8") as stream:
        schedule = json.load(stream)
    if not isinstance(schedule, list):
        raise ValueError("schedule root must be an array")

    by_operation = {}
    by_machine = defaultdict(list)
    for entry in schedule:
        op_id = entry["op_id"]
        if op_id in by_operation:
            raise ValueError(f"duplicate operation in schedule: {op_id}")
        start = float(entry["start_time"])
        completion = float(entry["completion_time"])
        if not math.isfinite(start) or not math.isfinite(completion):
            raise ValueError(f"non-finite time for {op_id}")
        by_operation[op_id] = entry
        by_machine[entry["machine_id"]].append(entry)

    if set(by_operation) != set(instance_data["op_by_id"]):
        raise ValueError("schedule operation coverage does not match the instance")

    makespan = max((float(entry["completion_time"]) for entry in schedule), default=0.0)
    job_end = {}
    for job_id, sequence in instance_data["job_sequences"].items():
        previous_completion = 0.0
        for op_id in sequence:
            entry = by_operation[op_id]
            if float(entry["start_time"]) < previous_completion - 1e-9:
                raise ValueError(f"precedence violation in {job_id} at {op_id}")
            previous_completion = float(entry["completion_time"])
        job_end[job_id] = previous_completion

    busy_time = defaultdict(float)
    machine_predecessor = {}
    for machine_id, entries in by_machine.items():
        entries.sort(key=lambda item: (float(item["start_time"]), float(item["completion_time"])))
        for index, entry in enumerate(entries):
            busy_time[machine_id] += float(entry["completion_time"]) - float(entry["start_time"])
            if index:
                previous = entries[index - 1]
                if float(entry["start_time"]) < float(previous["completion_time"]) - 1e-9:
                    raise ValueError(f"machine overlap on {machine_id}")
                machine_predecessor[entry["op_id"]] = previous["op_id"]

    predecessors = defaultdict(set)
    for sequence in instance_data["job_sequences"].values():
        for previous, current in zip(sequence, sequence[1:]):
            predecessors[current].add(previous)
    for current, previous in machine_predecessor.items():
        predecessors[current].add(previous)

    path_end = {}
    path_parent = {}
    for entry in sorted(schedule, key=lambda item: (float(item["start_time"]), float(item["completion_time"]))):
        op_id = entry["op_id"]
        duration = float(entry["completion_time"]) - float(entry["start_time"])
        prior = max(predecessors[op_id], key=lambda candidate: path_end[candidate], default=None)
        path_end[op_id] = duration + (path_end[prior] if prior is not None else 0.0)
        path_parent[op_id] = prior
    critical_end_op = max(path_end, key=path_end.get, default=None)
    critical_path = path_end.get(critical_end_op, 0.0)
    critical_operations = []
    cursor = critical_end_op
    while cursor is not None:
        critical_operations.append(cursor)
        cursor = path_parent[cursor]
    critical_operations.reverse()

    machine_ids = machine_ids_for(instance_data)
    utilization = {
        machine_id: busy_time[machine_id] / makespan if makespan else 0.0
        for machine_id in machine_ids
    }
    lower_bound = instance_data["lower_bound"]
    return {
        "makespan": makespan,
        "lower_bound": lower_bound,
        "lower_bound_gap_percent": (
            100.0 * (makespan - lower_bound) / lower_bound
            if lower_bound > 0
            else 0.0
        ),
        "critical_path": critical_path,
        "critical_path_operations": critical_operations,
        "machine_utilization_mean_percent": (
            100.0 * sum(utilization.values()) / len(utilization)
            if utilization
            else 0.0
        ),
        "machine_utilization_max_percent": (
            100.0 * max(utilization.values(), default=0.0)
        ),
        "machine_utilization_by_machine": utilization,
    }


def write_failure_log(log_path, message):
    log_path.write_text(message.strip() + "\n", encoding="utf-8")


def run_experiments(instance_paths, timeout):
    if not VALIDATOR.exists():
        raise FileNotFoundError(
            f"Independent validator not found: {VALIDATOR}. Build environment/validator.cpp first."
        )
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)

    columns = [
        "instance", "instance_type", "purpose", "algorithm", "algorithm_seed",
        "feasible", "makespan", "runtime_seconds", "jobs", "machines",
        "operations", "mean_flexibility", "processing_time_min", "processing_time_max",
        "processing_time_mean", "processing_time_stddev", "bottleneck_machines",
        "specialist_machines", "lower_bound", "lower_bound_gap_percent",
        "critical_path_length", "critical_path_operations",
        "machine_utilization_mean_percent", "machine_utilization_max_percent",
        "convergence_trace", "schedule_path", "diagnostic",
    ]
    with SUMMARY_CSV.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=columns)
        writer.writeheader()
        for instance_path in sorted(instance_paths):
            instance_data = load_instance(instance_path)
            name = instance_path.stem
            class_name = instance_data["metadata"].get("instance_class", name)
            purpose = PURPOSES.get(name, PURPOSES.get(class_name, "Scale/robustness case"))
            instance_type = (
                "hand-built edge case"
                if name in {
                    "bottleneck_hand", "extreme_time_gaps", "identical_processing_times",
                    "long_critical_chains", "machine_advantage_hand",
                    "many_very_short_operations", "near_total_flexibility",
                    "near_zero_flexibility", "one_job", "single_machine",
                }
                else "generated experiment instance"
            )
            for algorithm in ALGORITHMS:
                executable = ALGORITHMS_DIR / f"{algorithm}.exe"
                schedule_path = RESULTS_DIR / f"{name}_{algorithm}.json"
                log_path = LOG_DIR / f"{name}_{algorithm}.log"
                schedule_path.unlink(missing_ok=True)
                start = time.perf_counter()
                row = {
                    "instance": name,
                    "instance_type": instance_type,
                    "purpose": purpose,
                    "algorithm": algorithm,
                    "algorithm_seed": "42 (fixed default)" if algorithm != "greedy_spt" else "not applicable",
                    "feasible": False,
                    "makespan": "",
                    "runtime_seconds": "",
                    "jobs": instance_data["job_count"],
                    "machines": instance_data["machine_count"],
                    "operations": instance_data["operation_count"],
                    "mean_flexibility": f"{instance_data['mean_flexibility']:.6f}",
                    "processing_time_min": f"{instance_data['processing_time_min']:.6f}",
                    "processing_time_max": f"{instance_data['processing_time_max']:.6f}",
                    "processing_time_mean": f"{instance_data['processing_time_mean']:.6f}",
                    "processing_time_stddev": f"{instance_data['processing_time_stddev']:.6f}",
                    "bottleneck_machines": instance_data["bottleneck_machines"],
                    "specialist_machines": instance_data["specialist_machines"],
                    "lower_bound": f"{instance_data['lower_bound']:.6f}",
                    "lower_bound_gap_percent": "",
                    "critical_path_length": "",
                    "critical_path_operations": "",
                    "machine_utilization_mean_percent": "",
                    "machine_utilization_max_percent": "",
                    "convergence_trace": "not instrumented by solver",
                    "schedule_path": str(schedule_path.relative_to(BASE_DIR)),
                    "diagnostic": "",
                }
                try:
                    result = subprocess.run(
                        [str(executable), "--instance", str(instance_path), "--output", str(schedule_path)],
                        capture_output=True,
                        text=True,
                        timeout=timeout,
                        check=False,
                    )
                    elapsed = time.perf_counter() - start
                    row["runtime_seconds"] = f"{elapsed:.6f}"
                    if result.returncode != 0:
                        raise RuntimeError(
                            f"solver exit {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
                        )
                    validation = subprocess.run(
                        [
                            str(VALIDATOR),
                            "--instance", str(instance_path),
                            "--schedule", str(schedule_path),
                        ],
                        capture_output=True,
                        text=True,
                        timeout=timeout,
                        check=False,
                    )
                    if validation.returncode != 0:
                        raise RuntimeError(
                            f"validator exit {validation.returncode}\n"
                            f"stdout:\n{validation.stdout}\nstderr:\n{validation.stderr}"
                        )
                    metrics = schedule_metrics(schedule_path, instance_data)
                    row.update({
                        "feasible": True,
                        "makespan": f"{metrics['makespan']:.6f}",
                        "lower_bound_gap_percent": f"{metrics['lower_bound_gap_percent']:.6f}",
                        "critical_path_length": f"{metrics['critical_path']:.6f}",
                        "critical_path_operations": " > ".join(metrics["critical_path_operations"]),
                        "machine_utilization_mean_percent": (
                            f"{metrics['machine_utilization_mean_percent']:.6f}"
                        ),
                        "machine_utilization_max_percent": (
                            f"{metrics['machine_utilization_max_percent']:.6f}"
                        ),
                    })
                    log_path.unlink(missing_ok=True)
                except (OSError, RuntimeError, subprocess.TimeoutExpired, ValueError, KeyError) as error:
                    row["runtime_seconds"] = row["runtime_seconds"] or f"{time.perf_counter() - start:.6f}"
                    row["diagnostic"] = str(error).replace("\n", " | ")
                    write_failure_log(log_path, row["diagnostic"])
                writer.writerow(row)
                output.flush()
                status = "VALID" if row["feasible"] else "FAILED"
                print(
                    f"{status:6} {name:32} {algorithm:24} "
                    f"makespan={row['makespan'] or 'NA'} runtime={row['runtime_seconds']}s"
                )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--instances",
        default=str(EXPERIMENTS_DIR),
        help="Folder containing experiment instance JSON files (default: experiments/)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=180.0,
        help="Per solver or validator timeout in seconds (default: 180)",
    )
    parser.add_argument(
        "--pattern",
        default="*.json",
        help="Instance glob (default: *.json; excludes the Python hand-built generator)",
    )
    args = parser.parse_args()
    instance_paths = sorted(Path(args.instances).glob(args.pattern))
    if not instance_paths:
        parser.error(f"no instances matched {args.pattern!r} in {args.instances!r}")
    run_experiments(instance_paths, args.timeout)


if __name__ == "__main__":
    main()
