#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

struct ScheduledOp {
    string job_id;
    string op_id;
    string machine_id;
    double start;
    double comp;
};

struct InstanceOp {
    string job_id;
    unordered_map<string, double> eligibility; // machine_id -> processing_time
};

class ScheduleValidator {
private:
    json instance;
    json schedule;
    
    vector<string> errors;
    int makespan = 0;
    
    unordered_map<string, InstanceOp> ops_lookup;
    unordered_map<string, vector<string>> job_ops_sequence;
    
    unordered_map<string, ScheduledOp> parsed_schedule_ops;
    unordered_map<string, vector<ScheduledOp>> machine_schedules;
    unordered_map<string, vector<ScheduledOp>> job_schedules;
    unordered_map<string, double> job_completion;

    void report_error(const string& msg) {
        errors.push_back(msg);
    }

    void build_instance_lookups() {
        if (!instance.contains("jobs") || !instance["jobs"].is_array()) {
            report_error("Instance JSON missing 'jobs' array.");
            return;
        }

        for (const auto& job : instance["jobs"]) {
            string jid = job["job_id"];
            for (const auto& op : job["operations"]) {
                string op_id = op["op_id"];
                
                // Save exact precedence order
                job_ops_sequence[jid].push_back(op_id);
                
                InstanceOp iop;
                iop.job_id = jid;
                for (const auto& m : op["eligible_machines"]) {
                    iop.eligibility[m["machine_id"]] = m["processing_time"];
                }
                ops_lookup[op_id] = iop;
            }
        }
    }

public:
    ScheduleValidator(const string& instance_path, const string& schedule_path) {
        ifstream inst_file(instance_path);
        if (!inst_file.is_open()) {
            report_error("Could not open instance file: " + instance_path);
            return;
        }
        try {
            inst_file >> instance;
        } catch (const exception& e) {
            report_error(string("Error parsing instance JSON: ") + e.what());
            return;
        }

        ifstream sched_file(schedule_path);
        if (!sched_file.is_open()) {
            report_error("Could not open schedule file: " + schedule_path);
            return;
        }
        try {
            sched_file >> schedule;
        } catch (const exception& e) {
            report_error(string("Error parsing schedule JSON: ") + e.what());
            return;
        }

        build_instance_lookups();
    }

    bool validate_format() {
        if (!schedule.is_array()) {
            report_error("Schedule must be a JSON array of objects.");
            return false;
        }
        
        for (size_t i = 0; i < schedule.size(); i++) {
            const auto& entry = schedule[i];
            if (!entry.is_object()) {
                report_error("Schedule entry at index " + to_string(i) + " is not an object.");
                return false;
            }
            if (!entry.contains("job_id") || !entry.contains("op_id") || 
                !entry.contains("machine_id") || !entry.contains("start_time") || 
                !entry.contains("completion_time")) {
                report_error("Schedule entry at index " + to_string(i) + " missing required fields.");
                return false;
            }
        }
        return true;
    }

    void validate_ids() {
        for (size_t i = 0; i < schedule.size(); i++) {
            const auto& entry = schedule[i];
            if (!entry["job_id"].is_string() || !entry["op_id"].is_string() || !entry["machine_id"].is_string()) {
                report_error("IDs must be strings at entry " + to_string(i) + ".");
                continue;
            }
            
            string op_id = entry["op_id"];
            string jid = entry["job_id"];
            
            if (ops_lookup.find(op_id) == ops_lookup.end()) {
                report_error("Invalid operation ID '" + op_id + "' found in schedule.");
                continue;
            }
            
            string expected_jid = ops_lookup[op_id].job_id;
            if (jid != expected_jid) {
                report_error("Mismatch: '" + op_id + "' belongs to '" + expected_jid + "', but schedule says '" + jid + "'.");
            }
        }
    }

    void validate_completeness() {
        unordered_set<string> seen_ops;
        for (const auto& entry : schedule) {
            if (!entry["op_id"].is_string()) continue;
            string op_id = entry["op_id"];
            if (seen_ops.count(op_id)) {
                report_error("Duplicate operation in schedule: " + op_id);
            }
            seen_ops.insert(op_id);
        }
        
        for (const auto& pair : ops_lookup) {
            if (seen_ops.find(pair.first) == seen_ops.end()) {
                report_error("Missing operation in schedule: " + pair.first + " not scheduled.");
            }
        }
    }

    void validate_times() {
        for (size_t i = 0; i < schedule.size(); i++) {
            const auto& entry = schedule[i];
            if (!entry["op_id"].is_string()) continue;
            
            string op_id = entry["op_id"];
            if (!entry["start_time"].is_number() || !entry["completion_time"].is_number()) {
                report_error("Times must be numeric for operation '" + op_id + "'.");
                continue;
            }
            
            double start = entry["start_time"];
            double comp = entry["completion_time"];
            
            if (!isfinite(start) || !isfinite(comp)) {
                report_error("Times must be finite for operation '" + op_id + "'.");
                continue;
            }
            if (start < 0) {
                report_error("Negative start time for '" + op_id + "': " + to_string(start));
            }
            if (comp < start) {
                report_error("Completion time (" + to_string(comp) + ") is before start time (" + to_string(start) + ") for '" + op_id + "'.");
            }
        }
    }

    void validate_machine_assignment() {
        for (const auto& entry : schedule) {
            if (!entry["op_id"].is_string() || !entry["machine_id"].is_string() || 
                !entry["start_time"].is_number() || !entry["completion_time"].is_number()) {
                continue; // Skip if previous validations failed
            }
            
            string op_id = entry["op_id"];
            string mid = entry["machine_id"];
            double start = entry["start_time"];
            double comp = entry["completion_time"];
            
            if (ops_lookup.find(op_id) == ops_lookup.end()) continue;
            
            const auto& eligibility = ops_lookup[op_id].eligibility;
            if (eligibility.find(mid) == eligibility.end()) {
                report_error("Ineligible machine assigned: '" + op_id + "' scheduled on '" + mid + "'.");
                continue;
            }
            
            double expected_pt = eligibility.at(mid);
            if (abs((comp - start) - expected_pt) > 1e-6) {
                report_error("Duration mismatch for '" + op_id + "' on '" + mid + 
                             "': scheduled [" + to_string(start) + ", " + to_string(comp) + 
                             "], expected " + to_string(expected_pt));
            }

            ScheduledOp sop = {entry["job_id"], op_id, mid, start, comp};
            parsed_schedule_ops[op_id] = sop;
            machine_schedules[mid].push_back(sop);
            job_schedules[entry["job_id"]].push_back(sop);
        }
    }

    void validate_precedence() {
        for (const auto& pair : job_ops_sequence) {
            const string& jid = pair.first;
            const vector<string>& expected_op_sequence = pair.second;
            
            double last_comp = 0;
            for (const string& op_id : expected_op_sequence) {
                if (parsed_schedule_ops.find(op_id) == parsed_schedule_ops.end()) continue;
                
                const ScheduledOp& op = parsed_schedule_ops[op_id];
                if (op.start < last_comp) {
                    report_error("Precedence violation in " + jid + ": '" + op_id + 
                                 "' starts at " + to_string(op.start) + 
                                 " before previous operation finished at " + to_string(last_comp) + ".");
                }
                last_comp = max(last_comp, op.comp);
            }
            job_completion[jid] = last_comp;
        }
    }

    void validate_machine_overlaps() {
        for (auto& pair : machine_schedules) {
            auto& ops = pair.second;
            sort(ops.begin(), ops.end(), [](const ScheduledOp& a, const ScheduledOp& b) {
                return a.start < b.start;
            });

            for (size_t i = 0; i < ops.size(); i++) {
                if (i == ops.size() - 1) break;
                const auto& curr_op = ops[i];
                const auto& next_op = ops[i+1];
                
                if (next_op.start < curr_op.comp) {
                    report_error("Machine " + pair.first + " contains overlapping operations:\n  " + 
                                 curr_op.job_id + " " + curr_op.op_id + ": [" + to_string(curr_op.start) + ", " + to_string(curr_op.comp) + "]\n  " + 
                                 next_op.job_id + " " + next_op.op_id + ": [" + to_string(next_op.start) + ", " + to_string(next_op.comp) + "]");
                }
            }
        }
    }

    void calculate_objective() {
        double mx = 0;
        for (const auto& pair : job_completion) {
            mx = max(mx, pair.second);
        }
        makespan = mx;
    }

    bool validate() {
        if (!errors.empty()) return false;
        
        if (!validate_format()) return false;
        
        validate_ids();
        validate_completeness();
        validate_times();
        
        if (!errors.empty()) return false;
        
        validate_machine_assignment();
        
        if (!errors.empty()) return false;
        
        validate_precedence();
        validate_machine_overlaps();
        
        if (errors.empty()) {
            calculate_objective();
        }
        
        return errors.empty();
    }

    void print_report() {
        cout << "============================================================" << endl;
        cout << "  FJSP Independent Schedule Validator (C++)" << endl;
        cout << "============================================================" << endl;

        if (errors.empty()) {
            cout << "\n  STATUS: VALID" << endl;
            cout << "  Makespan (C_max): " << makespan << endl;
        } else {
            cout << "\n  STATUS: INVALID" << endl;
            cout << "\n  Diagnostics (Errors Found):" << endl;
            for (size_t i = 0; i < errors.size(); i++) {
                cout << "  " << i + 1 << ". " << errors[i] << endl;
            }
            cout << "\n  Validation failed. Schedule violates well-formedness rules." << endl;
        }
    }
    
    bool is_valid() const {
        return errors.empty();
    }
};

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " --instance <instance.json> --schedule <schedule.json>" << endl;
        return 1;
    }

    string instance_path;
    string schedule_path;

    for (int i = 1; i < argc; i++) {
        if (string(argv[i]) == "--instance" && i + 1 < argc) {
            instance_path = argv[++i];
        } else if (string(argv[i]) == "--schedule" && i + 1 < argc) {
            schedule_path = argv[++i];
        }
    }

    if (instance_path.empty() || schedule_path.empty()) {
        cerr << "Error: --instance and --schedule arguments are required." << endl;
        return 1;
    }

    ScheduleValidator validator(instance_path, schedule_path);
    validator.validate();
    validator.print_report();

    return validator.is_valid() ? 0 : 1;
}
