// Random number generation abstraction using C++ standard library
// Provides a unified interface over std::mt19937 for reproducible sampling

#pragma once

#include <random>
#include <concepts>
#include <cstdint>


namespace ctream {

// Concept for random number generators compatible with std::random_device style distributions
template<typename T>
concept RandomNumberGenerator = requires(T& rng) {
    { rng() } -> std::convertible_to<uint32_t>;
    typename T::result_type;
};

// Default RNG type: Mersenne Twister 19937
// Why MT19937: fast, widely used, good statistical properties for reservoir sampling
using DefaultRNG = std::mt19937;

// Wrapper for seeded RNG with explicit seed control
// Enables reproducible sampling in tests
class StdRNG {
public:
    using result_type = uint32_t;
    
    explicit StdRNG(uint32_t seed = std::random_device{}()) 
        : engine_(seed) {}
    
    // Interface compatible with std::random_device style
    result_type operator()() const {
        return const_cast<std::mt19937&>(engine_)();
    }
    
    static constexpr result_type min() { return std::mt19937::min(); }
    static constexpr result_type max() { return std::mt19937::max(); }
    
    // Seed the RNG for reproducibility
    void seed(uint32_t s) {
        engine_.seed(s);
    }

private:
    std::mt19937 engine_;
};

} // namespace ctream
