/*
 * evaluator.cpp — FJSP Schedule Evaluator (Part B)
 *
 * Evaluates candidate schedules one at a time against an FJSP instance.
 * Designed to be called by the scheduling algorithm in algorithms/.
 *
 * Design Principles:
 *   1. Process exactly ONE candidate Solution at a time — never stores
 *      all candidates simultaneously.
 *   2. For each candidate, compute and return the objective value (makespan).
 *   3. Internally maintain best_solution and best_objective. When a
 *      candidate is better, replace best_solution with the candidate's
 *      complete representation.
 *   4. best_solution preserves enough information (per-operation assignments
 *      and times) to recover the final operation order without regenerating
 *      previous candidates.
 *   5. Format/export the operation order only ONCE, after the final
 *      best_solution has been determined.
 *   6. Memory: O(N) for current + best solutions, excluding algorithm-
 *      specific data held by the caller.
 *   7. Evaluator logic is completely separate from scheduling/search logic.
 *
 * Build:
 *   g++ -O2 -std=c++17 -o evaluator evaluator.cpp
 *   cl /O2 /std:c++17 /EHsc evaluator.cpp /Fe:evaluator.exe
 *
 * Standalone usage (for testing):
 *   ./evaluator --instance instances/test.json --schedule instances/schedule.json
 *
 * Programmatic usage (from algorithm code):
 *   #include "evaluator.cpp"  // or split into .h/.cpp
 *   Evaluator eval("instances/test.json");
 *   double obj = eval.evaluate(candidate);
 *   // ... repeat for many candidates ...
 *   eval.export_best("instances/best_schedule.json");
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

// ──────────────────────────────────────────────────────
//  Data Structures
// ──────────────────────────────────────────────────────

/// A single operation assignment within a candidate solution.
/// This is the minimal representation the algorithm must produce per operation.
struct OpAssignment {
    string job_id;
    string op_id;
    string machine_id;
    double start_time;
    double completion_time;
};

/// A complete candidate solution: a flat vector of operation assignments.
/// This is the representation the algorithm hands to the evaluator.
/// Memory: O(total_ops) per solution.
struct Solution {
    vector<OpAssignment> assignments;
};

/// Instance-side operation data: which job owns it, and the eligible
/// machine → processing time map.
struct InstanceOp {
    string job_id;
    unordered_map<string, double> eligibility;  // machine_id -> proc_time
};

// ──────────────────────────────────────────────────────
//  Evaluator
// ──────────────────────────────────────────────────────

class Evaluator {
private:
    // ── Instance data (read once, immutable) ──
    json instance_json;
    unordered_map<string, InstanceOp> ops_lookup;          // op_id -> InstanceOp
    unordered_map<string, vector<string>> job_ops_sequence; // job_id -> ordered op_ids

    // ── Best-so-far tracking (O(N) memory) ──
    Solution best_solution;
    double best_objective;
    bool has_best;

    /// Build instance lookups from the loaded JSON.
    void build_instance_lookups() {
        if (!instance_json.contains("jobs") || !instance_json["jobs"].is_array()) {
            cerr << "ERROR [Evaluator]: Instance JSON missing 'jobs' array.\n";
            return;
        }

        for (const auto& job : instance_json["jobs"]) {
            string jid = job["job_id"];
            for (const auto& op : job["operations"]) {
                string oid = op["op_id"];

                // Preserve instance-defined precedence order
                job_ops_sequence[jid].push_back(oid);

                InstanceOp iop;
                iop.job_id = jid;
                for (const auto& m : op["eligible_machines"]) {
                    iop.eligibility[m["machine_id"]] = m["processing_time"];
                }
                ops_lookup[oid] = iop;
            }
        }
    }

    /// Validate a single candidate and compute its makespan.
    /// Returns: (is_valid, makespan, error_messages)
    struct EvalResult {
        bool valid;
        double makespan;
        vector<string> errors;
    };

    EvalResult validate_and_compute(const Solution& candidate) const {
        EvalResult result;
        result.valid = true;
        result.makespan = 0.0;

        // ── 1. Build fast lookup from candidate ──
        unordered_map<string, const OpAssignment*> op_map;      // op_id -> assignment
        unordered_map<string, vector<const OpAssignment*>> machine_timeline; // machine -> ops

        for (const auto& a : candidate.assignments) {
            // Duplicate check
            if (op_map.count(a.op_id)) {
                result.errors.push_back("Duplicate operation: " + a.op_id);
                result.valid = false;
                continue;
            }
            op_map[a.op_id] = &a;
            machine_timeline[a.machine_id].push_back(&a);
        }

        // ── 2. Completeness check ──
        for (const auto& [oid, iop] : ops_lookup) {
            if (!op_map.count(oid)) {
                result.errors.push_back("Missing operation: " + oid);
                result.valid = false;
            }
        }
        if (!result.valid) return result;  // short-circuit

        // ── 3. Per-operation validation ──
        for (const auto& a : candidate.assignments) {
            auto it = ops_lookup.find(a.op_id);
            if (it == ops_lookup.end()) {
                result.errors.push_back("Unknown operation: " + a.op_id);
                result.valid = false;
                continue;
            }
            const InstanceOp& iop = it->second;

            // Job ID match
            if (a.job_id != iop.job_id) {
                result.errors.push_back("Job mismatch for " + a.op_id +
                    ": expected " + iop.job_id + ", got " + a.job_id);
                result.valid = false;
            }

            // Non-negative start
            if (!isfinite(a.start_time) || !isfinite(a.completion_time)) {
                result.errors.push_back("Non-finite time for " + a.op_id);
                result.valid = false;
                continue;
            }

            if (a.start_time < 0.0) {
                result.errors.push_back("Negative start time for " + a.op_id);
                result.valid = false;
            }

            // Forward progress
            if (a.completion_time < a.start_time) {
                result.errors.push_back("completion_time < start_time for " + a.op_id);
                result.valid = false;
            }

            // Machine eligibility
            auto elig_it = iop.eligibility.find(a.machine_id);
            if (elig_it == iop.eligibility.end()) {
                result.errors.push_back("Ineligible machine " + a.machine_id +
                    " for " + a.op_id);
                result.valid = false;
                continue;
            }

            // Exact duration
            double expected_pt = elig_it->second;
            if (abs((a.completion_time - a.start_time) - expected_pt) > 1e-6) {
                result.errors.push_back("Duration mismatch for " + a.op_id +
                    " on " + a.machine_id + ": got " +
                    to_string(a.completion_time - a.start_time) +
                    ", expected " + to_string(expected_pt));
                result.valid = false;
            }
        }

        if (!result.valid) return result;  // short-circuit

        // ── 4. Job precedence check ──
        for (const auto& [jid, op_seq] : job_ops_sequence) {
            double last_comp = 0.0;
            for (const string& oid : op_seq) {
                auto it = op_map.find(oid);
                if (it == op_map.end()) continue; // already caught above
                const OpAssignment* a = it->second;

                if (a->start_time < last_comp - 1e-9) {
                    result.errors.push_back("Precedence violation in " + jid +
                        ": " + oid + " starts at " + to_string(a->start_time) +
                        " before previous finished at " + to_string(last_comp));
                    result.valid = false;
                }
                last_comp = a->completion_time;
            }
            // Track job completion for makespan
            result.makespan = max(result.makespan, last_comp);
        }

        // ── 5. Machine non-overlap check ──
        for (auto& [mid, ops] : machine_timeline) {
            // Sort by start time
            sort(ops.begin(), ops.end(),
                [](const OpAssignment* a, const OpAssignment* b) {
                    return a->start_time < b->start_time;
                });

            for (size_t i = 0; i + 1 < ops.size(); ++i) {
                if (ops[i + 1]->start_time < ops[i]->completion_time - 1e-9) {
                    result.errors.push_back("Machine overlap on " + mid +
                        ": " + ops[i]->op_id + " [" +
                        to_string(ops[i]->start_time) + ", " +
                        to_string(ops[i]->completion_time) + "] vs " +
                        ops[i + 1]->op_id + " [" +
                        to_string(ops[i + 1]->start_time) + ", " +
                        to_string(ops[i + 1]->completion_time) + "]");
                    result.valid = false;
                }
            }
        }

        return result;
    }

public:
    /// Construct an evaluator by loading the FJSP instance JSON.
    /// The instance is loaded once and reused for all subsequent evaluations.
    explicit Evaluator(const string& instance_path) 
        : best_objective(numeric_limits<double>::infinity()), has_best(false) {
        ifstream f(instance_path);
        if (!f.is_open()) {
            cerr << "ERROR [Evaluator]: Cannot open instance file: " << instance_path << "\n";
            return;
        }
        try {
            f >> instance_json;
        } catch (const exception& e) {
            cerr << "ERROR [Evaluator]: Error parsing instance JSON: " << e.what() << "\n";
            return;
        }
        build_instance_lookups();
    }

    /// Evaluate a single candidate solution.
    /// Returns the candidate's objective value (makespan).
    /// If the candidate is valid and better than the current best,
    /// replaces best_solution with the candidate's complete representation.
    /// Returns infinity if the candidate is invalid.
    double evaluate(const Solution& candidate) {
        EvalResult result = validate_and_compute(candidate);

        if (!result.valid) {
            // Print diagnostics to stderr but do NOT store invalid candidates
            cerr << "[Evaluator] INVALID candidate (" << result.errors.size() << " errors):\n";
            for (const auto& e : result.errors) {
                cerr << "  - " << e << "\n";
            }
            return numeric_limits<double>::infinity();
        }

        // Valid candidate — check if it is a new best
        if (result.makespan < best_objective) {
            best_objective = result.makespan;
            best_solution = candidate;  // O(N) copy — becomes the new best
            has_best = true;
        }

        return result.makespan;
    }

    /// Evaluate a candidate loaded directly from a schedule JSON file.
    /// Convenience method for standalone/testing use.
    double evaluate_from_file(const string& schedule_path) {
        ifstream f(schedule_path);
        if (!f.is_open()) {
            cerr << "ERROR [Evaluator]: Cannot open schedule file: " << schedule_path << "\n";
            return numeric_limits<double>::infinity();
        }

        json sched;
        try {
            f >> sched;
        } catch (const exception& e) {
            cerr << "ERROR [Evaluator]: Error parsing schedule JSON: " << e.what() << "\n";
            return numeric_limits<double>::infinity();
        }

        if (!sched.is_array()) {
            cerr << "ERROR [Evaluator]: Schedule JSON must be an array.\n";
            return numeric_limits<double>::infinity();
        }

        Solution candidate;
        candidate.assignments.reserve(sched.size());
        for (const auto& entry : sched) {
            OpAssignment a;
            a.job_id          = entry.value("job_id", "");
            a.op_id           = entry.value("op_id", "");
            a.machine_id      = entry.value("machine_id", "");
            a.start_time      = entry.value("start_time", -1.0);
            a.completion_time = entry.value("completion_time", -1.0);
            candidate.assignments.push_back(std::move(a));
        }

        return evaluate(candidate);
    }

    // ── Accessors ──

    double get_best_objective() const { return best_objective; }
    bool   has_valid_best()     const { return has_best; }
    const Solution& get_best_solution() const { return best_solution; }

    // ── Export (called ONCE after the final best is determined) ──

    /// Build the final operation order from best_solution.
    /// Operations are sorted by start_time to produce a chronological
    /// execution sequence. This is computed only on demand, not during
    /// evaluation, so non-best candidates never pay this cost.
    vector<OpAssignment> build_operation_order() const {
        if (!has_best) return {};

        vector<OpAssignment> ordered = best_solution.assignments;
        sort(ordered.begin(), ordered.end(),
            [](const OpAssignment& a, const OpAssignment& b) {
                if (abs(a.start_time - b.start_time) > 1e-9)
                    return a.start_time < b.start_time;
                return a.op_id < b.op_id;  // stable tiebreak
            });
        return ordered;
    }

    /// Export the best solution as a JSON schedule file.
    /// Called once after all candidates have been evaluated.
    bool export_best(const string& output_path) const {
        if (!has_best) {
            cerr << "[Evaluator] No valid solution to export.\n";
            return false;
        }

        // Build the chronological operation order
        vector<OpAssignment> ordered = build_operation_order();

        // Serialize to JSON
        json output = json::array();
        for (const auto& a : ordered) {
            json entry;
            entry["job_id"]          = a.job_id;
            entry["op_id"]           = a.op_id;
            entry["machine_id"]      = a.machine_id;
            entry["start_time"]      = a.start_time;
            entry["completion_time"] = a.completion_time;
            output.push_back(entry);
        }

        ofstream f(output_path);
        if (!f.is_open()) {
            cerr << "ERROR [Evaluator]: Cannot write to " << output_path << "\n";
            return false;
        }
        f << output.dump(2);
        cerr << "[Evaluator] Exported best schedule (makespan=" << best_objective
             << ") to " << output_path << "\n";
        return true;
    }

    /// Print a human-readable summary of the best solution to stdout.
    void print_summary() const {
        cout << "============================================================\n";
        cout << "  FJSP Schedule Evaluator Summary\n";
        cout << "============================================================\n";

        if (!has_best) {
            cout << "\n  No valid solution found.\n";
            return;
        }

        const auto& sol = best_solution;
        int total_ops = (int)sol.assignments.size();

        // Compute per-machine utilization
        unordered_map<string, double> machine_busy;   // machine -> total busy time
        unordered_map<string, double> machine_span;   // machine -> (max_comp - min_start)
        unordered_map<string, double> machine_min_start;
        unordered_map<string, double> machine_max_comp;
        unordered_map<string, int>    machine_op_count;

        for (const auto& a : sol.assignments) {
            double dur = a.completion_time - a.start_time;
            machine_busy[a.machine_id] += dur;
            machine_op_count[a.machine_id]++;

            if (!machine_min_start.count(a.machine_id) || a.start_time < machine_min_start[a.machine_id])
                machine_min_start[a.machine_id] = a.start_time;
            if (!machine_max_comp.count(a.machine_id) || a.completion_time > machine_max_comp[a.machine_id])
                machine_max_comp[a.machine_id] = a.completion_time;
        }

        // Compute per-job completion times
        unordered_map<string, double> job_completion;
        for (const auto& [jid, op_seq] : job_ops_sequence) {
            double last = 0.0;
            for (const string& oid : op_seq) {
                for (const auto& a : sol.assignments) {
                    if (a.op_id == oid) {
                        last = max(last, a.completion_time);
                        break;
                    }
                }
            }
            job_completion[jid] = last;
        }

        cout << "\n  STATUS:    VALID\n";
        cout << "  Makespan:  " << best_objective << "\n";
        cout << "  Total ops: " << total_ops << "\n";
        cout << "  Jobs:      " << job_ops_sequence.size() << "\n";
        cout << "  Machines used: " << machine_busy.size() << "\n";

        // Machine utilization table
        cout << "\n  Machine Utilization (busy / makespan):\n";
        cout << "  " << string(50, '-') << "\n";
        for (const auto& [mid, busy] : machine_busy) {
            double util = (best_objective > 0) ? (busy / best_objective) * 100.0 : 0.0;
            cout << "    " << mid << ":  " << busy << " / " << best_objective
                 << "  (" << util << "%)"
                 << "  [" << machine_op_count[mid] << " ops]\n";
        }

        // Job completion times
        cout << "\n  Job Completion Times:\n";
        cout << "  " << string(50, '-') << "\n";
        for (const auto& [jid, comp] : job_completion) {
            cout << "    " << jid << ":  " << comp << "\n";
        }
    }
};

// ──────────────────────────────────────────────────────
//  Standalone main (for testing / CLI use)
// ──────────────────────────────────────────────────────

#ifndef EVALUATOR_LIBRARY
int main(int argc, char* argv[]) {
    string instance_path;
    string schedule_path;
    string output_path;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--instance" && i + 1 < argc) {
            instance_path = argv[++i];
        } else if (arg == "--schedule" && i + 1 < argc) {
            schedule_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            cout << "Usage: evaluator --instance <instance.json> --schedule <schedule.json> [--output <best.json>]\n";
            return 0;
        }
    }

    if (instance_path.empty() || schedule_path.empty()) {
        cerr << "Usage: evaluator --instance <instance.json> --schedule <schedule.json> [--output <best.json>]\n";
        return 1;
    }

    Evaluator eval(instance_path);
    double makespan = eval.evaluate_from_file(schedule_path);

    if (makespan == numeric_limits<double>::infinity()) {
        cerr << "\n[Evaluator] Schedule is INVALID.\n";
        return 1;
    }

    eval.print_summary();

    if (!output_path.empty()) {
        eval.export_best(output_path);
    }

    return 0;
}
#endif
