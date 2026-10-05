#!/usr/bin/env python3
"""
generate_instances.py — CLI for generating and inspecting FJSP instances

Provides:
  1. single   — Generate a single instance via gen_core executable
  2. batch    — Generate all classes × all seeds in one run
  3. stats    — Print detailed statistics for a JSON instance file

No external dependencies — stdlib only (json, subprocess, pathlib, argparse, csv).

Usage:
  python generator/generate_instances.py single --jobs 100 --machines 20 --class average --seed 42 --output instances/instance.json
  python generator/generate_instances.py batch  --jobs 50 --machines 10 --seeds 1,2,3,4,5 --output-dir instances/batch/
  python generator/generate_instances.py stats  --input instances/instance.json
"""

import argparse
import json
import os
import subprocess
import sys
import math
from pathlib import Path
from collections import defaultdict

# ──────────────────────────────────────────────────────
#  Constants
# ──────────────────────────────────────────────────────

INSTANCE_CLASSES = [
    "average", "easy", "hard", "extreme",
    "bottleneck_heavy", "high_flexibility", "low_flexibility",
    "unbalanced", "high_variance", "machine_advantage",
    "large_scale", "small_tight"
]

# Locate gen_core executable relative to this script
SCRIPT_DIR = Path(__file__).parent.resolve()

def find_gen_core():
    """Find the gen_core executable."""
    candidates = [
        SCRIPT_DIR / "gen_core.exe",
        SCRIPT_DIR / "gen_core",
        SCRIPT_DIR / ".." / "gen_core.exe",
        SCRIPT_DIR / ".." / "gen_core",
    ]
    for c in candidates:
        if c.exists():
            return str(c.resolve())
    # Try PATH
    import shutil
    p = shutil.which("gen_core") or shutil.which("gen_core.exe")
    if p:
        return p
    return None


# ──────────────────────────────────────────────────────
#  Single Instance Generation
# ──────────────────────────────────────────────────────

def cmd_single(args):
    """Generate a single instance by invoking gen_core."""
    gen_core = find_gen_core()
    if not gen_core:
        print("ERROR: gen_core executable not found. Compile it first:")
        print("  g++ -O2 -std=c++17 -o generator/gen_core generator/gen_core.cpp")
        sys.exit(1)

    cmd = [gen_core]
    cmd += ["--jobs", str(args.jobs)]
    cmd += ["--machines", str(args.machines)]
    cmd += ["--seed", str(args.seed)]

    if args.instance_class != "custom":
        cmd += ["--class", args.instance_class]

    if args.format:
        cmd += ["--format", args.format]
    if args.output:
        cmd += ["--output", args.output]

    # Pass through optional params if specified
    if args.ops_mean is not None:
        cmd += ["--ops-mean", str(args.ops_mean)]
    if args.ops_max is not None:
        cmd += ["--ops-max", str(args.ops_max)]
    if args.flexibility is not None:
        cmd += ["--flexibility", str(args.flexibility)]
    if args.pt_min is not None:
        cmd += ["--pt-min", str(args.pt_min)]
    if args.pt_max is not None:
        cmd += ["--pt-max", str(args.pt_max)]
    if args.pt_var is not None:
        cmd += ["--pt-var", str(args.pt_var)]
    if args.pt_dist is not None:
        cmd += ["--pt-dist", args.pt_dist]
    if args.bneck_prob is not None:
        cmd += ["--bneck-prob", str(args.bneck_prob)]
    if args.noise is not None:
        cmd += ["--noise", str(args.noise)]

    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=False)
    if result.returncode == 0 and getattr(args, 'plot', False):
        try:
            import visualize_generated_instances
            target_json = args.output if args.output else "instances/instance.json"
            
            # Put the plot in an "images" subfolder relative to the target_json
            out_dir = os.path.dirname(target_json) or "."
            images_dir = os.path.join(out_dir, "images")
            os.makedirs(images_dir, exist_ok=True)
            
            base_name = os.path.basename(target_json)
            if base_name.endswith('.json'):
                plot_name = base_name[:-5] + "_dashboard.png"
            else:
                plot_name = base_name + "_dashboard.png"
                
            plot_path = os.path.join(images_dir, plot_name)
            
            if not args.output:
                print(f"Warning: --plot requires a JSON file. Attempting to use {target_json} if it exists.", file=sys.stderr)
            
            if os.path.exists(target_json):
                print(f"Generating plot: {plot_path}", file=sys.stderr)
                visualize_generated_instances.create_dashboard(target_json, plot_path, show=False)
            else:
                print(f"ERROR: Cannot plot because {target_json} not found.", file=sys.stderr)
        except ImportError:
            print("matplotlib is required for plotting. Please install it.", file=sys.stderr)

    return result.returncode


# ──────────────────────────────────────────────────────
#  Batch Generation
# ──────────────────────────────────────────────────────

def cmd_batch(args):
    """Generate all classes × all seeds."""
    gen_core = find_gen_core()
    if not gen_core:
        print("ERROR: gen_core executable not found. Compile it first.")
        sys.exit(1)

    seeds = [int(s.strip()) for s in args.seeds.split(",")]
    classes = args.classes.split(",") if args.classes else INSTANCE_CLASSES
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    total = len(classes) * len(seeds)
    count = 0
    failures = 0

    print(f"Batch generation: {len(classes)} classes × {len(seeds)} seeds = {total} instances")
    print(f"Output directory: {output_dir}")
    print()

    for cls in classes:
        for seed in seeds:
            count += 1
            # Naming: {class}_j{jobs}_m{machines}_s{seed}.json
            fname = f"{cls}_j{args.jobs}_m{args.machines}_s{seed}.json"
            fpath = output_dir / fname

            cmd = [
                gen_core,
                "--jobs", str(args.jobs),
                "--machines", str(args.machines),
                "--seed", str(seed),
                "--class", cls,
                "--format", "json",
                "--output", str(fpath),
            ]

            result = subprocess.run(cmd, capture_output=True, text=True)
            status = "OK" if result.returncode == 0 else "FAIL"
            if result.returncode != 0:
                failures += 1
                print(f"  [{count}/{total}] {status}: {fname}")
                if result.stderr:
                    print(f"    stderr: {result.stderr.strip()}")
            else:
                print(f"  [{count}/{total}] {status}: {fname}")
                if getattr(args, 'plot', False):
                    try:
                        import visualize_generated_instances
                        images_dir = output_dir / "images"
                        images_dir.mkdir(parents=True, exist_ok=True)
                        plot_path = images_dir / (fname[:-5] + "_dashboard.png")
                        visualize_generated_instances.create_dashboard(str(fpath), str(plot_path), show=False)
                    except ImportError:
                        pass

    print(f"\nDone: {total - failures}/{total} succeeded, {failures} failed")
    return 0 if failures == 0 else 1


# ──────────────────────────────────────────────────────
#  Instance Statistics
# ──────────────────────────────────────────────────────

def cmd_stats(args):
    """Print detailed statistics for a JSON instance file."""
    with open(args.input, "r") as f:
        data = json.load(f)

    meta = data["metadata"]
    jobs_data = data["jobs"]

    n = meta["num_jobs"]
    m = meta["num_machines"]
    total_ops = meta["total_ops"]

    print("=" * 60)
    print("  FJSP Instance Statistics")
    print("=" * 60)
    print(f"  Class:           {meta.get('instance_class', '?')}")
    print(f"  Seed:            {meta.get('seed', '?')}")
    print(f"  Generator:       {meta.get('generator_version', '?')}")
    print(f"  PT Distribution: {meta.get('pt_distribution', '?')}")
    print()
    print(f"  Jobs:            {n}")
    print(f"  Machines:        {m}")
    print(f"  Total Ops:       {total_ops}")
    print(f"  Avg Ops/Job:     {total_ops / n:.2f}")
    print()

    # Ops per job distribution
    ops_counts = [len(j["operations"]) for j in jobs_data]
    print(f"  Ops/Job Range:   [{min(ops_counts)}, {max(ops_counts)}]")
    print(f"  Ops/Job Mean:    {sum(ops_counts) / len(ops_counts):.2f}")
    print(f"  Ops/Job StdDev:  {_stddev(ops_counts):.2f}")
    print()

    # Flexibility analysis
    flex_values = []
    for job in jobs_data:
        for op in job["operations"]:
            num_elig = op.get("num_eligible", len(op["eligible_machines"]))
            flex_values.append(num_elig / m)

    print(f"  Flexibility:")
    print(f"    Mean:          {sum(flex_values) / len(flex_values):.3f}")
    print(f"    Min:           {min(flex_values):.3f}")
    print(f"    Max:           {max(flex_values):.3f}")
    print(f"    StdDev:        {_stddev(flex_values):.3f}")
    print()

    # Processing time analysis
    all_times = []
    for job in jobs_data:
        for op in job["operations"]:
            for mach in op["eligible_machines"]:
                all_times.append(mach["processing_time"])

    print(f"  Processing Times:")
    print(f"    Count:         {len(all_times)}")
    print(f"    Mean:          {sum(all_times) / len(all_times):.2f}")
    print(f"    Min:           {min(all_times)}")
    print(f"    Max:           {max(all_times)}")
    print(f"    StdDev:        {_stddev(all_times):.2f}")
    print(f"    Median:        {_median(all_times):.1f}")
    print()

    # Machine load analysis (how many operations list each machine)
    machine_load = defaultdict(int)
    machine_total_time = defaultdict(int)
    for job in jobs_data:
        for op in job["operations"]:
            for mach in op["eligible_machines"]:
                mid = mach["machine_id"]
                machine_load[mid] += 1
                machine_total_time[mid] += mach["processing_time"]

    loads = list(machine_load.values())
    print(f"  Machine Load (eligible ops per machine):")
    print(f"    Mean:          {sum(loads) / len(loads):.1f}" if loads else "    No data")
    print(f"    Min:           {min(loads)}" if loads else "")
    print(f"    Max:           {max(loads)}" if loads else "")
    print(f"    StdDev:        {_stddev(loads):.1f}" if loads else "")
    print()

    # Machine roles
    roles = meta.get("machine_roles", {})
    bneck = roles.get("bottleneck_machines", [])
    spec = roles.get("specialist_machines", [])
    print(f"  Machine Roles:")
    print(f"    Bottleneck:    {bneck if bneck else 'none'}")
    print(f"    Specialist:    {spec if spec else 'none'}")
    print()

    # Job weight distribution
    weight_counts = defaultdict(int)
    for job in jobs_data:
        wc = job.get("weight_class", 0)
        if wc == -1:
            weight_counts["light"] += 1
        elif wc == 0:
            weight_counts["medium"] += 1
        elif wc == 1:
            weight_counts["heavy"] += 1

    print(f"  Job Weights:")
    print(f"    Light:         {weight_counts.get('light', 0)}")
    print(f"    Medium:        {weight_counts.get('medium', 0)}")
    print(f"    Heavy:         {weight_counts.get('heavy', 0)}")
    print()

    # Machine load bar chart (ASCII)
    if loads:
        max_load = max(loads)
        bar_width = 40
        print(f"  Machine Load Bar Chart:")
        sorted_machines = sorted(machine_load.items(), key=lambda x: x[1], reverse=True)
        # Show top 20 machines max
        for mid, load in sorted_machines[:20]:
            bar_len = int((load / max_load) * bar_width) if max_load > 0 else 0
            bar = "#" * bar_len
            print(f"    {mid:>8s}: {bar} ({load})")
        if len(sorted_machines) > 20:
            print(f"    ... and {len(sorted_machines) - 20} more machines")

    print()
    print("=" * 60)
    return 0


def _stddev(values):
    """Compute population standard deviation."""
    if len(values) <= 1:
        return 0.0
    mean = sum(values) / len(values)
    variance = sum((x - mean) ** 2 for x in values) / len(values)
    return math.sqrt(variance)


def _median(values):
    """Compute median."""
    s = sorted(values)
    n = len(s)
    if n == 0:
        return 0.0
    if n % 2 == 1:
        return float(s[n // 2])
    return (s[n // 2 - 1] + s[n // 2]) / 2.0


# ──────────────────────────────────────────────────────
#  CLI Entry Point
# ──────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="FJSP Instance Generator Orchestrator (Part A)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s single --jobs 100 --machines 20 --class average --seed 42 --output instances/instance.json
  %(prog)s batch  --jobs 50 --machines 10 --seeds 1,2,3,4,5 --output-dir instances/batch/
  %(prog)s stats  --input instances/instance.json
"""
    )
    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # ── single ──
    p_single = subparsers.add_parser("single", help="Generate a single instance")
    p_single.add_argument("--jobs", type=int, default=10)
    p_single.add_argument("--machines", type=int, default=5)
    p_single.add_argument("--seed", type=int, default=42)
    p_single.add_argument("--class", dest="instance_class", default="custom")
    p_single.add_argument("--format", default="json", choices=["json", "text", "both"])
    p_single.add_argument("--output", default=None)
    # Optional passthrough params
    p_single.add_argument("--ops-mean", type=float, default=None)
    p_single.add_argument("--ops-max", type=int, default=None)
    p_single.add_argument("--flexibility", type=float, default=None)
    p_single.add_argument("--pt-min", type=int, default=None)
    p_single.add_argument("--pt-max", type=int, default=None)
    p_single.add_argument("--pt-var", type=float, default=None)
    p_single.add_argument("--pt-dist", default=None)
    p_single.add_argument("--bneck-prob", type=float, default=None)
    p_single.add_argument("--noise", type=float, default=None)
    p_single.add_argument("--plot", action="store_true", help="Generate visualization dashboard")

    # ── batch ──
    p_batch = subparsers.add_parser("batch", help="Batch generate all classes × seeds")
    p_batch.add_argument("--jobs", type=int, default=10)
    p_batch.add_argument("--machines", type=int, default=5)
    p_batch.add_argument("--seeds", default="1,2,3,4,5", help="Comma-separated seed list")
    p_batch.add_argument("--classes", default=None,
                         help="Comma-separated class list (default: all 12)")
    p_batch.add_argument("--output-dir", default="instances/generated_batch/")
    p_batch.add_argument("--plot", action="store_true", help="Generate visualization dashboard for all instances")

    # ── stats ──
    p_stats = subparsers.add_parser("stats", help="Print instance statistics")
    p_stats.add_argument("--input", required=True, help="Path to JSON instance file")

    args = parser.parse_args()

    if args.command == "single":
        return cmd_single(args)
    elif args.command == "batch":
        return cmd_batch(args)
    elif args.command == "stats":
        return cmd_stats(args)
    else:
        parser.print_help()
        return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
