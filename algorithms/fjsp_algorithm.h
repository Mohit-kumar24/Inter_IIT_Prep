#ifndef FJSP_ALGORITHM_H
#define FJSP_ALGORITHM_H

#include "../environment/json.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <utility>

using namespace std;
using json = nlohmann::json;

// Define operation and assignment for search algorithms
struct AlgOp {
    string job_id;
    string op_id;
    unordered_map<string, double> eligible_machines; // machine_id -> proc_time
};

struct AlgJob {
    string job_id;
    vector<AlgOp> operations;
};

struct InstanceData {
    vector<AlgJob> jobs;
    vector<string> machine_ids;
    unordered_map<string, pair<size_t, size_t>> operation_index;
};

InstanceData load_instance(const string& instance_path) {
    ifstream f(instance_path);
    if (!f.is_open()) {
        cerr << "ERROR: Cannot open instance file: " << instance_path << "\n";
        exit(1);
    }
    json instance_json;
    f >> instance_json;

    InstanceData data;
    unordered_map<string, bool> seen_machines;

    for (const auto& j : instance_json["jobs"]) {
        const size_t job_index = data.jobs.size();
        AlgJob job;
        job.job_id = j["job_id"];

        for (const auto& op : j["operations"]) {
            AlgOp a_op;
            a_op.job_id = job.job_id;
            a_op.op_id = op["op_id"];
            for (const auto& m : op["eligible_machines"]) {
                string mid = m["machine_id"];
                a_op.eligible_machines[mid] = m["processing_time"];
                if (!seen_machines[mid]) {
                    seen_machines[mid] = true;
                    data.machine_ids.push_back(mid);
                }
            }
            const size_t operation_index = job.operations.size();
            data.operation_index.emplace(a_op.op_id, make_pair(job_index, operation_index));
            job.operations.push_back(a_op);
        }
        data.jobs.push_back(job);
    }
    return data;
}

// ──────────────────────────────────────────────────────
//  Schedule Builder (Decoder)
// ──────────────────────────────────────────────────────

// Convert Operation Sequence (array of job_ids) to valid op_sequence
inline vector<pair<string, string>> convert_os_to_op_sequence(const InstanceData& instance, const vector<string>& os) {
    unordered_map<string, int> job_progress;
    vector<pair<string, string>> op_seq;
    
    // Quick lookup for job operations
    unordered_map<string, const AlgJob*> job_map;
    for (const auto& j : instance.jobs) {
        job_map[j.job_id] = &j;
        job_progress[j.job_id] = 0;
    }

    for (const string& jid : os) {
        const AlgJob* j = job_map[jid];
        int idx = job_progress[jid];
        if (idx < j->operations.size()) {
            op_seq.push_back({jid, j->operations[idx].op_id});
            job_progress[jid]++;
        }
    }
    return op_seq;
}

// Given an ordered list of operations and their assigned machines, build a valid semi-active schedule.
// Evaluator requires explicit start/completion times.
inline Solution build_schedule(const InstanceData& instance, const vector<pair<string, string>>& op_sequence, const unordered_map<string, string>& machine_assignments) {
    Solution sol;
    unordered_map<string, double> machine_avail;
    unordered_map<string, double> job_avail;

    for (const auto& m : instance.machine_ids) machine_avail[m] = 0.0;
    for (const auto& j : instance.jobs) job_avail[j.job_id] = 0.0;

    for (const auto& [job_id, op_id] : op_sequence) {
        string machine_id = machine_assignments.at(op_id);
        const auto& indices = instance.operation_index.at(op_id);
        const AlgOp& operation = instance.jobs.at(indices.first).operations.at(indices.second);
        double pt = operation.eligible_machines.at(machine_id);

        double start = max(machine_avail[machine_id], job_avail[job_id]);
        double comp = start + pt;

        OpAssignment assignment;
        assignment.job_id = job_id;
        assignment.op_id = op_id;
        assignment.machine_id = machine_id;
        assignment.start_time = start;
        assignment.completion_time = comp;
        sol.assignments.push_back(assignment);

        machine_avail[machine_id] = comp;
        job_avail[job_id] = comp;
    }
    return sol;
}

#endif // FJSP_ALGORITHM_H
