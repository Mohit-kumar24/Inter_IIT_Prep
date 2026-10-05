#define EVALUATOR_LIBRARY
#include "../environment/evaluator.cpp"
#include "fjsp_algorithm.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace std;

struct Chromosome {
    vector<string> os;
    unordered_map<string, string> ma;
    double makespan;
    double fitness;
};

class GeneticAlgorithm {
private:
    InstanceData instance;
    Evaluator eval;
    mt19937 rng;
    int pop_size;
    vector<Chromosome> population;

    void evaluate_chromosome(Chromosome& c) {
        auto op_seq = convert_os_to_op_sequence(instance, c.os);
        Solution sol = build_schedule(instance, op_seq, c.ma);
        c.makespan = eval.evaluate(sol);
        c.fitness = (c.makespan == numeric_limits<double>::infinity()) ? 0.0 : 10000.0 / c.makespan;
    }

    Chromosome generate_random_chromosome() {
        Chromosome c;
        for (const auto& j : instance.jobs) {
            for (int i = 0; i < j.operations.size(); ++i) {
                c.os.push_back(j.job_id);
            }
        }
        shuffle(c.os.begin(), c.os.end(), rng);

        for (const auto& j : instance.jobs) {
            for (const auto& op : j.operations) {
                vector<string> eligible;
                for (const auto& m : op.eligible_machines) {
                    eligible.push_back(m.first);
                }
                uniform_int_distribution<int> dist(0, eligible.size() - 1);
                c.ma[op.op_id] = eligible[dist(rng)];
            }
        }
        evaluate_chromosome(c);
        return c;
    }

    Chromosome tournament_selection() {
        uniform_int_distribution<int> dist(0, pop_size - 1);
        int idx1 = dist(rng);
        int idx2 = dist(rng);
        return (population[idx1].fitness > population[idx2].fitness) ? population[idx1] : population[idx2];
    }

    void crossover(Chromosome& p1, Chromosome& p2, Chromosome& c1, Chromosome& c2) {
        c1.ma = p1.ma;
        c2.ma = p2.ma;
        c1.os = p1.os;
        c2.os = p2.os;
        
        // MA uniform crossover
        uniform_real_distribution<double> coin(0.0, 1.0);
        for (auto& [op_id, m] : c1.ma) {
            if (coin(rng) < 0.5) {
                swap(c1.ma[op_id], c2.ma[op_id]);
            }
        }

        // OS Job-based Crossover (JBX)
        unordered_set<string> job_set1;
        for (const auto& j : instance.jobs) {
            if (coin(rng) < 0.5) job_set1.insert(j.job_id);
        }

        vector<string> p1_keep, p1_give, p2_keep, p2_give;
        for (const string& jid : p1.os) {
            if (job_set1.count(jid)) p1_keep.push_back(jid);
            else p1_give.push_back(jid);
        }
        for (const string& jid : p2.os) {
            if (job_set1.count(jid)) p2_give.push_back(jid);
            else p2_keep.push_back(jid);
        }

        int idx1 = 0, idx2 = 0;
        for (int i = 0; i < c1.os.size(); ++i) {
            if (job_set1.count(p1.os[i])) {
                c1.os[i] = p1_keep[idx1];
                c2.os[i] = p2_give[idx1++];
            } else {
                c1.os[i] = p2_keep[idx2];
                c2.os[i] = p1_give[idx2++];
            }
        }
    }

    void mutate(Chromosome& c) {
        uniform_real_distribution<double> coin(0.0, 1.0);
        if (coin(rng) < 0.1) { // 10% OS mutation
            uniform_int_distribution<int> dist(0, c.os.size() - 1);
            int p1 = dist(rng), p2 = dist(rng);
            swap(c.os[p1], c.os[p2]);
        }
        if (coin(rng) < 0.1) { // 10% MA mutation
            uniform_int_distribution<int> job_dist(0, instance.jobs.size() - 1);
            const AlgJob& random_job = instance.jobs[job_dist(rng)];
            uniform_int_distribution<int> op_dist(0, random_job.operations.size() - 1);
            const AlgOp& random_op = random_job.operations[op_dist(rng)];
            
            if (random_op.eligible_machines.size() > 1) {
                vector<string> eligible;
                for (const auto& m : random_op.eligible_machines) {
                    if (m.first != c.ma[random_op.op_id]) {
                        eligible.push_back(m.first);
                    }
                }
                uniform_int_distribution<int> m_dist(0, eligible.size() - 1);
                c.ma[random_op.op_id] = eligible[m_dist(rng)];
            }
        }
    }

public:
    GeneticAlgorithm(const string& instance_path, int pop_sz = 50, int seed = 42) 
        : eval(instance_path), rng(seed), pop_size(pop_sz) {
        instance = load_instance(instance_path);
        
        for (int i = 0; i < pop_size; ++i) {
            population.push_back(generate_random_chromosome());
        }
    }

    void solve(int generations) {
        for (int gen = 0; gen < generations; ++gen) {
            vector<Chromosome> new_pop;
            
            // Elitism: keep best
            auto best_it = max_element(population.begin(), population.end(), 
                [](const Chromosome& a, const Chromosome& b) { return a.fitness < b.fitness; });
            new_pop.push_back(*best_it);

            while (new_pop.size() < pop_size) {
                Chromosome p1 = tournament_selection();
                Chromosome p2 = tournament_selection();
                Chromosome c1, c2;
                
                crossover(p1, p2, c1, c2);
                mutate(c1);
                mutate(c2);
                
                evaluate_chromosome(c1);
                new_pop.push_back(c1);
                if (new_pop.size() < pop_size) {
                    evaluate_chromosome(c2);
                    new_pop.push_back(c2);
                }
            }
            population = new_pop;
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
        cerr << "Usage: genetic_algorithm --instance <instance.json> [--output <best.json>]\n";
        return 1;
    }

    cout << "[GA] Solving instance: " << instance_path << endl;
    GeneticAlgorithm ga(instance_path, 50); // population = 50
    ga.solve(200); // 200 generations
    ga.print_and_export(output_path);

    return 0;
}
