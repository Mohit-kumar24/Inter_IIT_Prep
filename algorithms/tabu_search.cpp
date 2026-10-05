#define EVALUATOR_LIBRARY
#include "../environment/evaluator.cpp"
#include "fjsp_algorithm.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <algorithm>
#include <deque>
#include <sstream>

using namespace std;

class TabuSearch {
private:
    InstanceData instance;
    Evaluator eval;
    mt19937 rng;

    // Current State
    vector<string> current_os;
    unordered_map<string, string> current_ma;
    double current_makespan;

    string hash_state(const vector<string>& os, const unordered_map<string, string>& ma) {
        ostringstream oss;
        for (const string& j : os) oss << j << ",";
        oss << "|";
        for (const auto& [op, m] : ma) oss << op << ":" << m << ",";
        return oss.str();
    }

public:
    TabuSearch(const string& instance_path, int seed = 42) : eval(instance_path), rng(seed) {
        instance = load_instance(instance_path);
        
        for (const auto& j : instance.jobs) {
            for (int i = 0; i < j.operations.size(); ++i) {
                current_os.push_back(j.job_id);
            }
        }
        shuffle(current_os.begin(), current_os.end(), rng);

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

    void solve(int tabu_tenure, int max_iter, int neighborhood_size) {
        deque<string> tabu_queue;
        unordered_set<string> tabu_set;

        for (int iter = 0; iter < max_iter; ++iter) {
            vector<string> best_neighbor_os;
            unordered_map<string, string> best_neighbor_ma;
            double best_neighbor_makespan = numeric_limits<double>::infinity();

            for (int n = 0; n < neighborhood_size; ++n) {
                vector<string> neighbor_os = current_os;
                unordered_map<string, string> neighbor_ma = current_ma;

                uniform_real_distribution<double> coin(0.0, 1.0);
                if (coin(rng) < 0.5) {
                    uniform_int_distribution<int> dist(0, current_os.size() - 1);
                    int p1 = dist(rng), p2 = dist(rng);
                    swap(neighbor_os[p1], neighbor_os[p2]);
                } else {
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

                string state_hash = hash_state(neighbor_os, neighbor_ma);
                
                auto op_seq = convert_os_to_op_sequence(instance, neighbor_os);
                Solution neighbor_sol = build_schedule(instance, op_seq, neighbor_ma);
                double neighbor_makespan = eval.evaluate(neighbor_sol);

                // Aspiration criterion: if it's the global best ever found, ignore tabu
                bool is_global_best = (neighbor_makespan < eval.get_best_objective());
                
                if (neighbor_makespan != numeric_limits<double>::infinity()) {
                    if ((tabu_set.find(state_hash) == tabu_set.end() || is_global_best) && 
                         neighbor_makespan < best_neighbor_makespan) {
                        best_neighbor_makespan = neighbor_makespan;
                        best_neighbor_os = neighbor_os;
                        best_neighbor_ma = neighbor_ma;
                    }
                }
            }

            // Move to the best neighbor
            if (best_neighbor_makespan != numeric_limits<double>::infinity()) {
                current_os = best_neighbor_os;
                current_ma = best_neighbor_ma;
                current_makespan = best_neighbor_makespan;

                string new_hash = hash_state(current_os, current_ma);
                tabu_queue.push_back(new_hash);
                tabu_set.insert(new_hash);

                if (tabu_queue.size() > tabu_tenure) {
                    tabu_set.erase(tabu_queue.front());
                    tabu_queue.pop_front();
                }
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
        cerr << "Usage: tabu_search --instance <instance.json> [--output <best.json>]\n";
        return 1;
    }

    cout << "[TabuSearch] Solving instance: " << instance_path << endl;
    TabuSearch ts(instance_path);
    ts.solve(100, 2000, 20); // tenure=100, iter=2000, neighborhood_size=20
    ts.print_and_export(output_path);

    return 0;
}
