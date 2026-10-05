#define EVALUATOR_LIBRARY
#include "../environment/evaluator.cpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <limits>

using namespace std;
using json = nlohmann::json;

// Struct to hold operation details for the algorithm
struct AlgOp {
    string job_id;
    string op_id;
    unordered_map<string, double> eligible_machines; // machine_id -> proc_time
};

struct AlgJob {
    string job_id;
    vector<AlgOp> operations;
    int next_op_idx = 0;
    double avail_time = 0.0;
};

class GreedySPT {
private:
    unordered_map<string, AlgJob> jobs;
    vector<string> job_ids;
    unordered_map<string, double> machine_avail_time;
    json instance_json;

public:
    GreedySPT(const string& instance_path) {
        ifstream f(instance_path);
        if (!f.is_open()) {
            cerr << "ERROR: Cannot open instance file: " << instance_path << "\n";
            exit(1);
        }
        f >> instance_json;

        for (const auto& j : instance_json["jobs"]) {
            AlgJob job;
            job.job_id = j["job_id"];
            job.next_op_idx = 0;
            job.avail_time = 0.0;

            for (const auto& op : j["operations"]) {
                AlgOp a_op;
                a_op.job_id = job.job_id;
                a_op.op_id = op["op_id"];
                for (const auto& m : op["eligible_machines"]) {
                    a_op.eligible_machines[m["machine_id"]] = m["processing_time"];
                    machine_avail_time[m["machine_id"]] = 0.0; // initialize
                }
                job.operations.push_back(a_op);
            }
            jobs[job.job_id] = job;
            job_ids.push_back(job.job_id);
        }
    }

    Solution solve() {
        Solution sol;
        bool all_done = false;

        while (!all_done) {
            all_done = true;
            
            string best_job = "";
            string best_machine = "";
            double best_ect = numeric_limits<double>::infinity();
            double best_start = 0.0;
            AlgOp best_op;

            // Check all ready operations (one per active job)
            for (const string& jid : job_ids) {
                auto& job = jobs[jid];
                if (job.next_op_idx < job.operations.size()) {
                    all_done = false;
                    AlgOp current_op = job.operations[job.next_op_idx];
                    
                    // Find the best machine for this operation
                    for (const auto& [mid, pt] : current_op.eligible_machines) {
                        double start_time = max(job.avail_time, machine_avail_time[mid]);
                        double ect = start_time + pt; // Earliest Completion Time
                        
                        // Break ties with processing time (SPT) or simply take min ECT
                        if (ect < best_ect) {
                            best_ect = ect;
                            best_job = jid;
                            best_machine = mid;
                            best_start = start_time;
                            best_op = current_op;
                        }
                    }
                }
            }

            if (!all_done) {
                // Schedule the best operation
                OpAssignment assignment;
                assignment.job_id = best_job;
                assignment.op_id = best_op.op_id;
                assignment.machine_id = best_machine;
                assignment.start_time = best_start;
                assignment.completion_time = best_ect;
                sol.assignments.push_back(assignment);

                // Update availabilities
                jobs[best_job].avail_time = best_ect;
                jobs[best_job].next_op_idx++;
                machine_avail_time[best_machine] = best_ect;
            }
        }

        return sol;
    }
};

int main(int argc, char* argv[]) {
    string instance_path;
    string output_path;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--instance" && i + 1 < argc) {
            instance_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    if (instance_path.empty()) {
        cerr << "Usage: greedy_spt --instance <instance.json> [--output <best.json>]\n";
        return 1;
    }

    // 1. Run Greedy SPT Algorithm
    cout << "[GreedySPT] Solving instance: " << instance_path << endl;
    GreedySPT solver(instance_path);
    Solution candidate = solver.solve();

    // 2. Evaluate and output using Evaluator
    Evaluator eval(instance_path);
    double makespan = eval.evaluate(candidate);

    if (makespan == numeric_limits<double>::infinity()) {
        cerr << "[GreedySPT] Generated schedule is INVALID!\n";
        return 1;
    }

    eval.print_summary();

    if (!output_path.empty()) {
        eval.export_best(output_path);
    }

    return 0;
}
