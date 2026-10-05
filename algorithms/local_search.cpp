#define EVALUATOR_LIBRARY
#include "../environment/evaluator.cpp"
#include "fjsp_algorithm.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>

using namespace std;

class LocalSearch {
private:
    InstanceData instance;
    Evaluator eval;
    mt19937 rng;

    // Current State
    vector<string> current_os;
    unordered_map<string, string> current_ma;
    double current_makespan;

public:
    LocalSearch(const string& instance_path, int seed = 42) : eval(instance_path), rng(seed) {
        instance = load_instance(instance_path);
        
        // Initialize random OS
        for (const auto& j : instance.jobs) {
            for (int i = 0; i < j.operations.size(); ++i) {
                current_os.push_back(j.job_id);
            }
        }
        shuffle(current_os.begin(), current_os.end(), rng);

        // Initialize random MA
        for (const auto& j : instance.jobs) {
            for (const auto& op : j.operations) {
                vector<string> eligible;
                for (const auto& m : op.eligible_machines) {
                    eligible.push_back(m.first);
                }
                uniform_int_distribution<int> dist(0, eligible.size() - 1);
                current_ma[op.op_id] = eligible[dist(rng)];
            }
        }

        auto op_seq = convert_os_to_op_sequence(instance, current_os);
        Solution sol = build_schedule(instance, op_seq, current_ma);
        current_makespan = eval.evaluate(sol);
    }

    void solve(int max_iter) {
        for (int iter = 0; iter < max_iter; ++iter) {
            // Generate a neighbor
            vector<string> neighbor_os = current_os;
            unordered_map<string, string> neighbor_ma = current_ma;

            // 50% chance to mutate OS, 50% chance to mutate MA
            uniform_real_distribution<double> coin(0.0, 1.0);
            if (coin(rng) < 0.5) {
                // Mutate OS: swap two random positions
                uniform_int_distribution<int> dist(0, current_os.size() - 1);
                int p1 = dist(rng), p2 = dist(rng);
                swap(neighbor_os[p1], neighbor_os[p2]);
            } else {
                // Mutate MA: change machine for a random operation
                uniform_int_distribution<int> job_dist(0, instance.jobs.size() - 1);
                const AlgJob& random_job = instance.jobs[job_dist(rng)];
                uniform_int_distribution<int> op_dist(0, random_job.operations.size() - 1);
                const AlgOp& random_op = random_job.operations[op_dist(rng)];
                
                if (random_op.eligible_machines.size() > 1) {
                    vector<string> eligible;
                    for (const auto& m : random_op.eligible_machines) {
                        if (m.first != neighbor_ma[random_op.op_id]) {
                            eligible.push_back(m.first);
                        }
                    }
                    uniform_int_distribution<int> m_dist(0, eligible.size() - 1);
                    neighbor_ma[random_op.op_id] = eligible[m_dist(rng)];
                }
            }

            auto op_seq = convert_os_to_op_sequence(instance, neighbor_os);
            Solution neighbor_sol = build_schedule(instance, op_seq, neighbor_ma);
            double neighbor_makespan = eval.evaluate(neighbor_sol);

            // Accept if strictly better (or equal, to allow plateau walks)
            if (neighbor_makespan <= current_makespan && neighbor_makespan != numeric_limits<double>::infinity()) {
                current_os = neighbor_os;
                current_ma = neighbor_ma;
                current_makespan = neighbor_makespan;
            }
        }
    }

    void print_and_export(const string& output_path) {
        eval.print_summary();
        if (!output_path.empty()) {
            eval.export_best(output_path);
        }
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
        cerr << "Usage: local_search --instance <instance.json> [--output <best.json>]\n";
        return 1;
    }

    cout << "[LocalSearch] Solving instance: " << instance_path << endl;
    LocalSearch ls(instance_path);
    ls.solve(10000); // 10,000 iterations
    ls.print_and_export(output_path);

    return 0;
}
