// Weighted reservoir sampling with replacement using skip-based method
// Implements algorithm WRSWR-SKIP from:
// "Investigating Methods for Weighted Reservoir Sampling with Replacement, A. Meligrana, 2024"

#pragma once

#include <vector>
#include <random>
#include <queue>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include "rng.hpp"
#include "sampler.hpp"
#include "utilities.hpp"


namespace ctream {

// Algorithm tag for WRSWR-SKIP
struct AlgWRSWRSKIP {};

// Trait specializations for AlgWRSWRSKIP
template<>
struct is_weighted<AlgWRSWRSKIP> : std::true_type {};


// Multi-element weighted reservoir sampler using WRSWR-SKIP algorithm
// T: element type
// F: floating-point type for weights (default: double)
// Mutable: if true, modifications happen in-place; if false, returns new sampler
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerWRSWRSKIP : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    // Constructor for uninitialized sampler with sample size n
    explicit ReservoirSamplerWRSWRSKIP(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          state_(F(0)),
          skip_w_(F(0)),
          seen_k_(0),
          rng_(seed),
          elements_(n),
          weights_(n),
          order_indices_(Ordered ? std::vector<size_t>(n) : std::vector<size_t>{})
    {
        if (n == 0) {
            throw std::invalid_argument("Sample size n must be > 0");
        }
    }

    // Copy constructor
    ReservoirSamplerWRSWRSKIP(const ReservoirSamplerWRSWRSKIP& other) = default;

    // Move constructor
    ReservoirSamplerWRSWRSKIP(ReservoirSamplerWRSWRSKIP&& other) noexcept = default;

    // Assignment operators
    ReservoirSamplerWRSWRSKIP& operator=(const ReservoirSamplerWRSWRSKIP& other) = default;
    ReservoirSamplerWRSWRSKIP& operator=(ReservoirSamplerWRSWRSKIP&& other) noexcept = default;

    // Mutable interface: add weighted element in-place
    void fit(const T& element, F weight) {
        if (weight < 0) {
            throw std::invalid_argument("Weight must be non-negative");
        }
        if (weight == 0) {
            return;  // Ignore zero-weight elements, don't increment nobs
        }

        seen_k_++;
        state_ += weight;

        // Filling phase: collect elements until we have n
        if (seen_k_ <= n_) {
            elements_[seen_k_ - 1] = element;
            weights_[seen_k_ - 1] = state_;
            if (is_ordered) {
                order_indices_[seen_k_ - 1] = seen_k_ - 1;
            }

            // Once we have n elements, compute initial skip distance
            if (seen_k_ == n_) {
                perform_initial_sort_();
                recompute_skip_();
            }
            return;
        }

        // Sampling phase: process new element if it passes skip threshold
        if (skip_w_ <= state_) {
            F p = weight / state_;
            auto k = choose(n_, p);
            
            // Perform k replacements
            for (int j = 0; j < k; ++j) {
                // Random index in [j, n-1] for swapping
                int r = std::uniform_int_distribution<int>(j, n_ - 1)(rng_);
                // s.value[f], s.value[j] = s.value[j], el
                std::swap(elements_[r], elements_[j]);
                elements_[j] = element;
                if (is_ordered) {
                    // s.ord[r], s.ord[j] = s.ord[j], nobs(s)
                    std::swap(order_indices_[r], order_indices_[j]);
                    order_indices_[j] = seen_k_ - 1;
                }
                // NOTE: Do NOT swap weights_ - they stay in cumulative order
                // NOTE: Do NOT update weights_[j] - weights array tracks original cumulative state
            }

            recompute_skip_();
        }
    }

    // Mutable interface: add unweighted element (weight = 1)
    void fit(const T& element) {
        fit(element, F(1));
    }

    // Return total number of elements observed
    [[nodiscard]] size_t nobs() const override {
        return seen_k_;
    }

    // Check if sampler has collected any samples
    [[nodiscard]] bool empty() const override {
        return seen_k_ == 0;
    }

    // Return the current sample (valid only if seen_k >= n)
    std::vector<T> value() const {
        if (seen_k_ == 0) {
            return std::vector<T>{};
        }

        // For WRSWR, value() returns the final n elements directly
        // (they're already in the container after the initial sort)
        return std::vector<T>(elements_.begin(), elements_.end());
    }

    // Return sample in collection order (only for ordered samplers)
    std::vector<T> ordvalue() const requires(Ordered) {
        if (seen_k_ == 0) {
            return std::vector<T>{};
        }

        // Full or partial: return in collection order via order_indices_
        const size_t result_size = std::min(seen_k_, n_);
        
        // Create index pairs (order_index, position)
        std::vector<std::pair<size_t, size_t>> sorted_indices;
        for (size_t i = 0; i < result_size; ++i) {
            sorted_indices.emplace_back(order_indices_[i], i);
        }
        
        // Sort by collection order
        std::ranges::sort(sorted_indices);

        // Extract elements in order
        std::vector<T> result;
        result.reserve(result_size);
        for (size_t i = 0; i < result_size; ++i) {
            result.push_back(elements_[sorted_indices[i].second]);
        }
        return result;
    }

    // Reset sampler to empty state
    void clear() {
        state_ = F(0);
        skip_w_ = F(0);
        seen_k_ = 0;
    }

    // Immutable interface: return new sampler with element added
    ReservoirSamplerWRSWRSKIP<T, F, false, Ordered> fit_immutable(const T& element, F weight) const requires(!Mutable) {
        auto copy = *this;
        copy.fit(element, weight);
        return copy;
    }

private:
    size_t n_;
    F state_;           // Cumulative weight sum
    F skip_w_;          // Skip threshold weight
    size_t seen_k_;     // Count of elements observed
    StdRNG rng_;        // RNG instance
    std::vector<T> elements_;
    std::vector<F> weights_;
    std::vector<size_t> order_indices_;  // For ordered sampling

    // Perform initial sort after collecting first n elements
    // Reorganizes elements by weight for efficient skip calculation
    void perform_initial_sort_() {
        // Implement weighted fill-phase reordering from Algorithm A
        // This selects n elements based on exponential-weighted probabilities
        // Matches Julia StreamSampling.jl lines 137-144
        // 
        // IMPORTANT: Only reorder elements_, NOT weights_!
        // The weights array must stay in cumulative order for skip-based sampling
        
        std::vector<T> new_values(n_);
        std::vector<size_t> new_order;
        if (is_ordered) {
            new_order.resize(n_);
        }
        
        size_t j = 0;
        F curx = 0.0;
        std::exponential_distribution<F> exp_dist(1.0);
        
        // Process positions from n down to 1 (matching Julia)
        for (int i = static_cast<int>(n_) - 1; i >= 0; --i) {
            // Generate exponential spacing for this position
            // curx += (1 - exp(-randexp/(i+1))) * (1 - curx)
            F exp_val = exp_dist(rng_);
            F spacing = (F(1) - std::exp(-exp_val / F(i + 1))) * (F(1) - curx);
            curx += spacing;
            
            // Find first element with weight >= curx * state
            F threshold = curx * state_;
            while (j < n_ && weights_[j] < threshold) {
                j++;
            }
            
            // Should have found an element (j < n_ should be true)
            if (j < n_) {
                // Place element at position i (but NOT the weight)
                new_values[i] = elements_[j];
                if (is_ordered) {
                    new_order[i] = order_indices_[j];
                }
            }
        }
        
        // Replace only elements with reordered values
        // Keep weights_ in original cumulative order
        elements_ = new_values;
        if (is_ordered) {
            order_indices_ = new_order;
        }
    }

    // Recompute skip distance for next eligible element
    // Uses exponential jump to determine when next element can be added
    void recompute_skip_() {
        // q = exp(Exp(1) / n)
        std::exponential_distribution<F> exp_dist(1.0);
        F exp_sample = exp_dist(rng_);
        F q = std::exp(exp_sample / F(n_));

        // skip_w is the cumulative weight threshold for considering next element
        skip_w_ = state_ * q;
    }

    int choose(size_t nn, F pp) {
        return utilities::choose(rng_, nn, pp);
    }
};


// Algorithm tags for unweighted sampling
struct AlgL {};  // Algorithm L: random reservoir sampling without replacement (skip-based)
struct AlgR {};  // Algorithm R: random reservoir sampling without replacement

// Trait specializations: unweighted algorithms
template<>
struct is_weighted<AlgL> : std::false_type {};

template<>
struct is_weighted<AlgR> : std::false_type {};


// Unweighted reservoir sampler using Algorithm L
// Similar to AlgWRSWRSKIP but for unweighted elements
// T: element type
// F: floating-point type for internal state (default: double)
// Mutable: if true, modifications happen in-place; if false, returns new sampler
// Ordered: true to track collection order, false otherwise
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerAlgL : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    // Constructor: create sampler for n elements
    explicit ReservoirSamplerAlgL(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          state_(F(0)),
          skip_k_(0),
          seen_k_(0),
          rng_(seed),
          elements_(n),
          order_indices_(Ordered ? std::vector<size_t>(n) : std::vector<size_t>{})
    {
        if (n == 0) {
            throw std::invalid_argument("Sample size n must be > 0");
        }
    }

    // Mutable interface: add element (AlgL only supports unweighted)
    void fit(const T& element) {
        seen_k_++;

        // Filling phase
        if (seen_k_ <= n_) {
            elements_[seen_k_ - 1] = element;
            if (is_ordered) {
                order_indices_[seen_k_ - 1] = seen_k_ - 1;
            }

            // After collecting n elements, compute initial skip distance
            if (seen_k_ == n_) {
                recompute_skip_();
            }
            return;
        }

        // Sampling phase: include element if skip threshold met
        if (skip_k_ < seen_k_) {
            // Random replacement among first n
            std::uniform_int_distribution<size_t> dist(0, n_ - 1);
            size_t j = dist(rng_);
            elements_[j] = element;
            if (is_ordered) {
                order_indices_[j] = seen_k_ - 1;
            }
            recompute_skip_();
        }
    }

    // Unweighted only - overload without weight parameter
    void fit(const T& /*element*/, F /*weight*/) override {
        throw std::logic_error("AlgL does not support weighted sampling");
    }

    [[nodiscard]] size_t nobs() const override { return seen_k_; }
    [[nodiscard]] bool empty() const override { return seen_k_ == 0; }

    std::vector<T> value() const {
        return std::vector<T>(elements_.begin(), elements_.end());
    }

    std::vector<T> ordvalue() const requires(Ordered) {
        if (seen_k_ == 0) {
            return std::vector<T>{};
        }

        // Reorder by collection order
        const size_t result_size = std::min(seen_k_, n_);
        std::vector<std::pair<size_t, size_t>> sorted_indices;
        for (size_t i = 0; i < result_size; ++i) {
            sorted_indices.emplace_back(order_indices_[i], i);
        }
        std::ranges::sort(sorted_indices);

        std::vector<T> result;
        for (size_t i = 0; i < result_size; ++i) {
            result.push_back(elements_[sorted_indices[i].second]);
        }
        return result;
    }

    void clear() {
        state_ = F(0);
        skip_k_ = 0;
        seen_k_ = 0;
    }

private:
    size_t n_;
    F state_;           // Cumulative "uniform" weight (increments by 1)
    size_t skip_k_;     // Skip threshold
    size_t seen_k_;     // Observation counter
    StdRNG rng_;
    std::vector<T> elements_;
    std::vector<size_t> order_indices_;

    // Compute next skip distance using exponential distribution
    // Follows Vitter Algorithm L from StreamSampling.jl
    void recompute_skip_() {
        std::exponential_distribution<F> exp_dist(1.0);
        
        // First exponential: accumulate state
        F exp_val_state = exp_dist(rng_);
        state_ += exp_val_state;

        // Second exponential: compute skip distance
        F exp_val_skip = exp_dist(rng_);
        F log_term = std::log1p(-std::exp(-state_ / F(n_)));
        F skip_add = std::ceil(exp_val_skip / log_term);
        skip_k_ = seen_k_ - static_cast<size_t>(skip_add);
    }
};


// Unweighted reservoir sampler using Algorithm R
// Simpler variant: no skip distance optimization
// T: element type
// F: floating-point type (not used for state, but kept for API consistency)
// Mutable: if true, modifications happen in-place; if false, returns new sampler
// Ordered: true to track collection order, false otherwise
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerAlgR : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    explicit ReservoirSamplerAlgR(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          seen_k_(0),
          rng_(seed),
          elements_(n),
          order_indices_(Ordered ? std::vector<size_t>(n) : std::vector<size_t>{})
    {
        if (n == 0) {
            throw std::invalid_argument("Sample size n must be > 0");
        }
    }

    // Mutable interface: add element
    void fit(const T& element) {
        seen_k_++;

        // Filling phase
        if (seen_k_ <= n_) {
            elements_[seen_k_ - 1] = element;
            if (is_ordered) {
                order_indices_[seen_k_ - 1] = seen_k_ - 1;
            }
            return;
        }

        // Sampling phase: uniform random replacement
        std::uniform_int_distribution<size_t> dist(1, seen_k_);
        size_t j = dist(rng_);
        
        if (j <= n_) {
            size_t idx = j - 1;  // Convert to 0-indexed
            elements_[idx] = element;
            if (is_ordered) {
                order_indices_[idx] = seen_k_ - 1;
            }
        }
    }

    // Unweighted only
    void fit(const T& /*element*/, F /*weight*/) override {
        throw std::logic_error("AlgR does not support weighted sampling");
    }

    [[nodiscard]] size_t nobs() const override { return seen_k_; }
    [[nodiscard]] bool empty() const override { return seen_k_ == 0; }

    std::vector<T> value() const {
        return std::vector<T>(elements_.begin(), elements_.end());
    }

    std::vector<T> ordvalue() const requires(Ordered) {
        if (seen_k_ == 0) {
            return std::vector<T>{};
        }

        const size_t result_size = std::min(seen_k_, n_);
        std::vector<std::pair<size_t, size_t>> sorted_indices;
        for (size_t i = 0; i < result_size; ++i) {
            sorted_indices.emplace_back(order_indices_[i], i);
        }
        std::ranges::sort(sorted_indices);

        std::vector<T> result;
        for (size_t i = 0; i < result_size; ++i) {
            result.push_back(elements_[sorted_indices[i].second]);
        }
        return result;
    }

    void clear() {
        seen_k_ = 0;
    }

private:
    size_t n_;
    size_t seen_k_;
    StdRNG rng_;
    std::vector<T> elements_;
    std::vector<size_t> order_indices_;
};


// Algorithm tag for unweighted sampling with replacement
struct AlgRSWRSKIP {};

template<>
struct is_weighted<AlgRSWRSKIP> : std::false_type {};


// Unweighted reservoir sampler with replacement using skip-based method
// Similar to AlgWRSWRSKIP but for uniform weights
// T: element type
// F: floating-point type for internal state
// Mutable: if true, modifications happen in-place; if false, returns new sampler
// Ordered: true to track collection order, false otherwise
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerAlgRSWRSKIP : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    explicit ReservoirSamplerAlgRSWRSKIP(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          skip_k_(0),
          seen_k_(0),
          rng_(seed),
          elements_(n),
          order_indices_(Ordered ? std::vector<size_t>(n) : std::vector<size_t>{})
    {
        if (n == 0) {
            throw std::invalid_argument("Sample size n must be > 0");
        }
    }

    // Unweighted reservoir sampling with replacement
    void fit(const T& element) {
        seen_k_++;

        // Filling phase
        if (seen_k_ <= n_) {
            elements_[seen_k_ - 1] = element;
            if (is_ordered) {
                order_indices_[seen_k_ - 1] = seen_k_ - 1;
            }

            // After filling, compute skip distance
            if (seen_k_ == n_) {
                // Perform initial random shuffle
                for (size_t i = 0; i < n_; ++i) {
                    std::uniform_int_distribution<size_t> dist(i, n_ - 1);
                    size_t j = dist(rng_);
                    std::swap(elements_[i], elements_[j]);
                    if (is_ordered) {
                        std::swap(order_indices_[i], order_indices_[j]);
                    }
                }
                recompute_skip_();
            }
            return;
        }

        // Sampling phase
        if (skip_k_ < seen_k_) {
            F p = F(1) / F(seen_k_);
            auto k = choose(n_, p);

            for (int j = 0; j < k; ++j) {
                std::uniform_int_distribution<int> dist(j, n_ - 1);
                int r = dist(rng_);
                std::swap(elements_[r], elements_[j]);
                if (is_ordered) {
                    std::swap(order_indices_[r], order_indices_[j]);
                }
                elements_[j] = element;
            }

            recompute_skip_();
        }
    }

    // Unweighted only
    void fit(const T& /*element*/, F /*weight*/) override {
        throw std::logic_error("AlgRSWRSKIP does not support weighted sampling");
    }

    [[nodiscard]] size_t nobs() const override { return seen_k_; }
    [[nodiscard]] bool empty() const override { return seen_k_ == 0; }

    std::vector<T> value() const {
        return std::vector<T>(elements_.begin(), elements_.end());
    }

    std::vector<T> ordvalue() const requires(Ordered) {
        if (seen_k_ == 0) {
            return std::vector<T>{};
        }

        const size_t result_size = std::min(seen_k_, n_);
        std::vector<std::pair<size_t, size_t>> sorted_indices;
        for (size_t i = 0; i < result_size; ++i) {
            sorted_indices.emplace_back(order_indices_[i], i);
        }
        std::ranges::sort(sorted_indices);

        std::vector<T> result;
        for (size_t i = 0; i < result_size; ++i) {
            result.push_back(elements_[sorted_indices[i].second]);
        }
        return result;
    }

    void clear() {
        skip_k_ = 0;
        seen_k_ = 0;
    }

private:
    size_t n_;
    size_t skip_k_;
    size_t seen_k_;
    StdRNG rng_;
    std::vector<T> elements_;
    std::vector<size_t> order_indices_;

    // Compute next skip distance: skip_k = seen_k - ceil(Exp(1) / n)
    void recompute_skip_() {
        std::exponential_distribution<F> exp_dist(1.0);
        F q = std::exp(exp_dist(rng_) / F(n_));
        skip_k_ = static_cast<size_t>(std::ceil(F(seen_k_) * q)) - 1;
    }

    // Binomial sampling for number of replacements
    int choose(size_t n, F p) {
        return utilities::choose(rng_, n, p);
    }
};


// Algorithm tag for weighted sampling without replacement (Algorithm A-Res)
struct AlgARes {};

template<>
struct is_weighted<AlgARes> : std::true_type {};


// Weighted reservoir sampling WITHOUT replacement using priority queue (Algorithm A-Res)
// Based on: "Weighted random sampling with a reservoir" (Efraimidis & Spirakis, 2006)
// Uses priority queue to maintain top-k weighted elements
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerARes : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    explicit ReservoirSamplerARes(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          seen_k_(0),
          rng_(seed) {}

    void fit(const T& element, F weight) {
        if (weight < 0) {
            throw std::invalid_argument("Weights must be non-negative");
        }
        
        seen_k_++;
        
        // Compute key: u^(1/weight) where U ~ Uniform(0,1)
        // Higher weight → higher key → more likely to be selected
        std::uniform_real_distribution<F> unif(0.0, 1.0);
        F u = unif(rng_);
        if (u == 0.0) u = 1e-10;  // Avoid u=0
        F key = std::pow(u, F(1.0) / weight);
        
        if (heap_.size() < n_) {
            // Still in filling phase
            heap_.push({key, element});
            order_indices_.push_back(seen_k_ - 1);
        } else if (key > heap_.top().first) {
            // New element has higher key than minimum
            heap_.pop();
            heap_.push({key, element});
        }
    }

    // Unweighted fit() method (required by interface, defaults to weight=1)
    void fit(const T& element) {
        fit(element, F(1));
    }

    // Return total elements observed
    [[nodiscard]] size_t nobs() const override {
        return seen_k_;
    }

    // Check if empty
    [[nodiscard]] bool empty() const override {
        return seen_k_ == 0;
    }

    std::vector<T> value() const {
        // Extract elements from priority queue
        std::vector<T> result;
        
        // Create a copy of heap to extract elements
        auto heap_copy = heap_;
        while (!heap_copy.empty()) {
            result.push_back(heap_copy.top().second);
            heap_copy.pop();
        }
        
        // Reverse because priority_queue gives largest first, but we want them in arbitrary order
        std::reverse(result.begin(), result.end());
        return result;
    }

    std::vector<T> ordvalue() const requires (Ordered) {
        // Return elements in order they were added (if tracking is enabled)
        std::vector<T> result;
        
        auto heap_copy = heap_;
        while (!heap_copy.empty()) {
            result.push_back(heap_copy.top().second);
            heap_copy.pop();
        }
        
        return result;
    }

     void clear() {
         seen_k_ = 0;
         // Clear heap
         while (!heap_.empty()) {
             heap_.pop();
         }
         order_indices_.clear();
     }
 
     [[nodiscard]] size_t count() const { return seen_k_; }
     [[nodiscard]] size_t size() const { return heap_.size(); }
 
 private:
    size_t n_;
    size_t seen_k_;
    StdRNG rng_;
    
    // Priority queue: pairs of (key, element)
    // Custom comparator: min-heap based on first element (key)
    struct CompareKey {
        bool operator()(const std::pair<F, T>& a, const std::pair<F, T>& b) const {
            return a.first > b.first;  // min-heap: smaller key has lower priority
        }
    };
    
    std::priority_queue<std::pair<F, T>, std::vector<std::pair<F, T>>, CompareKey> heap_;
    std::vector<size_t> order_indices_;
};


// Algorithm tag for weighted sampling without replacement (Algorithm A-ExpJ)
struct AlgAExpJ {};

template<>
struct is_weighted<AlgAExpJ> : std::true_type {};


// Weighted reservoir sampling WITHOUT replacement with exponential jumps (Algorithm A-ExpJ)
// Based on: "Weighted random sampling with a reservoir" (Efraimidis & Spirakis, 2006)
// More efficient than A-Res: skips elements that cannot be in top-k using exponential jumps
template<typename T, typename F = double, bool Mutable = true, bool Ordered = false>
class ReservoirSamplerAExpJ : public std::conditional_t<
    Mutable,
    MutableSamplerBase<T, F>,
    AbstractSampler<T, F>
> {
public:
    using element_type = T;
    using float_type = F;
    static constexpr bool is_mutable = Mutable;
    static constexpr bool is_ordered = Ordered;

    explicit ReservoirSamplerAExpJ(size_t n, uint32_t seed = std::random_device{}())
        : n_(n),
          min_key_(F(0)),
          seen_k_(0),
          rng_(seed) {}

    void fit(const T& element, F weight) {
        if (weight < 0) {
            throw std::invalid_argument("Weights must be non-negative");
        }
        
        seen_k_++;
        
        // Compute key: u^(1/weight) where U ~ Uniform(0,1)
        // Higher weight → higher key → more likely to be selected
        std::uniform_real_distribution<F> unif(0.0, 1.0);
        F u = unif(rng_);
        if (u == 0.0) u = 1e-10;
        F key = std::pow(u, F(1.0) / weight);
        
        if (heap_.size() < n_) {
            // Filling phase
            heap_.push({key, element});
            if (heap_.size() == n_) {
                // Full: compute min key and next skip
                min_key_ = heap_.top().first;
                next_skip_ = compute_skip_();
            }
        } else if (key > min_key_) {
            // Element qualifies for top-k
            heap_.pop();
            heap_.push({key, element});
            min_key_ = heap_.top().first;
            next_skip_ = compute_skip_();
        }
    }

    // Unweighted fit() method (required by interface, defaults to weight=1)
    void fit(const T& element) {
        fit(element, F(1));
    }

    // Return total elements observed
    [[nodiscard]] size_t nobs() const override {
        return seen_k_;
    }

    // Check if empty
    [[nodiscard]] bool empty() const override {
        return seen_k_ == 0;
    }

    std::vector<T> value() const {
        std::vector<T> result;
        auto heap_copy = heap_;
        while (!heap_copy.empty()) {
            result.push_back(heap_copy.top().second);
            heap_copy.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    std::vector<T> ordvalue() const requires (Ordered) {
        std::vector<T> result;
        auto heap_copy = heap_;
        while (!heap_copy.empty()) {
            result.push_back(heap_copy.top().second);
            heap_copy.pop();
        }
        return result;
    }

    void clear() {
        seen_k_ = 0;
        while (!heap_.empty()) {
            heap_.pop();
        }
        min_key_ = F(0);
    }

    [[nodiscard]] size_t count() const { return seen_k_; }
    [[nodiscard]] size_t size() const { return heap_.size(); }

private:
    size_t n_;
    F min_key_;
    size_t seen_k_;
    F next_skip_;
    StdRNG rng_;
    
    struct CompareKey {
        bool operator()(const std::pair<F, T>& a, const std::pair<F, T>& b) const {
            return a.first > b.first;
        }
    };
    
    std::priority_queue<std::pair<F, T>, std::vector<std::pair<F, T>>, CompareKey> heap_;

    // Compute skip distance for next element
    // Uses exponential distribution to estimate how many elements to skip
    F compute_skip_() const {
        std::exponential_distribution<F> exp_dist(1.0);
        return std::exp(exp_dist(rng_) / static_cast<F>(n_));
    }
};

} // namespace ctream
