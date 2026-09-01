/// @file aio/test/temp_dir.hpp
/// @brief A temporary directory that removes itself.
/// @details Tests that shell out to openssl(1) need somewhere to put the keys and certificates it
///          writes. Fixed paths under /tmp make two tests collide, leave files behind after the run,
///          and - where one test writes what another reads - couple tests together through the
///          filesystem in a way nothing in either file mentions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <filesystem>
    #include <string>
    #include <system_error>

    #include <cstdlib>
#endif

namespace kmx::aio::test
{
    /// @brief A uniquely-named directory under the system temp dir, removed on destruction.
    class scoped_temp_dir
    {
    public:
        /// @brief Creates the directory.
        /// @param prefix Included in the name, so a leftover from a crashed run says where it came from.
        explicit scoped_temp_dir(const std::string& prefix = "kmx_aio_test") noexcept(false)
        {
            auto pattern = (std::filesystem::temp_directory_path() / (prefix + "_XXXXXX")).string();
            if (const char* const created = ::mkdtemp(pattern.data()); created != nullptr)
                path_ = created;
        }

        scoped_temp_dir(const scoped_temp_dir&) = delete;
        scoped_temp_dir& operator=(const scoped_temp_dir&) = delete;

        /// @brief Removes the directory and everything in it.
        /// @details Errors are swallowed: a test that already failed should report that failure, not be
        ///          replaced by an exception thrown while cleaning up after it.
        ~scoped_temp_dir() noexcept
        {
            if (path_.empty())
                return;

            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        /// @brief Whether the directory was created.
        /// @return True when ::mkdtemp succeeded.
        [[nodiscard]] bool valid() const noexcept { return !path_.empty(); }

        /// @brief The directory itself.
        /// @return Its path, empty when creation failed.
        [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

        /// @brief Builds a path to a named entry inside the directory.
        /// @param name The entry's file name.
        /// @return The full path.
        [[nodiscard]] std::filesystem::path operator/(const std::string& name) const noexcept(false) { return path_ / name; }

    private:
        std::filesystem::path path_;
    };

} // namespace kmx::aio::test
