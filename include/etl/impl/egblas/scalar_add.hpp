//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file
 * \brief EGBLAS wrappers for the axdy operation.
 */

#pragma once

#ifdef ETL_EGBLAS_MODE

#include "etl/impl/cublas/cuda.hpp"

#include <egblas.hpp>

#endif

namespace etl {

namespace impl {

namespace egblas {

#ifdef EGBLAS_HAS_SCALAR_SADD

static constexpr bool has_scalar_sadd = true;

/*!
 * \brief Adds the scalar beta to each element of the single-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(float* x, size_t n, size_t s, float* beta){
    egblas_scalar_sadd(x, n, s, *beta);
}

#else

static constexpr bool has_scalar_sadd = false;

#endif

#ifdef EGBLAS_HAS_SCALAR_DADD

static constexpr bool has_scalar_dadd = true;

/*!
 * \brief Adds the scalar beta to each element of the double-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(double* x, size_t n, size_t s, double* beta){
    egblas_scalar_dadd(x, n, s, *beta);
}

#else

static constexpr bool has_scalar_sadd = false;

#endif

#ifdef EGBLAS_HAS_SCALAR_CADD

static constexpr bool has_scalar_cadd = true;

/*!
 * \brief Adds the scalar beta to each element of the complex single-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(etl::complex<float>* x, size_t n, size_t s, etl::complex<float>* beta){
    egblas_scalar_cadd(reinterpret_cast<cuComplex*>(x), n, s, *reinterpret_cast<cuComplex*>(beta));
}

/*!
 * \brief Adds the scalar beta to each element of the complex single-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(std::complex<float>* x, size_t n, size_t s, std::complex<float>* beta){
    egblas_scalar_cadd(reinterpret_cast<cuComplex*>(x), n, s, *reinterpret_cast<cuComplex*>(beta));
}

#else

static constexpr bool has_scalar_cadd = false;

#endif

#ifdef EGBLAS_HAS_SCALAR_ZADD

static constexpr bool has_scalar_zadd = true;

/*!
 * \brief Adds the scalar beta to each element of the complex single-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(etl::complex<double>* x, size_t n, size_t s, etl::complex<double>* beta){
    egblas_scalar_zadd(reinterpret_cast<cuDoubleComplex*>(x), n, s, *reinterpret_cast<cuDoubleComplex*>(beta));
}

/*!
 * \brief Adds the scalar beta to each element of the complex single-precision vector x
 * \param x The vector to add the scalar to (GPU pointer)
 * \param n The size of the vector
 * \param s The stride of the vector
 * \param beta The scalar to add
 */
inline void scalar_add(std::complex<double>* x, size_t n, size_t s, std::complex<double>* beta){
    egblas_scalar_zadd(reinterpret_cast<cuDoubleComplex*>(x), n, s, *reinterpret_cast<cuDoubleComplex*>(beta));
}

#else

static constexpr bool has_scalar_zadd = false;

#endif

} //end of namespace egblas
} //end of namespace impl
} //end of namespace etl