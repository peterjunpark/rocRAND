// Copyright (c) 2017-2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef ROCRAND_RNG_DISTRIBUTION_POISSON_H_
#define ROCRAND_RNG_DISTRIBUTION_POISSON_H_

#include "discrete.hpp"

#include <rocrand/rocrand.h>
#include <rocrand/rocrand_uniform.h>

#include <algorithm>
#include <climits>
#include <vector>

namespace rocrand_impl::host
{

template<discrete_method Method = DISCRETE_METHOD_ALIAS, bool IsHostSide = false>
class poisson_distribution : public discrete_distribution_base<Method, IsHostSide>
{
public:
    typedef discrete_distribution_base<Method, IsHostSide> base;

    poisson_distribution() : base() {}

    explicit poisson_distribution(double lambda) : poisson_distribution()
    {
        set_lambda(lambda);
    }

    void set_lambda(double lambda)
    {
        const size_t capacity =
            2 * static_cast<size_t>(16.0 * (2.0 + std::sqrt(lambda)));
        std::vector<double> p(capacity);

        calculate_probabilities(p, capacity, lambda);

        this->init(p, this->size, this->offset);
    }

protected:

    void calculate_probabilities(std::vector<double>& p, const size_t capacity,
                                 const double lambda)
    {
        const double p_epsilon = 1e-12;
        const double log_lambda = std::log(lambda);

        const int left = static_cast<int>(std::floor(lambda)) - capacity / 2;

        // Calculate probabilities starting from mean in both directions,
        // because only a small part of [0, lambda] has non-negligible values
        // (> p_epsilon).

        int lo = 0;
        for (int i = capacity / 2; i >= 0; i--)
        {
            const double x = left + i;
            const double pp = std::exp(x * log_lambda - std::lgamma(x + 1.0) - lambda);
            if (pp < p_epsilon)
            {
                lo = i + 1;
                break;
            }
            p[i] = pp;
        }

        int hi = capacity - 1;
        for (int i = capacity / 2 + 1; i < static_cast<int>(capacity); i++)
        {
            const double x = left + i;
            const double pp = std::exp(x * log_lambda - std::lgamma(x + 1.0) - lambda);
            if (pp < p_epsilon)
            {
                hi = i - 1;
                break;
            }
            p[i] = pp;
        }

        for (int i = lo; i <= hi; i++)
        {
            p[i - lo] = p[i];
        }

        this->size = hi - lo + 1;
        this->offset = left + lo;
    }
};

// Handles caching of precomputed tables for the distribution and recomputes
// them only when lambda is changed (as these computations, device memory
// allocations and copying take time).
template<discrete_method Method = DISCRETE_METHOD_ALIAS, bool IsHostSide = false>
class poisson_distribution_manager
{
public:
    poisson_distribution<Method, IsHostSide> dis;

    poisson_distribution_manager() = default;

    poisson_distribution_manager(const poisson_distribution_manager&) = delete;

    poisson_distribution_manager(poisson_distribution_manager&& other)
        : dis(other.dis), lambda(other.lambda)
    {
        // For now, we didn't make poisson_distribution move-only
        // We copied the pointers of dis. Prevent deallocation by the destructor of other
        other.dis = {};
    }

    poisson_distribution_manager& operator=(const poisson_distribution_manager&) = delete;

    poisson_distribution_manager& operator=(poisson_distribution_manager&& other)
    {
        dis    = other.dis;
        lambda = other.lambda;

        // For now, we didn't make poisson_distribution move-only
        // We copied the pointers of dis. Prevent deallocation by the destructor of other
        other.dis = {};

        return *this;
    }

    ~poisson_distribution_manager()
    {
        dis.deallocate();
    }

    void set_lambda(double new_lambda)
    {
        const bool changed = lambda != new_lambda;
        if (changed)
        {
            lambda = new_lambda;
            dis.set_lambda(lambda);
        }
    }

private:
    double lambda = 0.0;
};

// Mrg32k3a and Mrg31k3p

template<typename state_type, bool IsHostSide = false>
struct mrg_engine_poisson_distribution
{
    using distribution_type = poisson_distribution<DISCRETE_METHOD_ALIAS, IsHostSide>;
    static constexpr unsigned int input_width = 1;
    static constexpr unsigned int output_width = 1;

    distribution_type dis;

    mrg_engine_poisson_distribution(distribution_type dis) : dis(dis) {}

    __host__ __device__
    void operator()(const unsigned int (&input)[1], unsigned int (&output)[1]) const
    {
        // Alias method requires x in [0, 1), uint must be in [0, UINT_MAX],
        // but MRG-based engine's "raw" output is in [1, MRG_M1],
        // so probabilities are slightly different than expected,
        // some values can not be generated at all.
        // Hence the "raw" value is remapped to [0, UINT_MAX]:
        unsigned int v
            = rocrand_device::detail::mrg_uniform_distribution_uint<state_type>(input[0]);
        output[0] = dis(v);
    }
};

// Mrg32ka (compatibility API)

struct mrg_poisson_distribution : mrg_engine_poisson_distribution<rocrand_state_mrg32k3a>
{
    explicit mrg_poisson_distribution(poisson_distribution<DISCRETE_METHOD_ALIAS> dis)
        : mrg_engine_poisson_distribution(dis)
    {}
};

} // namespace rocrand_impl::host

#endif // ROCRAND_RNG_DISTRIBUTION_POISSON_H_
