"""Run the legacy algorithm-metrics sweep and create schedule Gantt charts."""

import os
import json
import time
import csv
import subprocess
from pathlib import Path

# Set up paths according to the required folder structure
BASE_DIR = Path(__file__).parent.parent.resolve()
ALGO_DIR = BASE_DIR / "algorithms"
EXP_DIR = BASE_DIR / "experiments"
RES_DIR = BASE_DIR / "results"
ANALYSIS_DIR = BASE_DIR / "analysis"
VISUALIZE = ALGO_DIR / "visualize_schedule.py"

RES_DIR.mkdir(parents=True, exist_ok=True)
ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)

ALGORITHMS = [
    "greedy_spt",
    "local_search",
    "simulated_annealing",
    "tabu_search",
    "genetic_algorithm",
    "memetic_algorithm"
]

def get_makespan(schedule_path):
    if not Path(schedule_path).exists():
        return float('inf')
    with open(schedule_path, 'r') as f:
        data = json.load(f)
    if not data:
        return float('inf')
    return max(task["completion_time"] for task in data)

def run_metrics():
    print("=== Running Metric Evaluations on all Experiments ===")
    
    csv_file = ANALYSIS_DIR / "experiment_summary.csv"
    
    instances = list(EXP_DIR.glob("*.json"))
    if not instances:
        print(f"No instances found in {EXP_DIR}")
        return
        
    with open(csv_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Instance", "Algorithm", "Makespan", "Runtime_sec"])
        
        for instance_path in instances:
            instance_name = instance_path.stem
            print(f"\nEvaluating Instance: {instance_name}")
            
            for algo in ALGORITHMS:
                algo_exe = ALGO_DIR / f"{algo}.exe"
                if not algo_exe.exists():
                    print(f"  WARNING: {algo_exe.name} not found. Skipping.")
                    continue
                    
                schedule_out = RES_DIR / f"sched_{instance_name}_{algo}.json"
                schedule_out.unlink(missing_ok=True)
                
                # Run Algorithm
                print(f"  -> Running {algo}...")
                start_time = time.time()
                run_succeeded = True
                try:
                    subprocess.run([str(algo_exe), "--instance", str(instance_path), "--output", str(schedule_out)],
                                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
                except subprocess.TimeoutExpired:
                    print(f"     [TIMEOUT] {algo} exceeded 60s")
                    run_succeeded = False
                except subprocess.CalledProcessError:
                    print(f"     [ERROR] {algo} failed")
                    run_succeeded = False
                
                elapsed = time.time() - start_time
                makespan = get_makespan(schedule_out) if run_succeeded else float('inf')
                
                print(f"     Makespan: {makespan} | Time: {elapsed:.2f}s")
                writer.writerow([instance_name, algo, makespan, elapsed])
                f.flush()
                
                # Visualize
                if run_succeeded and makespan != float('inf'):
                    img_out = ANALYSIS_DIR / f"gantt_{instance_name}_{algo}.png"
                    subprocess.run(["python", str(VISUALIZE), 
                                    "--instance", str(instance_path), 
                                    "--schedule", str(schedule_out),
                                    "--output", str(img_out),
                                    "--algo", algo], check=True, stdout=subprocess.DEVNULL)
                                    
    print(f"\nMetric evaluation completed. Results saved to {ANALYSIS_DIR}")

if __name__ == "__main__":
    run_metrics()
