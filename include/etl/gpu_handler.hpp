//=======================================================================
// Copyright (c) 2014-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#ifdef ETL_CUDA
#include "etl/impl/cublas/cuda.hpp"
#endif

namespace etl {

#ifdef ETL_CUDA
template<typename T>
struct gpu_memory_handler {
private:
    T* gpu_memory_;

    mutable bool cpu_up_to_date = true;
    mutable bool gpu_up_to_date = false;

public:
    /*!
     * \brief Return GPU memory of this expression, if any.
     * \return a pointer to the GPU memory or nullptr if not allocated in GPU.
     */
    T* gpu_memory() const noexcept {
        return gpu_memory_;
    }

    /*!
     * \brief Evict the expression from GPU.
     */
    void gpu_evict() const noexcept {
        if (gpu_memory_) {
            //Note: the const_cast is only here to allow compilation
            cuda_check(cudaFree((reinterpret_cast<void*>(const_cast<std::remove_const_t<T>*>(gpu_memory_)))));
        }

        invalidate_gpu();
    }

    /*!
     * \brief Invalidates the CPU memory
     */
    void invalidate_cpu() const noexcept {
        cpu_up_to_date = false;
    }

    /*!
     * \brief Invalidates the GPU memory
     */
    void invalidate_gpu() const noexcept {
        gpu_up_to_date = false;
    }

    /*!
     * \brief Ensures that the GPU memory is allocated and that the GPU memory
     * is up to date (to undefined value).
     * \param etl_size The size of the memory
     */
    void ensure_gpu_allocated(size_t etl_size) const {
        if (!is_gpu_allocated()) {
            gpu_allocate_impl(etl_size);
        }

        gpu_up_to_date = true;
    }

    /*!
     * \brief Allocate memory on the GPU for the expression and copy the values into the GPU.
     * \param cpu_memory Pointer to CPU memory
     * \param etl_size The size of the memory
     */
    void ensure_gpu_up_to_date(T* cpu_memory, size_t etl_size) const {
        // Make sure there is some memory allocate
        if (!is_gpu_allocated()) {
            gpu_allocate_impl(etl_size);
        }

        if(!gpu_up_to_date){
            cpu_to_gpu(cpu_memory, etl_size);
        }
    }

    /*!
     * \brief Copy back from the GPU to the expression memory if
     * necessary.
     * \param cpu_memory Pointer to CPU memory
     * \param etl_size The size of the memory
     */
    void ensure_cpu_up_to_date(T* cpu_memory, size_t etl_size) const {
        if(!cpu_up_to_date){
            gpu_to_cpu(cpu_memory, etl_size);
        }
    }

    /*!
     * \brief Transfer the GPU memory to another handler
     * \param rhs The handler to transfer memory to
     */
    void gpu_transfer_to(gpu_memory_handler& rhs){
        rhs.gpu_memory_ = gpu_memory_;
        gpu_memory_ = nullptr;

        // The memory was transferred, not up to date anymore
        gpu_up_to_date = false;

        // The target is up to date on GPU but CPU is not to date anymore
        rhs.gpu_up_to_date = true;
        rhs.cpu_up_to_date = false;
    }

private:

    /*!
     * \brief Allocate memory on the GPU for the expression
     */
    void gpu_allocate_impl(size_t etl_size) const {
        cpp_assert(!is_gpu_allocated(), "Trying to allocate already allocated GPU gpu_memory_");

        gpu_memory_ = impl::cuda::cuda_allocate_only<T>(etl_size);

        inc_counter("gpu:allocate");
    }

    /*!
     * \brief Copy back from the CPU to the GPU
     */
    void cpu_to_gpu(T* cpu_memory, size_t etl_size) const {
        cpp_assert(is_gpu_allocated(), "Cannot copy to unallocated GPU memory");
        cpp_assert(!gpu_up_to_date, "Copy must only be done if necessary");
        cpp_assert(cpu_up_to_date, "Copy from invalid memory!");

        cuda_check(cudaMemcpy(
            const_cast<std::remove_const_t<T>*>(gpu_memory_),
            const_cast<std::remove_const_t<T>*>(cpu_memory),
            etl_size * sizeof(T), cudaMemcpyHostToDevice));

        gpu_up_to_date = true;

        inc_counter("gpu:cpu_to_gpu");
    }

    /*!
     * \brief Copy back from the GPU to the expression memory.
     */
    void gpu_to_cpu(T* cpu_memory, size_t etl_size) const {
        cpp_assert(is_gpu_allocated(), "Cannot copy from unallocated GPU memory()");
        cpp_assert(gpu_up_to_date, "Cannot copy from invalid memory");
        cpp_assert(!cpu_up_to_date, "Copy done without reason");

        cpp_assert(!std::is_const<std::remove_pointer_t<decltype(gpu_memory_)>>::value, "copy_from should not be used on const memory");
        cpp_assert(!std::is_const<T>::value, "copy_from should not be used on const memory");

        cuda_check(cudaMemcpy(
            const_cast<std::remove_const_t<T>*>(cpu_memory),
            const_cast<std::remove_const_t<T>*>(gpu_memory_),
            etl_size * sizeof(T), cudaMemcpyDeviceToHost));

        cpu_up_to_date = true;

        inc_counter("gpu:gpu_to_cpu");
    }

    /*!
     * \brief Indicates if the expression is allocated in GPU.
     * \return true if the expression is allocated in GPU, false otherwise
     */
    bool is_gpu_allocated() const noexcept {
        return gpu_memory_;
    }
};

#else
template<typename T>
struct gpu_memory_handler {
    /*!
     * \brief Return GPU memory of this expression, if any.
     * \return a pointer to the GPU memory or nullptr if not allocated in GPU.
     */
    T* gpu_memory() const noexcept {
        return nullptr;
    }

    /*!
     * \brief Evict the expression from GPU.
     */
    void gpu_evict() const noexcept {}

    /*!
     * \brief Invalidates the CPU memory
     */
    void invalidate_cpu() const noexcept {}

    /*!
     * \brief Invalidates the GPU memory
     */
    void invalidate_gpu() const noexcept {}

    /*!
     * \brief Ensures that the GPU memory is allocated and that the GPU memory
     * is up to date (to undefined value).
     * \param etl_size The size of the memory
     */
    void ensure_gpu_allocated(size_t etl_size) const {
        cpp_unused(etl_size);
    }

    /*!
     * \brief Allocate memory on the GPU for the expression and copy the values into the GPU.
     * \param cpu_memory Pointer to CPU memory
     * \param etl_size The size of the memory
     */
    void ensure_gpu_up_to_date(T* cpu_memory, size_t etl_size) const {
        cpp_unused(cpu_memory);
        cpp_unused(etl_size);
    }

    /*!
     * \brief Copy back from the GPU to the expression memory if
     * necessary.
     * \param cpu_memory Pointer to CPU memory
     * \param etl_size The size of the memory
     */
    void ensure_cpu_up_to_date(T* cpu_memory, size_t etl_size) const {
        cpp_unused(cpu_memory);
        cpp_unused(etl_size);
    }

    /*!
     * \brief Transfer the GPU memory to another handler
     * \param rhs The handler to transfer memory to
     */
    void gpu_transfer_to(gpu_memory_handler& rhs){
        cpp_unused(rhs);
    }
};
#endif

} //end of namespace etl
