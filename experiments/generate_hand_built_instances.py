"""Generate the hand-built FJSP edge-case JSON instances."""

import json
import os
from pathlib import Path

# Paths
EXPERIMENTS_DIR = Path(__file__).parent.resolve()

def create_instance(name, num_jobs, num_machines, ops_data):
    """
    ops_data: list of jobs, where each job is a list of ops,
              where each op is a list of (machine_id, time)
    """
    instance = {
        "metadata": {
            "instance_name": name,
            "num_jobs": num_jobs,
            "num_machines": num_machines,
            "description": f"Hand-built edge case: {name}"
        },
        "jobs": []
    }
    
    for j_idx, job_ops in enumerate(ops_data):
        job_id = f"J_{j_idx+1:02d}"
        job_obj = {
            "job_id": job_id,
            "operations": []
        }
        for o_idx, elig_machines in enumerate(job_ops):
            op_id = f"{job_id}_O_{o_idx+1:02d}"
            op_obj = {
                "op_id": op_id,
                "eligible_machines": []
            }
            for (m_idx, time) in elig_machines:
                op_obj["eligible_machines"].append({
                    "machine_id": f"M_{m_idx+1}",
                    "processing_time": time
                })
            job_obj["operations"].append(op_obj)
        instance["jobs"].append(job_obj)
        
    out_path = EXPERIMENTS_DIR / f"{name}.json"
    with open(out_path, 'w') as f:
        json.dump(instance, f, indent=4)
    print(f"Created {out_path.name}")

def generate_hand_built():
    print("Generating hand-built edge cases...")
    
    # 1. single_machine
    create_instance("single_machine", 5, 1, 
        [ [[(0, 10)], [(0, 15)]] for _ in range(5) ]
    )

    # 2. one_job
    create_instance("one_job", 1, 5, 
        [ [[(m, 5) for m in range(5)] for _ in range(10)] ]
    )

    # 3. bottleneck
    # M2 is bottleneck
    jobs = []
    for j in range(10):
        job = []
        for o in range(3):
            if o == 1:
                job.append([(2, 50)]) # Bottleneck on M_3
            else:
                job.append([(0, 5), (1, 5), (3, 5), (4, 5)])
        jobs.append(job)
    create_instance("bottleneck_hand", 10, 5, jobs)

    # 4. extreme_time_gaps
    create_instance("extreme_time_gaps", 3, 2, 
        [ [[(0, 1), (1, 10000)] for _ in range(3)] for _ in range(3) ]
    )

    # 5. machine_advantage
    # J1 loves M1, J2 loves M2
    create_instance("machine_advantage_hand", 2, 2, 
        [
            [[(0, 1), (1, 100)], [(0, 1), (1, 100)], [(0, 1), (1, 100)]],
            [[(0, 100), (1, 1)], [(0, 100), (1, 1)], [(0, 100), (1, 1)]]
        ]
    )

    # 6. near_total_flexibility
    create_instance("near_total_flexibility", 3, 3, 
        [ [[(m, 10) for m in range(3)] for _ in range(3)] for _ in range(3) ]
    )

    # 7. near_zero_flexibility
    create_instance("near_zero_flexibility", 3, 3, 
        [
            [[(0, 10)], [(1, 10)], [(2, 10)]],
            [[(1, 10)], [(2, 10)], [(0, 10)]],
            [[(2, 10)], [(0, 10)], [(1, 10)]]
        ]
    )

    # 8. identical_processing_times
    create_instance("identical_processing_times", 5, 5, 
        [ [[(m, 10) for m in range(5)] for _ in range(4)] for _ in range(5) ]
    )

    # 9. long_critical_chains
    create_instance("long_critical_chains", 2, 5, 
        [ [[(m, 5) for m in range(5)] for _ in range(20)] for _ in range(2) ]
    )

    # 10. many_very_short_operations
    create_instance("many_very_short_operations", 3, 3, 
        [ [[(m, 1) for m in range(3)] for _ in range(50)] for _ in range(3) ]
    )

if __name__ == "__main__":
    generate_hand_built()
