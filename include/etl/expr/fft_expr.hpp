//=======================================================================
// Copyright (c) 2014-2015 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file fft_expr.hpp
 * \brief Contains the FFT expressions.
*/

#pragma once

#include "cpp_utils/assert.hpp"
#include "cpp_utils/tmp.hpp"

//Get the implementations
#include "etl/impl/fft.hpp"

namespace etl {

template <typename T, std::size_t D, template <typename...> class Impl>
struct basic_fft_expr {
    using this_type  = basic_fft_expr<T, D, Impl>;
    using value_type = T;

private:
    template <typename A, class Enable = void>
    struct result_type_builder {
        using type = dyn_matrix<value_type, D>;
    };

    template <typename A, typename I>
    struct fast_result_type_builder;

    template <typename A, std::size_t... I>
    struct fast_result_type_builder<A, std::index_sequence<I...>> {
        using type = fast_dyn_matrix<value_type, this_type::template dim<A, I>()...>;
    };

    template <typename A>
    struct result_type_builder<A, std::enable_if_t<decay_traits<A>::is_fast>> {
        using type = typename fast_result_type_builder<A, std::make_index_sequence<D>>::type;
    };

    template <typename A, std::size_t... I>
    static result_type<A>* dyn_allocate(A&& a, std::index_sequence<I...> /*seq*/) {
        return new result_type<A>(etl::template dim<I>(a)...);
    }

public:

    /*!
     * \brief The result type for a given sub expression type
     * \tparam A The sub epxpression type
     */
    template <typename A>
    using result_type = typename result_type_builder<A>::type;

    /*!
     * \brief Returns the DDth dimension of the expression
     * \param a The sub expression
     * \tparam DD The dimension to get
     * \return the DDth dimension of the expression
     */
    template <typename A, std::size_t DD>
    static constexpr std::size_t dim() {
        return decay_traits<A>::template dim<DD>();
    }

    /*!
     * \brief Allocate the temporary for the expression
     * \param a The sub expression
     * \return a pointer to the temporary
     */
    template <typename A, cpp_enable_if(decay_traits<A>::is_fast)>
    static result_type<A>* allocate(A&& a) {
        cpp_unused(a);
        return new result_type<A>();
    }

    /*!
     * \brief Allocate the temporary for the expression
     * \param a The sub expression
     * \return a pointer to the temporary
     */
    template <typename A, cpp_disable_if(decay_traits<A>::is_fast)>
    static result_type<A>* allocate(A&& a) {
        return dyn_allocate(std::forward<A>(a), std::make_index_sequence<D>());
    }

    /*!
     * \brief Apply the expression
     * \param a The sub expression
     * \param c The expression where to store the results
     */
    template <typename A, typename C>
    static void apply(A&& a, C&& c) {
        static_assert(is_etl_expr<A>::value && is_etl_expr<C>::value, "Fast-Fourrier Transform only supported for ETL expressions");

        Impl<decltype(make_temporary(std::forward<A>(a))), C>::apply(
            make_temporary(std::forward<A>(a)),
            std::forward<C>(c));
    }

    /*!
     * \brief Returns a textual representation of the operation
     * \return a textual representation of the operation
     */
    static std::string desc() noexcept {
        return "fft";
    }

    /*!
     * \brief Returns the dth dimension of the expression
     * \param a The sub expression
     * \param d The dimension to get
     * \return the dth dimension of the expression
     */
    template <typename A>
    static std::size_t dim(const A& a, std::size_t d) {
        return etl_traits<A>::dim(a, d);
    }

    /*!
     * \brief Returns the size of the expression
     * \param a The sub expression
     * \return the size of the expression
     */
    template <typename A>
    static std::size_t size(const A& a) {
        return etl::size(a);
    }

    /*!
     * \brief Returns the size of the expression
     * \return the size of the expression
     */
    template <typename A>
    static constexpr std::size_t size() {
        return etl::decay_traits<A>::size();
    }

    /*!
     * \brief Returns the number of dimensions of the expression
     * \return the number of dimensions of the expression
     */
    static constexpr std::size_t dimensions() {
        return D;
    }
};

//1D FFT/IFFT

template <typename T>
using fft1_expr = basic_fft_expr<T, 1, detail::fft1_impl>;

template <typename T>
using ifft1_expr = basic_fft_expr<T, 1, detail::ifft1_impl>;

template <typename T>
using ifft1_real_expr = basic_fft_expr<T, 1, detail::ifft1_real_impl>;

//2D FFT/IFFT

template <typename T>
using fft2_expr = basic_fft_expr<T, 2, detail::fft2_impl>;

template <typename T>
using ifft2_expr = basic_fft_expr<T, 2, detail::ifft2_impl>;

template <typename T>
using ifft2_real_expr = basic_fft_expr<T, 2, detail::ifft2_real_impl>;

//Many 1D FFT/IFFT

template <typename T>
using fft1_many_expr = basic_fft_expr<T, 2, detail::fft1_many_impl>;

//Many 2D FFT/IFFT

template <typename T>
using fft2_many_expr = basic_fft_expr<T, 3, detail::fft2_many_impl>;

} //end of namespace etl
