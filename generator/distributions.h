/*
 * distributions.h — Probability distribution wrappers for FJSP generator
 *
 * All distributions are built on top of C++ <random> only.
 * No external libraries (NumPy, SciPy, Boost) are used.
 *
 * Distributions provided:
 *   1. Normal          — bell-shaped, for flexibility perturbation
 *   2. LogNormal       — right-skewed, for realistic processing times
 *   3. Poisson         — count distribution, for ops-per-job
 *   4. Gamma           — tunable skew, for structured processing times
 *   5. Bernoulli       — binary decisions (bottleneck forcing, advantage)
 *   6. DiscreteUniform — machine selection, tiebreaking
 *   7. ContinuousUniform — noise injection
 *
 * All wrappers take a reference to mt19937_64 and return values.
 * This ensures all randomness flows through a single seeded PRNG.
 */

#ifndef DISTRIBUTIONS_H
#define DISTRIBUTIONS_H

#include <random>
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>

// ──────────────────────────────────────────────────────
//  Distribution Engine
//  Wraps all distribution sampling behind a single PRNG.
// ──────────────────────────────────────────────────────

class DistEngine {
public:
    explicit DistEngine(uint64_t seed) : rng_(seed) {}

    /// Re-seed the engine (for testing)
    void seed(uint64_t s) { rng_.seed(s); }

    /// Get a reference to the raw PRNG (for custom use)
    std::mt19937_64& rng() { return rng_; }

    // ── Normal Distribution N(mu, sigma) ──

    /// Sample from N(mu, sigma)
    double normal(double mu, double sigma) {
        if (sigma <= 0.0) return mu;
        std::normal_distribution<double> dist(mu, sigma);
        return dist(rng_);
    }

    /// Sample from N(mu, sigma) and clamp to [lo, hi]
    double normal_clamped(double mu, double sigma, double lo, double hi) {
        double val = normal(mu, sigma);
        return std::clamp(val, lo, hi);
    }

    // ── LogNormal Distribution LogN(mu_ln, sigma_ln) ──
    // E[X] = exp(mu_ln + sigma_ln^2 / 2)
    // To target a specific median M: mu_ln = ln(M), sigma_ln controls spread.

    /// Sample from LogN(mu_ln, sigma_ln)
    double lognormal(double mu_ln, double sigma_ln) {
        if (sigma_ln <= 0.0) return std::exp(mu_ln);
        std::lognormal_distribution<double> dist(mu_ln, sigma_ln);
        return dist(rng_);
    }

    /// Sample LogNormal targeting a desired median, with spread control.
    /// median: desired center value
    /// spread: controls sigma_ln (0.0 = tight, 1.0 = heavy tail)
    double lognormal_median(double median, double spread) {
        if (median <= 0.0) median = 1.0;
        double sigma_ln = 0.1 + spread * 0.9;  // maps [0,1] -> [0.1, 1.0]
        double mu_ln = std::log(median);
        // Note: NOT adjusting for mean vs median; we want median-centered output
        return lognormal(mu_ln, sigma_ln);
    }

    // ── Poisson Distribution Pois(lambda) ──

    /// Sample from Pois(lambda), returns non-negative integer
    int poisson(double lambda) {
        if (lambda <= 0.0) return 0;
        std::poisson_distribution<int> dist(lambda);
        return dist(rng_);
    }

    /// Sample Poisson clamped to [lo, hi]
    int poisson_clamped(double lambda, int lo, int hi) {
        int val = poisson(lambda);
        return std::clamp(val, lo, hi);
    }

    // ── Gamma Distribution Gamma(alpha, beta) ──
    // E[X] = alpha * beta

    /// Sample from Gamma(alpha, beta)
    double gamma(double alpha, double beta) {
        if (alpha <= 0.0 || beta <= 0.0) return 0.0;
        std::gamma_distribution<double> dist(alpha, beta);
        return dist(rng_);
    }

    /// Sample Gamma targeting a desired mean, with shape control.
    /// mean: desired E[X]
    /// shape_param: controls alpha; higher = more peaked/Normal-like
    double gamma_mean(double mean, double shape_param) {
        if (mean <= 0.0) mean = 1.0;
        double alpha = std::max(0.1, shape_param);
        double beta = mean / alpha;
        return gamma(alpha, beta);
    }

    // ── Bernoulli Distribution Bern(p) ──

    /// Sample from Bern(p): returns true with probability p
    bool bernoulli(double p) {
        if (p <= 0.0) return false;
        if (p >= 1.0) return true;
        std::bernoulli_distribution dist(p);
        return dist(rng_);
    }

    // ── Discrete Uniform U{lo, hi} (inclusive) ──

    /// Sample uniformly from {lo, lo+1, ..., hi}
    int uniform_int(int lo, int hi) {
        if (lo >= hi) return lo;
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(rng_);
    }

    // ── Continuous Uniform U(lo, hi) ──

    /// Sample uniformly from [lo, hi]
    double uniform_real(double lo, double hi) {
        if (lo >= hi) return lo;
        std::uniform_real_distribution<double> dist(lo, hi);
        return dist(rng_);
    }

    // ── Categorical Distribution ──

    /// Sample from a categorical distribution with given probabilities.
    /// Returns index in [0, probs.size()-1].
    /// probs need not sum to 1; they are normalized internally.
    int categorical(const std::vector<double>& probs) {
        if (probs.empty()) return 0;
        double total = 0.0;
        for (double p : probs) total += p;
        if (total <= 0.0) return 0;
        double r = uniform_real(0.0, total);
        double cumulative = 0.0;
        for (int i = 0; i < (int)probs.size(); ++i) {
            cumulative += probs[i];
            if (r <= cumulative) return i;
        }
        return (int)probs.size() - 1;
    }

    // ── Fisher-Yates Partial Shuffle ──
    // Selects k elements from pool without replacement.
    // Modifies pool in-place; selected elements are in pool[0..k-1].

    void partial_shuffle(std::vector<int>& pool, int k) {
        int n = (int)pool.size();
        k = std::min(k, n);
        for (int i = 0; i < k; ++i) {
            int j = uniform_int(i, n - 1);
            std::swap(pool[i], pool[j]);
        }
    }

    /// Select k elements from {1, 2, ..., n} without replacement.
    /// Returns a vector of size k.
    std::vector<int> random_subset(int n, int k) {
        k = std::clamp(k, 0, n);
        std::vector<int> pool(n);
        std::iota(pool.begin(), pool.end(), 1);  // 1, 2, ..., n
        partial_shuffle(pool, k);
        return std::vector<int>(pool.begin(), pool.begin() + k);
    }

    // ── Processing Time from Distribution ──
    // Generates a processing time using the specified distribution,
    // centered around a target median within [pt_min, pt_max],
    // with the given variance control.

    int gen_proc_time(const std::string& dist_name,
                      int pt_min, int pt_max, double pt_variance) {
        double median = (pt_min + pt_max) / 2.0;
        double half_range = (pt_max - pt_min) / 2.0;
        double raw;

        if (dist_name == "lognormal") {
            raw = lognormal_median(median, pt_variance);
        } else if (dist_name == "normal") {
            double sigma = half_range * pt_variance * 0.5;
            raw = normal(median, sigma);
        } else if (dist_name == "gamma") {
            // Higher alpha = lower variance (more peaked)
            double alpha = 1.0 + (1.0 - pt_variance) * 10.0;
            double beta = median / alpha;
            raw = gamma(alpha, beta);
        } else {
            // uniform fallback
            double spread = half_range * pt_variance;
            raw = uniform_real(median - spread, median + spread);
        }

        // Clamp and ensure >= 1
        int result = (int)std::round(raw);
        result = std::clamp(result, pt_min, pt_max);
        result = std::max(1, result);
        return result;
    }

private:
    std::mt19937_64 rng_;
};

#endif // DISTRIBUTIONS_H
