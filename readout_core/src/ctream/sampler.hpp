// Core sampler interfaces for streaming reservoir sampling
// Provides abstract base classes for mutable and immutable sampling variants

#pragma once

#include <vector>
#include <optional>
#include <concepts>
#include <cstddef>
#include "rng.hpp"


namespace ctream {

// Forward declarations
struct MutableSampler {};
struct ImmutableSampler {};

// Ordered vs unordered sample tracking
struct Ordered {};
struct Unordered {};

// Type for distinguishing single vs multi-element samplers
// Template parameter N is sample size, or 1 for single-element sampling
template<size_t N>
struct SampleSize {};

// Trait to extract element type from a sampler
template<typename Sampler>
struct element_type_impl;

template<typename T>
struct element_type_impl<std::vector<T>> {
    using type = T;
};


// Abstract base class for all sampler types
// T: element type being sampled
// F: floating-point type for weights and calculations (default: double)
template<typename T, typename F = double>
class AbstractSampler {
public:
    using element_type = T;
    using float_type = F;
    
    virtual ~AbstractSampler() = default;
    
    // Return total count of elements observed during sampling
    virtual size_t nobs() const = 0;
    
    // Check if sampler has any samples collected
    virtual bool empty() const = 0;
};


// Mutable sampler interface: in-place updates
// fit!(sampler, element) modifies the sampler state
template<typename T, typename F = double>
class MutableSamplerBase : public AbstractSampler<T, F> {
public:
    // Fit with unweighted element (weight = 1)
    virtual void fit(const T& element) = 0;
    
    // Fit with weighted element
    virtual void fit(const T& element, F weight) = 0;
};


// Immutable sampler interface: returns new sampler with updated state
// fit_immutable(sampler, element) -> new sampler
template<typename DerivedSampler>
class ImmutableSamplerBase : public AbstractSampler<typename DerivedSampler::element_type, typename DerivedSampler::float_type> {
public:
    using T = typename DerivedSampler::element_type;
    using F = typename DerivedSampler::float_type;
    
    // Fit with unweighted element, returning new sampler
    virtual DerivedSampler fit_immutable(const T& element) const = 0;
    
    // Fit with weighted element, returning new sampler
    virtual DerivedSampler fit_weighted_immutable(const T& element, F weight) const = 0;
};


// Trait to check if a sampler supports ordered output
// Ordered samplers can return elements in collection order via ordvalue()
template<typename T>
struct is_ordered : std::false_type {};


// Trait to check if a sampler supports weighted input
// Weighted samplers require both fit(el) and fit(el, weight) signatures
template<typename T>
struct is_weighted : std::false_type {};


} // namespace ctream
