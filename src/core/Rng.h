#pragma once
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

// Seedable RNG so a run can be reproduced exactly (pass --seed N).
class Rng {
public:
    explicit Rng(std::uint32_t seed = std::random_device{}()) : seed_(seed), gen_(seed) {}

    void reseed(std::uint32_t seed) { seed_ = seed; gen_.seed(seed); }
    std::uint32_t seed() const { return seed_; }

    int range(int a, int b) {
        std::uniform_int_distribution<int> d(std::min(a, b), std::max(a, b));
        return d(gen_);
    }
    double real(double a = 0.0, double b = 1.0) {
        std::uniform_real_distribution<double> d(std::min(a, b), std::max(a, b));
        return d(gen_);
    }
    bool chance(double p) { return real() < p; }

    // Returns an index into `weights` (weights don't need to sum to anything specific).
    std::size_t weighted(const std::vector<double>& weights) {
        double total = 0;
        for (double w : weights) total += std::max(0.0, w);
        if (total <= 0) return 0;
        double r = real(0, total);
        for (std::size_t i = 0; i < weights.size(); ++i) {
            r -= std::max(0.0, weights[i]);
            if (r <= 0) return i;
        }
        return weights.size() - 1;
    }

    template <class T>
    const T& pick(const std::vector<T>& v) { return v[static_cast<std::size_t>(range(0, int(v.size()) - 1))]; }

private:
    std::uint32_t seed_;
    std::mt19937 gen_;
};
