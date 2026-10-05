/*
 * gen_core.cpp — FJSP Instance Generator Core (Part A)
 *
 * Generates Flexible Job-Shop Scheduling Problem instances using a
 * sophisticated distribution-based pipeline with dependency modeling.
 *
 * Features:
 *   - 6 probability distributions (Normal, LogNormal, Poisson, Gamma,
 *     Bernoulli, Uniform) for realistic data generation
 *   - Job weight classes (light/medium/heavy) that correlate ops count,
 *     processing times, and flexibility
 *   - Machine roles (bottleneck, specialist, normal) that affect
 *     eligible-set composition and time scaling
 *   - Positional dependency within jobs (later ops differ from early ops)
 *   - Controlled noise injection for realism
 *   - 12 named instance classes mapping to concrete parameter sets
 *   - Deterministic seed-based reproduction
 *   - Validity guaranteed by construction + post-validation
 *
 * Build:
 *   g++ -O2 -std=c++17 -o gen_core gen_core.cpp
 *   cl /O2 /std:c++17 /EHsc gen_core.cpp /Fe:gen_core.exe
 *
 * Usage:
 *   ./gen_core --jobs 100 --machines 20 --class average --seed 42
 *              --format json --output instance.json
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "distributions.h"
#include "fjsp_instance.h"

// ──────────────────────────────────────────────────────
//  Generator Parameters
// ──────────────────────────────────────────────────────

struct GenParams {
    // Core (required in parameter-based mode)
    int num_jobs            = -1;
    int num_machines        = -1;
    double ops_mean         = -1.0;
    int ops_max             = -1;
    double flexibility      = -1.0;
    int pt_min              = -1;
    int pt_max              = -1;
    double pt_variance      = -1.0;
    double bottleneck_prob  = -1.0;
    uint64_t seed           = 0;
    bool seed_provided      = false;

    // Extended (optional in parameter-based mode)
    std::string pt_distribution = "";
    double flex_variance        = -1.0;
    double bottleneck_strength  = -1.0;
    int num_bottleneck_machines = -1;
    double advantage_prob       = -1.0;
    double advantage_strength   = -1.0;
    double job_weight_variance  = -1.0;
    double noise_level          = -1.0;
    
    std::string instance_class  = "";

    // Output
    std::string format          = "json";   // "json" or "text" or "both"
    std::string output_path     = "";       // empty = stdout

    /// Store all params into a string map for metadata
    std::map<std::string, std::string> to_map() const {
        std::map<std::string, std::string> m;
        m["num_jobs"]               = std::to_string(num_jobs);
        m["num_machines"]           = std::to_string(num_machines);
        m["ops_per_job_mean"]       = std::to_string(ops_mean);
        m["ops_per_job_max"]        = std::to_string(ops_max);
        m["flexibility"]            = std::to_string(flexibility);
        m["pt_min"]                 = std::to_string(pt_min);
        m["pt_max"]                 = std::to_string(pt_max);
        m["pt_variance"]            = std::to_string(pt_variance);
        m["pt_distribution"]        = pt_distribution;
        m["bottleneck_probability"] = std::to_string(bottleneck_prob);
        m["bottleneck_strength"]    = std::to_string(bottleneck_strength);
        m["num_bottleneck_machines"]= std::to_string(num_bottleneck_machines);
        m["flex_variance"]          = std::to_string(flex_variance);
        m["advantage_probability"]  = std::to_string(advantage_prob);
        m["advantage_strength"]     = std::to_string(advantage_strength);
        m["job_weight_variance"]    = std::to_string(job_weight_variance);
        m["noise_level"]            = std::to_string(noise_level);
        return m;
    }
};

// ──────────────────────────────────────────────────────
//  Named Instance Class Presets
// ──────────────────────────────────────────────────────

/// Apply a named instance class preset to GenParams.
/// Only overrides structural params; jobs/machines/seed are preserved.
bool apply_class_preset(GenParams& p, const std::string& cls) {
    if (cls == "custom" || cls.empty()) return true;  // no override

    // Helper macro for compactness
    #define SET_PRESET(OM,OX,FL,FV,PMN,PMX,PV,PD,BP,BS,BC,AP,AS,JWV,NL) \
        p.ops_mean=OM; p.ops_max=OX; p.flexibility=FL; p.flex_variance=FV; \
        p.pt_min=PMN; p.pt_max=PMX; p.pt_variance=PV; p.pt_distribution=PD; \
        p.bottleneck_prob=BP; p.bottleneck_strength=BS; p.num_bottleneck_machines=BC; \
        p.advantage_prob=AP; p.advantage_strength=AS; p.job_weight_variance=JWV; \
        p.noise_level=NL;

    if (cls == "average") {
        SET_PRESET(5.0,10, 0.50,0.10, 1,30, 0.50,"lognormal", 0.10,0.70,1, 0.10,0.40, 0.30,0.05)
    } else if (cls == "easy") {
        SET_PRESET(3.0,6,  0.80,0.05, 1,20, 0.20,"normal",    0.00,1.00,0, 0.00,1.00, 0.10,0.03)
    } else if (cls == "hard") {
        SET_PRESET(7.0,15, 0.25,0.15, 1,60, 0.80,"lognormal", 0.30,0.40,2, 0.25,0.25, 0.50,0.08)
    } else if (cls == "extreme") {
        SET_PRESET(10.0,20,0.15,0.20, 1,100,1.00,"gamma",     0.50,0.25,3, 0.35,0.15, 0.70,0.12)
    } else if (cls == "bottleneck_heavy") {
        SET_PRESET(5.0,10, 0.40,0.10, 5,40, 0.50,"lognormal", 0.70,0.35,1, 0.00,1.00, 0.30,0.05)
    } else if (cls == "high_flexibility") {
        SET_PRESET(5.0,10, 0.90,0.05, 1,30, 0.50,"lognormal", 0.10,0.70,1, 0.20,0.40, 0.20,0.05)
    } else if (cls == "low_flexibility") {
        SET_PRESET(5.0,10, 0.12,0.05, 1,30, 0.50,"lognormal", 0.10,0.70,1, 0.00,1.00, 0.20,0.05)
    } else if (cls == "unbalanced") {
        SET_PRESET(5.0,15, 0.40,0.15, 1,50, 0.70,"gamma",     0.20,0.55,2, 0.10,0.35, 0.80,0.10)
    } else if (cls == "high_variance") {
        SET_PRESET(5.0,10, 0.50,0.10, 1,100,1.00,"lognormal", 0.10,0.70,1, 0.15,0.30, 0.40,0.15)
    } else if (cls == "machine_advantage") {
        SET_PRESET(5.0,10, 0.50,0.10, 5,40, 0.50,"lognormal", 0.00,1.00,0, 0.65,0.18, 0.30,0.05)
    } else if (cls == "large_scale") {
        SET_PRESET(6.0,12, 0.35,0.12, 1,50, 0.60,"lognormal", 0.15,0.55,2, 0.15,0.35, 0.40,0.07)
    } else if (cls == "small_tight") {
        SET_PRESET(3.0,5,  0.20,0.05, 1,15, 0.25,"normal",    0.00,1.00,0, 0.00,1.00, 0.10,0.03)
    } else {
        std::cerr << "ERROR: Unknown instance class '" << cls << "'\n";
        return false;
    }

    #undef SET_PRESET
    return true;
}

// ──────────────────────────────────────────────────────
//  Instance Generation Pipeline
// ──────────────────────────────────────────────────────

FJSPInstance generate_instance(const GenParams& params) {
    DistEngine dist(params.seed);

    int n = params.num_jobs;
    int m = params.num_machines;

    // ── Step 2: Generate machine roles ──

    // Bottleneck machines
    std::vector<bool> is_bottleneck(m, false);
    std::vector<int> bottleneck_ids;
    int nbneck = std::clamp(params.num_bottleneck_machines, 0, m);
    if (nbneck > 0 && params.bottleneck_prob > 0.0) {
        auto bneck_set = dist.random_subset(m, nbneck);
        for (int mid : bneck_set) {
            is_bottleneck[mid - 1] = true;
            bottleneck_ids.push_back(mid);
        }
    }

    // Specialist machines (~30% of machines, at least 1 if advantage is used)
    std::vector<bool> is_specialist(m, false);
    std::vector<int> specialist_ids;
    int nspec = (params.advantage_prob > 0.0)
              ? std::max(1, (int)std::round(0.3 * m))
              : 0;
    if (nspec > 0) {
        auto spec_set = dist.random_subset(m, nspec);
        for (int mid : spec_set) {
            is_specialist[mid - 1] = true;
            specialist_ids.push_back(mid);
        }
    }

    // Build machine roles vector
    std::vector<MachineRole> machine_roles(m);
    for (int i = 0; i < m; ++i) {
        machine_roles[i].is_bottleneck = is_bottleneck[i];
        machine_roles[i].is_specialist = is_specialist[i];
    }

    // ── Step 3: Generate job weight classes ──

    // Categorical: P(light), P(medium), P(heavy)
    double p_light  = std::max(0.05, 0.3 - params.job_weight_variance * 0.15);
    double p_heavy  = std::max(0.05, 0.3 + params.job_weight_variance * 0.15);
    double p_medium = std::max(0.05, 1.0 - p_light - p_heavy);
    std::vector<double> weight_probs = {p_light, p_medium, p_heavy};
    int weight_map[] = {-1, 0, 1};  // light, medium, heavy

    std::vector<int> job_weights(n);
    for (int j = 0; j < n; ++j) {
        int cat = dist.categorical(weight_probs);
        job_weights[j] = weight_map[cat];
    }

    // ── Step 4 + 5: Generate jobs and operations ──

    std::vector<Job> jobs(n);
    int total_ops = 0;
    int max_ops_in_job = 0;

    for (int j = 0; j < n; ++j) {
        jobs[j].job_id = j + 1;
        jobs[j].weight_class = job_weights[j];

        // Ops count: Poisson(lambda_j), where lambda depends on job weight
        double lambda_j = params.ops_mean;
        if (job_weights[j] == -1) lambda_j *= 0.6;      // light: fewer ops
        else if (job_weights[j] == 1) lambda_j *= 1.5;   // heavy: more ops

        int num_ops = dist.poisson_clamped(lambda_j, 1, params.ops_max);
        max_ops_in_job = std::max(max_ops_in_job, num_ops);

        jobs[j].operations.resize(num_ops);

        for (int k = 0; k < num_ops; ++k) {
            Operation& op = jobs[j].operations[k];
            op.job_id = j + 1;
            op.op_index = k;

            // ── 5a: Sample flexibility ──
            double position_ratio = (num_ops > 1) ? (double)k / (num_ops - 1) : 0.0;

            double effective_flex = params.flexibility;
            // Job-weight modifier: heavy jobs slightly less flexible
            effective_flex += job_weights[j] * (-0.05);
            // Positional decay: later ops slightly more constrained
            effective_flex -= position_ratio * 0.08;
            // Per-operation noise
            effective_flex += dist.normal(0.0, params.flex_variance);
            // Clamp
            effective_flex = std::clamp(effective_flex, 0.01, 1.0);

            int num_eligible = std::max(1, (int)std::round(effective_flex * m));
            num_eligible = std::min(num_eligible, m);

            // ── 5b: Sample eligible machine set (Fisher-Yates) ──
            std::vector<int> pool(m);
            std::iota(pool.begin(), pool.end(), 1);  // {1, 2, ..., m}
            dist.partial_shuffle(pool, num_eligible);
            std::vector<int> eligible(pool.begin(), pool.begin() + num_eligible);

            // Bottleneck forcing
            if (params.bottleneck_prob > 0.0 && !bottleneck_ids.empty()) {
                if (dist.bernoulli(params.bottleneck_prob)) {
                    // Pick a random bottleneck machine
                    int bidx = dist.uniform_int(0, (int)bottleneck_ids.size() - 1);
                    int bneck_machine = bottleneck_ids[bidx];
                    // Add if not already present
                    bool found = false;
                    for (int eid : eligible) {
                        if (eid == bneck_machine) { found = true; break; }
                    }
                    if (!found) {
                        eligible.push_back(bneck_machine);
                    }
                }
            }

            // ── 5c: Generate processing times for each eligible machine ──
            op.machines.resize(eligible.size());

            // Job-weight time scaling
            double job_time_scale = 1.0 + job_weights[j] * params.job_weight_variance * 0.3;

            // Positional time scaling (later ops slightly longer)
            double pos_time_scale = 1.0 + position_ratio * 0.15;

            for (int e = 0; e < (int)eligible.size(); ++e) {
                int mid = eligible[e];
                op.machines[e].machine_id = mid;

                // Base processing time from distribution
                int base_time = dist.gen_proc_time(
                    params.pt_distribution,
                    params.pt_min, params.pt_max,
                    params.pt_variance
                );

                double t = (double)base_time;

                // Apply job-weight correlation
                t *= job_time_scale;

                // Apply positional correlation
                t *= pos_time_scale;

                // Apply machine role scaling
                if (is_bottleneck[mid - 1]) {
                    t *= params.bottleneck_strength;  // < 1 = faster
                }

                // Apply specialist advantage
                if (is_specialist[mid - 1] && params.advantage_prob > 0.0) {
                    if (dist.bernoulli(params.advantage_prob)) {
                        t *= params.advantage_strength;  // < 1 = faster
                    }
                }

                // Apply noise
                if (params.noise_level > 0.0) {
                    double noise_mult = dist.uniform_real(
                        1.0 - params.noise_level,
                        1.0 + params.noise_level
                    );
                    t *= noise_mult;
                }

                // Final clamp: ensure >= 1 and within range
                int final_time = (int)std::round(t);
                final_time = std::clamp(final_time, 1, params.pt_max);
                final_time = std::max(1, final_time);

                op.machines[e].proc_time = final_time;
            }

            // Sort eligible machines by machine_id for consistent output
            std::sort(op.machines.begin(), op.machines.end(),
                [](const MachineTime& a, const MachineTime& b) {
                    return a.machine_id < b.machine_id;
                });

            ++total_ops;
        }
    }

    // ── Step 6: Assemble instance ──

    FJSPInstance instance;
    instance.jobs = std::move(jobs);
    instance.machine_roles = std::move(machine_roles);

    instance.meta.num_jobs = n;
    instance.meta.num_machines = m;
    instance.meta.total_ops = total_ops;
    instance.meta.max_ops_in_job = max_ops_in_job;
    instance.meta.seed = params.seed;
    instance.meta.instance_class = params.instance_class;
    instance.meta.generator_version = "gen_core_v1";
    instance.meta.pt_distribution = params.pt_distribution;
    instance.meta.params = params.to_map();
    instance.meta.bottleneck_machines = bottleneck_ids;
    instance.meta.specialist_machines = specialist_ids;

    // ── Step 7: Validate ──
    try {
        instance.validate();
    } catch (const std::exception& e) {
        std::cerr << "VALIDATION FAILED: " << e.what() << "\n";
        std::exit(2);
    }

    return instance;
}

// ──────────────────────────────────────────────────────
//  CLI Argument Parsing
// ──────────────────────────────────────────────────────

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
        << "FJSP Instance Generator — Part A\n\n"
        << "Required:\n"
        << "  --jobs N              Number of jobs (default: 10)\n"
        << "  --machines N          Number of machines (default: 5)\n"
        << "  --seed N              PRNG seed, uint64 (default: 42)\n"
        << "\nStructural:\n"
        << "  --class NAME          Instance class preset (overrides structural params)\n"
        << "                        Options: average, easy, hard, extreme, bottleneck_heavy,\n"
        << "                        high_flexibility, low_flexibility, unbalanced,\n"
        << "                        high_variance, machine_advantage, large_scale, small_tight\n"
        << "  --ops-mean F          Mean operations per job (default: 5.0)\n"
        << "  --ops-max N           Max operations per job (default: 10)\n"
        << "  --flexibility F       Mean machine flexibility 0-1 (default: 0.5)\n"
        << "  --flex-var F          Flexibility variance 0-0.5 (default: 0.1)\n"
        << "  --pt-min N            Min processing time (default: 1)\n"
        << "  --pt-max N            Max processing time (default: 30)\n"
        << "  --pt-var F            Processing time variance 0-1 (default: 0.5)\n"
        << "  --pt-dist NAME        Distribution: normal, lognormal, gamma, uniform (default: lognormal)\n"
        << "  --bneck-prob F        Bottleneck probability 0-1 (default: 0.1)\n"
        << "  --bneck-strength F    Bottleneck speed multiplier 0-1 (default: 0.7)\n"
        << "  --bneck-count N       Number of bottleneck machines (default: 1)\n"
        << "  --adv-prob F          Machine advantage probability 0-1 (default: 0.1)\n"
        << "  --adv-strength F      Advantage speed multiplier 0-1 (default: 0.4)\n"
        << "  --job-weight-var F    Job weight variance 0-1 (default: 0.3)\n"
        << "  --noise F             Noise level 0-0.5 (default: 0.05)\n"
        << "\nOutput:\n"
        << "  --format FMT          Output format: json, text, both (default: json)\n"
        << "  --output PATH         Output file path (default: stdout)\n"
        << "  --help                Show this help message\n";
}

GenParams parse_args(int argc, char* argv[]) {
    GenParams p;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }

        // Helper: get next arg as value
        auto next_str = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "ERROR: Missing value for " << arg << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        auto next_int = [&]() -> int { return std::stoi(next_str()); };
        auto next_uint64 = [&]() -> uint64_t { return std::stoull(next_str()); };
        auto next_double = [&]() -> double { return std::stod(next_str()); };

        if (arg == "--jobs")            p.num_jobs = next_int();
        else if (arg == "--machines")   p.num_machines = next_int();
        else if (arg == "--seed")       { p.seed = next_uint64(); p.seed_provided = true; }
        else if (arg == "--class")      p.instance_class = next_str();
        else if (arg == "--ops-mean")   p.ops_mean = next_double();
        else if (arg == "--ops-max")    p.ops_max = next_int();
        else if (arg == "--flexibility")p.flexibility = next_double();
        else if (arg == "--flex-var")   p.flex_variance = next_double();
        else if (arg == "--pt-min")     p.pt_min = next_int();
        else if (arg == "--pt-max")     p.pt_max = next_int();
        else if (arg == "--pt-var")     p.pt_variance = next_double();
        else if (arg == "--pt-dist")    p.pt_distribution = next_str();
        else if (arg == "--bneck-prob") p.bottleneck_prob = next_double();
        else if (arg == "--bneck-strength") p.bottleneck_strength = next_double();
        else if (arg == "--bneck-count")p.num_bottleneck_machines = next_int();
        else if (arg == "--adv-prob")   p.advantage_prob = next_double();
        else if (arg == "--adv-strength")p.advantage_strength = next_double();
        else if (arg == "--job-weight-var") p.job_weight_variance = next_double();
        else if (arg == "--noise")      p.noise_level = next_double();
        else if (arg == "--format")     p.format = next_str();
        else if (arg == "--output")     p.output_path = next_str();
        else {
            std::cerr << "ERROR: Unknown argument '" << arg << "'\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return p;
}

// ──────────────────────────────────────────────────────
//  Main
// ──────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    GenParams params = parse_args(argc, argv);

    // Apply modes: (1) Class preset, (2) Required-standard-parameter, (3) Extended-parameter
    if (!params.instance_class.empty()) {
        // MODE 1: Class preset mode
        if (params.num_jobs == -1) params.num_jobs = 10;
        if (params.num_machines == -1) params.num_machines = 5;
        if (!params.seed_provided) params.seed = 42;
        
        if (!apply_class_preset(params, params.instance_class)) {
            return 1;
        }
    } else {
        // MODE 2 & 3: Parameter-based modes
        // Ensure ALL core parameters are explicitly provided
        if (params.num_jobs == -1 || params.num_machines == -1 || params.ops_mean < 0.0 || 
            params.flexibility < 0.0 || params.pt_min == -1 || params.pt_max == -1 || 
            params.pt_variance < 0.0 || params.bottleneck_prob < 0.0 || !params.seed_provided) {
            std::cerr << "ERROR: In parameter-based mode (no --class), you must explicitly provide all core parameters:\n"
                      << "       --jobs, --machines, --ops-mean, --flexibility, --pt-min, --pt-max, --pt-var, --bneck-prob, --seed\n";
            return 1;
        }
        
        // Mode 3 optional extended parameters use sensible defaults if not provided
        if (params.pt_distribution.empty()) params.pt_distribution = "lognormal";
        if (params.flex_variance < 0.0) params.flex_variance = 0.1;
        if (params.bottleneck_strength < 0.0) params.bottleneck_strength = 0.7;
        if (params.num_bottleneck_machines == -1) params.num_bottleneck_machines = 1;
        if (params.advantage_prob < 0.0) params.advantage_prob = 0.1;
        if (params.advantage_strength < 0.0) params.advantage_strength = 0.4;
        if (params.job_weight_variance < 0.0) params.job_weight_variance = 0.3;
        if (params.noise_level < 0.0) params.noise_level = 0.05;
        if (params.ops_max == -1) params.ops_max = (int)std::ceil(params.ops_mean * 2.0);
        
        params.instance_class = "custom"; // Mark for metadata
    }

    // Validate parameter ranges
    if (params.num_jobs < 1 || params.num_machines < 1) {
        std::cerr << "ERROR: num_jobs and num_machines must be >= 1\n";
        return 1;
    }
    if (params.pt_min < 1 || params.pt_max < params.pt_min) {
        std::cerr << "ERROR: Need pt_min >= 1 and pt_max >= pt_min\n";
        return 1;
    }

    // Generate
    FJSPInstance instance = generate_instance(params);

    // Output
    auto write_output = [&](const std::string& content, const std::string& suffix) {
        if (params.output_path.empty()) {
            std::cout << content;
        } else {
            std::string path = params.output_path;
            if (!suffix.empty()) {
                // Insert suffix before extension
                auto dot = path.rfind('.');
                if (dot != std::string::npos) {
                    path = path.substr(0, dot) + suffix + path.substr(dot);
                } else {
                    path += suffix;
                }
            }
            std::ofstream f(path);
            if (!f) {
                std::cerr << "ERROR: Cannot write to " << path << "\n";
                std::exit(1);
            }
            f << content;
            std::cerr << "Wrote: " << path << "\n";
        }
    };

    if (params.format == "json" || params.format == "both") {
        write_output(instance.to_json(), (params.format == "both") ? "" : "");
    }
    if (params.format == "text") {
        write_output(instance.to_text(), "");
    }
    if (params.format == "both") {
        // Write text version with _text suffix
        std::string text_path = params.output_path;
        if (!text_path.empty()) {
            auto dot = text_path.rfind('.');
            if (dot != std::string::npos) {
                text_path = text_path.substr(0, dot) + ".txt";
            } else {
                text_path += ".txt";
            }
            std::ofstream f(text_path);
            if (!f) {
                std::cerr << "ERROR: Cannot write to " << text_path << "\n";
                std::exit(1);
            }
            f << instance.to_text();
            std::cerr << "Wrote: " << text_path << "\n";
        } else {
            std::cout << "\n--- Brandimarte Text Format ---\n";
            std::cout << instance.to_text();
        }
    }

    // Print summary to stderr
    std::cerr << "Generated: " << params.instance_class
              << " | " << instance.meta.num_jobs << " jobs"
              << " | " << instance.meta.num_machines << " machines"
              << " | " << instance.meta.total_ops << " total ops"
              << " | seed=" << instance.meta.seed
              << " | dist=" << params.pt_distribution
              << "\n";

    return 0;
}
