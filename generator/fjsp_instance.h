/*
 * fjsp_instance.h — Canonical FJSP instance data model (Part A)
 *
 * Stores the complete definition of a Flexible Job-Shop Scheduling Problem
 * instance: jobs, operations, eligible-machine sets, processing times, and
 * metadata (seed, generator parameters, instance class).
 *
 * Provides output in two formats:
 *   1. JSON  (for programmatic consumption, includes full metadata)
 *   2. Standard text (Brandimarte-style, widely used in FJSP literature)
 *
 * Naming: Jobs = J_0001, Machines = M_0001, Operations = O_0001_01
 * Padding width adapts to total count: ceil(log10(count + 1)).
 *
 * This header is self-contained (uses only the C++ standard library).
 */

#ifndef FJSP_INSTANCE_H
#define FJSP_INSTANCE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iomanip>

// ──────────────────────────────────────────────────────
//  ID formatting helpers
// ──────────────────────────────────────────────────────

/// Compute the padding width needed: ceil(log10(count + 1)), minimum 1
inline int pad_width(int count) {
    if (count <= 0) return 1;
    return std::max(1, (int)std::ceil(std::log10((double)(count + 1))));
}

/// Format a job ID: J_0001
inline std::string format_job_id(int job_id, int total_jobs) {
    int w = pad_width(total_jobs);
    std::ostringstream oss;
    oss << "J_" << std::setfill('0') << std::setw(w) << job_id;
    return oss.str();
}

/// Format a machine ID: M_0001
inline std::string format_machine_id(int machine_id, int total_machines) {
    int w = pad_width(total_machines);
    std::ostringstream oss;
    oss << "M_" << std::setfill('0') << std::setw(w) << machine_id;
    return oss.str();
}

/// Format an operation ID: O_0001_01
inline std::string format_op_id(int job_id, int op_index, int total_jobs, int max_ops) {
    int wj = pad_width(total_jobs);
    int wo = pad_width(max_ops);
    std::ostringstream oss;
    oss << "O_" << std::setfill('0') << std::setw(wj) << job_id
        << "_" << std::setfill('0') << std::setw(wo) << (op_index + 1);
    return oss.str();
}

// ──────────────────────────────────────────────────────
//  Data model
// ──────────────────────────────────────────────────────

/// A single (machine_id -> processing_time) pair.
struct MachineTime {
    int machine_id;   // 1-indexed
    int proc_time;    // > 0
};

/// One operation O(j, k) — its eligible machine set and processing times.
struct Operation {
    int job_id;       // 1-indexed
    int op_index;     // 0-indexed within the job
    std::vector<MachineTime> machines;  // eligible machines + times
};

/// A complete job — ordered sequence of operations.
struct Job {
    int job_id;       // 1-indexed
    int weight_class; // -1 = light, 0 = medium, +1 = heavy
    std::vector<Operation> operations;
};

/// Machine role information
struct MachineRole {
    bool is_bottleneck;
    bool is_specialist;
};

/// Metadata associated with the instance (for reproducibility).
struct InstanceMeta {
    int num_jobs       = 0;
    int num_machines   = 0;
    int total_ops      = 0;
    int max_ops_in_job = 0;           // for formatting padding
    uint64_t seed      = 0;
    std::string instance_class = "custom";
    std::string generator_version = "gen_core_v1";
    std::string pt_distribution = "lognormal";
    // Generator parameters stored as key-value strings.
    std::map<std::string, std::string> params;
    // Machine roles
    std::vector<int> bottleneck_machines;   // 1-indexed IDs
    std::vector<int> specialist_machines;   // 1-indexed IDs
};

/// The full FJSP instance.
struct FJSPInstance {
    InstanceMeta meta;
    std::vector<Job> jobs;
    std::vector<MachineRole> machine_roles;  // indexed 0..m-1 (machine_id - 1)

    // ── Validation ──

    void validate() const {
        if (meta.num_jobs < 1)
            throw std::runtime_error("num_jobs must be >= 1");
        if (meta.num_machines < 1)
            throw std::runtime_error("num_machines must be >= 1");
        if ((int)jobs.size() != meta.num_jobs)
            throw std::runtime_error("jobs.size() != num_jobs");

        int counted_ops = 0;
        for (int j = 0; j < (int)jobs.size(); ++j) {
            const auto& job = jobs[j];
            if (job.job_id != j + 1)
                throw std::runtime_error("Job ID not contiguous at index " + std::to_string(j));
            if (job.operations.empty())
                throw std::runtime_error("Job " + std::to_string(job.job_id) + " has no operations");

            for (int k = 0; k < (int)job.operations.size(); ++k) {
                const auto& op = job.operations[k];
                if (op.job_id != job.job_id)
                    throw std::runtime_error("Operation job_id mismatch");
                if (op.op_index != k)
                    throw std::runtime_error("Operation index mismatch");
                if (op.machines.empty())
                    throw std::runtime_error("Operation " + std::to_string(k) +
                        " of job " + std::to_string(job.job_id) + " has no eligible machines");

                std::set<int> seen_machines;
                for (const auto& mt : op.machines) {
                    if (mt.machine_id < 1 || mt.machine_id > meta.num_machines)
                        throw std::runtime_error("Machine ID out of range: " + std::to_string(mt.machine_id));
                    if (mt.proc_time < 1)
                        throw std::runtime_error("Processing time < 1");
                    if (!seen_machines.insert(mt.machine_id).second)
                        throw std::runtime_error("Duplicate machine ID " + std::to_string(mt.machine_id));
                }
                ++counted_ops;
            }
        }
        if (counted_ops != meta.total_ops)
            throw std::runtime_error("total_ops mismatch: counted " +
                std::to_string(counted_ops) + " vs meta " + std::to_string(meta.total_ops));
    }

    // ── JSON output ──

    std::string to_json() const {
        int nj = meta.num_jobs;
        int nm = meta.num_machines;
        int mo = meta.max_ops_in_job;
        std::ostringstream o;
        o << "{\n";
        // metadata
        o << "  \"metadata\": {\n";
        o << "    \"num_jobs\": " << meta.num_jobs << ",\n";
        o << "    \"num_machines\": " << meta.num_machines << ",\n";
        o << "    \"total_ops\": " << meta.total_ops << ",\n";
        o << "    \"max_ops_in_job\": " << meta.max_ops_in_job << ",\n";
        o << "    \"seed\": " << meta.seed << ",\n";
        o << "    \"instance_class\": \"" << meta.instance_class << "\",\n";
        o << "    \"generator_version\": \"" << meta.generator_version << "\",\n";
        o << "    \"pt_distribution\": \"" << meta.pt_distribution << "\",\n";
        // parameters
        o << "    \"parameters\": {\n";
        int pi = 0;
        for (const auto& kv : meta.params) {
            o << "      \"" << kv.first << "\": \"" << kv.second << "\"";
            if (++pi < (int)meta.params.size()) o << ",";
            o << "\n";
        }
        o << "    },\n";
        // machine roles
        o << "    \"machine_roles\": {\n";
        o << "      \"bottleneck_machines\": [";
        for (int i = 0; i < (int)meta.bottleneck_machines.size(); ++i) {
            if (i) o << ", ";
            o << "\"" << format_machine_id(meta.bottleneck_machines[i], nm) << "\"";
        }
        o << "],\n";
        o << "      \"specialist_machines\": [";
        for (int i = 0; i < (int)meta.specialist_machines.size(); ++i) {
            if (i) o << ", ";
            o << "\"" << format_machine_id(meta.specialist_machines[i], nm) << "\"";
        }
        o << "]\n";
        o << "    }\n";
        o << "  },\n";
        // jobs
        o << "  \"jobs\": [\n";
        for (int j = 0; j < (int)jobs.size(); ++j) {
            const auto& job = jobs[j];
            o << "    {\n";
            o << "      \"job_id\": \"" << format_job_id(job.job_id, nj) << "\",\n";
            o << "      \"weight_class\": " << job.weight_class << ",\n";
            o << "      \"num_operations\": " << job.operations.size() << ",\n";
            o << "      \"operations\": [\n";
            for (int k = 0; k < (int)job.operations.size(); ++k) {
                const auto& op = job.operations[k];
                o << "        {\n";
                o << "          \"op_id\": \"" << format_op_id(op.job_id, op.op_index, nj, mo) << "\",\n";
                o << "          \"num_eligible\": " << op.machines.size() << ",\n";
                o << "          \"eligible_machines\": [\n";
                for (int e = 0; e < (int)op.machines.size(); ++e) {
                    const auto& mt = op.machines[e];
                    o << "            {\"machine_id\": \""
                      << format_machine_id(mt.machine_id, nm)
                      << "\", \"processing_time\": " << mt.proc_time << "}";
                    if (e + 1 < (int)op.machines.size()) o << ",";
                    o << "\n";
                }
                o << "          ]\n";
                o << "        }";
                if (k + 1 < (int)job.operations.size()) o << ",";
                o << "\n";
            }
            o << "      ]\n";
            o << "    }";
            if (j + 1 < (int)jobs.size()) o << ",";
            o << "\n";
        }
        o << "  ]\n";
        o << "}\n";
        return o.str();
    }

    // ── Brandimarte text format ──

    std::string to_text() const {
        std::ostringstream o;
        o << meta.num_jobs << " " << meta.num_machines << "\n";
        for (const auto& job : jobs) {
            o << job.operations.size();
            for (const auto& op : job.operations) {
                o << "  " << op.machines.size();
                for (const auto& mt : op.machines) {
                    o << " " << mt.machine_id << " " << mt.proc_time;
                }
            }
            o << "\n";
        }
        return o.str();
    }

    // ── File I/O ──

    void save_json(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot open file: " + path);
        f << to_json();
    }

    void save_text(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot open file: " + path);
        f << to_text();
    }
};

#endif // FJSP_INSTANCE_H
