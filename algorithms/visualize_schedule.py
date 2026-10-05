"""Render a solver schedule JSON file as a Gantt chart."""

import json
import argparse
import random
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

def load_json(path):
    with open(path, 'r') as f:
        return json.load(f)

def generate_colors(num_jobs):
    colors = {}
    # Use a colormap
    cmap = plt.colormaps['tab20']
    for i in range(num_jobs):
        colors[f"J_{i+1:02d}"] = cmap(i % 20) # Modulo 20 to avoid index out of bounds
    return colors

def plot_gantt(instance_path, schedule_path, output_path=None, algo_name=None):
    # Load instance and schedule
    instance = load_json(instance_path)
    schedule = load_json(schedule_path)
    
    # Extract jobs and machines
    jobs = [j["job_id"] for j in instance["jobs"]]
    num_machines = instance["metadata"]["num_machines"]
    
    # Assign colors to jobs dynamically based on IDs present
    cmap = plt.colormaps['tab20']
    job_colors = {jid: cmap(i % 20) for i, jid in enumerate(jobs)}

    # Machine index mapping (M_001 -> 1)
    # Sort machines to display nicely on Y-axis
    machines = set()
    for task in schedule:
        machines.add(task["machine_id"])
    sorted_machines = sorted(list(machines), key=lambda x: int(x.split('_')[1]))
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    # Plot bars
    for task in schedule:
        job_id = task["job_id"]
        op_id = task["op_id"]
        machine = task["machine_id"]
        start = task["start_time"]
        completion = task["completion_time"]
        
        m_idx = sorted_machines.index(machine)
        
        ax.barh(m_idx, completion - start, left=start, height=0.6, 
                color=job_colors[job_id], edgecolor='black')
        
        # Add text for the operation (e.g. O_01_01)
        # Simplify op_id for display: e.g. "J1_1"
        op_str = op_id.split('_')[-1]
        job_str = job_id.split('_')[-1]
        label = f"{job_str}-{op_str}"
        ax.text(start + (completion - start) / 2, m_idx, label, 
                ha='center', va='center', color='black', fontsize=8)

    # Formatting
    ax.set_yticks(range(len(sorted_machines)))
    ax.set_yticklabels(sorted_machines)
    ax.set_xlabel("Time")
    ax.set_ylabel("Machines")
    
    if algo_name:
        ax.set_title(f"FJSP Gantt Chart - {algo_name}")
    else:
        ax.set_title("FJSP Gantt Chart")
    
    # Legend
    handles = [mpatches.Patch(color=job_colors[j], label=j) for j in jobs]
    ax.legend(handles=handles, loc='center left', bbox_to_anchor=(1, 0.5), title="Jobs")
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=300)
        print(f"Saved Gantt chart to {output_path}")
    else:
        plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Visualize FJSP Schedule as Gantt Chart")
    parser.add_argument("--instance", required=True, help="Path to instance JSON")
    parser.add_argument("--schedule", required=True, help="Path to schedule JSON")
    parser.add_argument("--output", help="Path to save output image (e.g., .png)")
    parser.add_argument("--algo", help="Name of the algorithm (for title)")
    
    args = parser.parse_args()
    plot_gantt(args.instance, args.schedule, args.output, args.algo)
