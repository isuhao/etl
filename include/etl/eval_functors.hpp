//=======================================================================
// Copyright (c) 2014-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file eval_functors.hpp
 * \brief Contains the functors used by the evaluator to perform its
 * actions.
 */

#pragma once

#include "cpp_utils/parallel.hpp"

#include "etl/visitor.hpp"        //visitor of the expressions
#include "etl/threshold.hpp"      //parallel thresholds
#include "etl/eval_selectors.hpp" //method selectors

namespace etl {

namespace detail {

/*!
 * \brief Functor for simple assign
 *
 * The result is written to lhs with operator[] and read from rhs
 * with read_flat
 */
template<typename V_T, typename V_Expr>
struct Assign {
    mutable V_T* lhs;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    Assign(V_T* lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i]     = rhs.read_flat(i);
                lhs[i + 1] = rhs.read_flat(i + 1);
                lhs[i + 2] = rhs.read_flat(i + 2);
                lhs[i + 3] = rhs.read_flat(i + 3);
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] = rhs.read_flat(i);
        }
    }
};

/*!
 * \brief Common base for vectorized functors
 */
template<typename L_Expr, typename V_Expr, typename Base>
struct vectorized_base {
    using derived_t = Base;
    using memory_type = value_t<L_Expr>*;

    L_Expr& lhs;
    memory_type lhs_m;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    using IT = intrinsic_traits<value_t<V_Expr>>;

    vectorized_base(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), lhs_m(lhs.memory_start()), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        //1. Peel loop (if necessary)
        auto peeled = as_derived().peel_loop();

        //2. Main vectorized loop

        std::size_t first = peeled;

        if (_size - peeled >= IT::size) {
            if (reinterpret_cast<uintptr_t>(lhs_m + _first + peeled) % IT::alignment == 0) {
                first = as_derived().aligned_main_loop(_first + peeled);
            } else {
                first = as_derived().unaligned_main_loop(_first + peeled);
            }
        } else {
            first += _first;
        }

        //3. Remainder loop (non-vectorized)

        as_derived().remainder_loop(first);
    }

private:
    /*!
     * \brief Returns a reference to the derived object, i.e. the object using the CRTP injector.
     * \return a reference to the derived object.
     */
    const derived_t& as_derived() const noexcept {
        return *static_cast<const derived_t*>(this);
    }
};

/*!
 * \brief Functor for vectorized assign
 *
 * The result is computed in a vectorized fashion with several
 * operations per cycle and written directly to the memory of lhs.
 */
template<typename L_Expr, typename V_Expr>
struct VectorizedAssign : vectorized_base<L_Expr, V_Expr, VectorizedAssign<L_Expr, V_Expr>> {
    using base_t = vectorized_base<L_Expr, V_Expr, VectorizedAssign<L_Expr, V_Expr>>;
    using IT = typename base_t::IT;

    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    VectorizedAssign(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last) : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::operator();

    /*!
     * \brief Peel the loop to perform aligned store when possible
     * \return the number of peeled iterations
     */
    std::size_t peel_loop() const {
        std::size_t i = 0;

        constexpr const auto size_1 = sizeof(value_t<V_Expr>);
        auto u_bytes                = (reinterpret_cast<uintptr_t>(lhs_m + _first) % IT::alignment);

        if (u_bytes >= size_1 && u_bytes % size_1 == 0) {
            auto u_loads = std::min(u_bytes / size_1, _size);

            for (; i < u_loads; ++i) {
                lhs_m[_first + i] = rhs[_first + i];
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using aligned store operations
     * \param first The index when to start
     */
    inline std::size_t aligned_main_loop(std::size_t first) const {
        std::size_t i = 0;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::store(lhs_m + i, rhs.load(i));
                default_vec::store(lhs_m + i + 1 * IT::size, rhs.load(i + 1 * IT::size));
                default_vec::store(lhs_m + i + 2 * IT::size, rhs.load(i + 2 * IT::size));
                default_vec::store(lhs_m + i + 3 * IT::size, rhs.load(i + 3 * IT::size));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::store(lhs_m + i, rhs.load(i));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using unaligned store operations
     * \param first The index when to start
     */
    inline std::size_t unaligned_main_loop(std::size_t first) const {
        std::size_t i;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::storeu(lhs_m + i, rhs.load(i));
                default_vec::storeu(lhs_m + i + 1 * IT::size, rhs.load(i + 1 * IT::size));
                default_vec::storeu(lhs_m + i + 2 * IT::size, rhs.load(i + 2 * IT::size));
                default_vec::storeu(lhs_m + i + 3 * IT::size, rhs.load(i + 3 * IT::size));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::storeu(lhs_m + i, rhs.load(i));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the last iterations of the the loop that have
     * not been vectorized
     * \param first The index when to start
     */
    void remainder_loop(std::size_t first) const {
        for (std::size_t i = first; i < _last; ++i) {
            lhs_m[i] = rhs[i];
        }
    }
};

/*!
 * \brief Functor for simple compound assign add
 */
template<typename V_T, typename V_Expr>
struct AssignAdd {
    mutable V_T* lhs;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    AssignAdd(V_T* lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i]     += rhs[i];
                lhs[i + 1] += rhs[i + 1];
                lhs[i + 2] += rhs[i + 2];
                lhs[i + 3] += rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] += rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign add
 */
template<typename L_Expr, typename V_Expr>
struct VectorizedAssignAdd : vectorized_base<L_Expr, V_Expr, VectorizedAssignAdd<L_Expr, V_Expr>> {
    using base_t = vectorized_base<L_Expr, V_Expr, VectorizedAssignAdd<L_Expr, V_Expr>>;
    using IT = typename base_t::IT;

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    VectorizedAssignAdd(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last) : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::operator();

    /*!
     * \brief Peel the loop to perform aligned store when possible
     * \return the number of peeled iterations
     */
    std::size_t peel_loop() const {
        std::size_t i = 0;

        constexpr const auto size_1 = sizeof(value_t<V_Expr>);
        auto u_bytes                = (reinterpret_cast<uintptr_t>(lhs_m + _first) % IT::alignment);

        if (u_bytes >= size_1 && u_bytes % size_1 == 0) {
            auto u_loads = std::min(u_bytes / size_1, _size);

            for (; i < u_loads; ++i) {
                lhs_m[_first + i] += rhs[_first + i];
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using aligned store operations
     * \param first The index when to start
     */
    inline std::size_t aligned_main_loop(std::size_t first) const {
        std::size_t i = 0;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::store(lhs_m + i,                default_vec::add(lhs.load(i), rhs.load(i)));
                default_vec::store(lhs_m + i + 1 * IT::size, default_vec::add(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::store(lhs_m + i + 2 * IT::size, default_vec::add(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::store(lhs_m + i + 3 * IT::size, default_vec::add(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::store(lhs_m + i, default_vec::add(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using unaligned store operations
     * \param first The index when to start
     */
    inline std::size_t unaligned_main_loop(std::size_t first) const {
        std::size_t i;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::storeu(lhs_m + i,                default_vec::add(lhs.load(i), rhs.load(i)));
                default_vec::storeu(lhs_m + i + 1 * IT::size, default_vec::add(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::storeu(lhs_m + i + 2 * IT::size, default_vec::add(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::storeu(lhs_m + i + 3 * IT::size, default_vec::add(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::storeu(lhs_m + i, default_vec::add(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the last iterations of the the loop that have
     * not been vectorized
     * \param first The index when to start
     */
    void remainder_loop(std::size_t first) const {
        for (std::size_t i = first; i < _last; ++i) {
            lhs_m[i] += rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign sub
 */
template<typename V_T, typename V_Expr>
struct AssignSub {
    mutable V_T* lhs;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    AssignSub(V_T* lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i]     -= rhs[i];
                lhs[i + 1] -= rhs[i + 1];
                lhs[i + 2] -= rhs[i + 2];
                lhs[i + 3] -= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] -= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign sub
 */
template<typename L_Expr, typename V_Expr>
struct VectorizedAssignSub : vectorized_base<L_Expr, V_Expr, VectorizedAssignSub<L_Expr, V_Expr>> {
    using base_t = vectorized_base<L_Expr, V_Expr, VectorizedAssignSub<L_Expr, V_Expr>>;
    using IT = typename base_t::IT;

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    VectorizedAssignSub(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last) : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::operator();

    /*!
     * \brief Peel the loop to perform aligned store when possible
     * \return the number of peeled iterations
     */
    std::size_t peel_loop() const {
        std::size_t i = 0;

        constexpr const auto size_1 = sizeof(value_t<V_Expr>);
        auto u_bytes                = (reinterpret_cast<uintptr_t>(lhs_m + _first) % IT::alignment);

        if (u_bytes >= size_1 && u_bytes % size_1 == 0) {
            auto u_loads = std::min(u_bytes / size_1, _size);

            for (; i < u_loads; ++i) {
                lhs_m[_first + i] -= rhs[_first + i];
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using aligned store operations
     * \param first The index when to start
     */
    inline std::size_t aligned_main_loop(std::size_t first) const {
        std::size_t i = 0;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::store(lhs_m + i,                default_vec::sub(lhs.load(i), rhs.load(i)));
                default_vec::store(lhs_m + i + 1 * IT::size, default_vec::sub(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::store(lhs_m + i + 2 * IT::size, default_vec::sub(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::store(lhs_m + i + 3 * IT::size, default_vec::sub(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::store(lhs_m + i, default_vec::sub(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using unaligned store operations
     * \param first The index when to start
     */
    inline std::size_t unaligned_main_loop(std::size_t first) const {
        std::size_t i;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::storeu(lhs_m + i,                default_vec::sub(lhs.load(i), rhs.load(i)));
                default_vec::storeu(lhs_m + i + 1 * IT::size, default_vec::sub(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::storeu(lhs_m + i + 2 * IT::size, default_vec::sub(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::storeu(lhs_m + i + 3 * IT::size, default_vec::sub(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::storeu(lhs_m + i, default_vec::sub(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the last iterations of the the loop that have
     * not been vectorized
     * \param first The index when to start
     */
    void remainder_loop(std::size_t first) const {
        for (std::size_t i = first; i < _last; ++i) {
            lhs_m[i] -= rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign mul
 */
template<typename V_T, typename V_Expr>
struct AssignMul {
    mutable V_T* lhs;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    AssignMul(V_T* lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i]     *= rhs[i];
                lhs[i + 1] *= rhs[i + 1];
                lhs[i + 2] *= rhs[i + 2];
                lhs[i + 3] *= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] *= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign mul
 */
template<typename L_Expr, typename V_Expr>
struct VectorizedAssignMul : vectorized_base<L_Expr, V_Expr, VectorizedAssignMul<L_Expr, V_Expr>> {
    using base_t = vectorized_base<L_Expr, V_Expr, VectorizedAssignMul<L_Expr, V_Expr>>;
    using IT = typename base_t::IT;

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    VectorizedAssignMul(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last) : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::operator();

    /*!
     * \brief Peel the loop to perform aligned store when possible
     * \return the number of peeled iterations
     */
    std::size_t peel_loop() const {
        std::size_t i = 0;

        constexpr const auto size_1 = sizeof(value_t<V_Expr>);
        auto u_bytes                = (reinterpret_cast<uintptr_t>(lhs_m + _first) % IT::alignment);

        if (u_bytes >= size_1 && u_bytes % size_1 == 0) {
            auto u_loads = std::min(u_bytes / size_1, _size);

            for (; i < u_loads; ++i) {
                lhs_m[_first + i] *= rhs[_first + i];
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using aligned store operations
     * \param first The index when to start
     */
    inline std::size_t aligned_main_loop(std::size_t first) const {
        std::size_t i = 0;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::store(lhs_m + i,                default_vec::mul(lhs.load(i), rhs.load(i)));
                default_vec::store(lhs_m + i + 1 * IT::size, default_vec::mul(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::store(lhs_m + i + 2 * IT::size, default_vec::mul(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::store(lhs_m + i + 3 * IT::size, default_vec::mul(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::store(lhs_m + i, default_vec::mul(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using unaligned store operations
     * \param first The index when to start
     */
    inline std::size_t unaligned_main_loop(std::size_t first) const {
        std::size_t i;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::storeu(lhs_m + i,                default_vec::mul(lhs.load(i), rhs.load(i)));
                default_vec::storeu(lhs_m + i + 1 * IT::size, default_vec::mul(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::storeu(lhs_m + i + 2 * IT::size, default_vec::mul(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::storeu(lhs_m + i + 3 * IT::size, default_vec::mul(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::storeu(lhs_m + i, default_vec::mul(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the last iterations of the the loop that have
     * not been vectorized
     * \param first The index when to start
     */
    void remainder_loop(std::size_t first) const {
        for (std::size_t i = first; i < _last; ++i) {
            lhs_m[i] *= rhs[i];
        }
    }
};

/*!
 * \brief Functor for compound assign div
 */
template<typename V_T, typename V_Expr>
struct AssignDiv {
    mutable V_T* lhs;
    V_Expr& rhs;
    const std::size_t _first;
    const std::size_t _last;
    const std::size_t _size;

    AssignDiv(V_T* lhs, V_Expr& rhs, std::size_t first, std::size_t last)
            : lhs(lhs), rhs(rhs), _first(first), _last(last), _size(last - first) {
        //Nothing else
    }

    /*!
     * \brief Assign rhs to lhs
     */
    void operator()() const {
        std::size_t iend = _first;

        if (unroll_normal_loops) {
            iend = _first + (_size & std::size_t(-4));

            for (std::size_t i = _first; i < iend; i += 4) {
                lhs[i]     /= rhs[i];
                lhs[i + 1] /= rhs[i + 1];
                lhs[i + 2] /= rhs[i + 2];
                lhs[i + 3] /= rhs[i + 3];
            }
        }

        for (std::size_t i = iend; i < _last; ++i) {
            lhs[i] /= rhs[i];
        }
    }
};

/*!
 * \brief Functor for vectorized compound assign div
 */
template<typename L_Expr, typename V_Expr>
struct VectorizedAssignDiv : vectorized_base<L_Expr, V_Expr, VectorizedAssignDiv<L_Expr, V_Expr>> {
    using base_t = vectorized_base<L_Expr, V_Expr, VectorizedAssignDiv<L_Expr, V_Expr>>;
    using IT = typename base_t::IT;

    using base_t::lhs;
    using base_t::lhs_m;
    using base_t::rhs;
    using base_t::_first;
    using base_t::_size;
    using base_t::_last;

    VectorizedAssignDiv(L_Expr& lhs, V_Expr& rhs, std::size_t first, std::size_t last) : base_t(lhs, rhs, first, last) {
        //Nothing else
    }

    using base_t::operator();

    /*!
     * \brief Peel the loop to perform aligned store when possible
     * \return the number of peeled iterations
     */
    std::size_t peel_loop() const {
        std::size_t i = 0;

        constexpr const auto size_1 = sizeof(value_t<V_Expr>);
        auto u_bytes                = (reinterpret_cast<uintptr_t>(lhs_m + _first) % IT::alignment);

        if (u_bytes >= size_1 && u_bytes % size_1 == 0) {
            auto u_loads = std::min(u_bytes / size_1, _size);

            for (; i < u_loads; ++i) {
                lhs_m[_first + i] /= rhs[_first + i];
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using aligned store operations
     * \param first The index when to start
     */
    inline std::size_t aligned_main_loop(std::size_t first) const {
        std::size_t i = 0;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::store(lhs_m + i,                default_vec::div(lhs.load(i), rhs.load(i)));
                default_vec::store(lhs_m + i + 1 * IT::size, default_vec::div(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::store(lhs_m + i + 2 * IT::size, default_vec::div(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::store(lhs_m + i + 3 * IT::size, default_vec::div(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::store(lhs_m + i, default_vec::div(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the vectorized iterations of the the loop using unaligned store operations
     * \param first The index when to start
     */
    inline std::size_t unaligned_main_loop(std::size_t first) const {
        std::size_t i;

        if(unroll_vectorized_loops && _last - first > IT::size * 4){
            for(i = first; i + IT::size * 4 - 1 < _last; i += IT::size * 4){
                default_vec::storeu(lhs_m + i,                default_vec::div(lhs.load(i), rhs.load(i)));
                default_vec::storeu(lhs_m + i + 1 * IT::size, default_vec::div(lhs.load(i + 1 * IT::size), rhs.load(i + 1 * IT::size)));
                default_vec::storeu(lhs_m + i + 2 * IT::size, default_vec::div(lhs.load(i + 2 * IT::size), rhs.load(i + 2 * IT::size)));
                default_vec::storeu(lhs_m + i + 3 * IT::size, default_vec::div(lhs.load(i + 3 * IT::size), rhs.load(i + 3 * IT::size)));
            }
        } else {
            for(i = first; i + IT::size - 1 < _last; i += IT::size){
                default_vec::storeu(lhs_m + i, default_vec::div(lhs.load(i), rhs.load(i)));
            }
        }

        return i;
    }

    /*!
     * \brief Compute the last iterations of the the loop that have
     * not been vectorized
     * \param first The index when to start
     */
    void remainder_loop(std::size_t first) const {
        for (std::size_t i = first; i < _last; ++i) {
            lhs_m[i] /= rhs[i];
        }
    }
};

} //end of namespace detail

} //end of namespace etl
