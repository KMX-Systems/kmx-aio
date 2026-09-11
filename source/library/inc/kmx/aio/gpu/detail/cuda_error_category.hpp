/// @file inc/kmx/aio/gpu/detail/cuda_error_category.hpp
/// @brief The std::error_category class that names CUDA runtime error codes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_CUDA)
    #ifndef PCH
        #include <string>
        #include <system_error>
    #endif

namespace kmx::aio::gpu::detail
{
    /// @brief The `std::error_category` that names CUDA runtime error codes.
    class cuda_error_category: public std::error_category
    {
    public:
        /// @brief Returns the category name, "cuda".
        [[nodiscard]] const char* name() const noexcept override { return "cuda"; }

        /// @brief Returns the message text of a CUDA runtime error code.
        /// @param ev The error value, a `cudaError_t`.
        /// @return Its text, or "CUDA error code" followed by the number for a code without a text of its own.
        [[nodiscard]] std::string message(int ev) const override;
    };
}
#endif
