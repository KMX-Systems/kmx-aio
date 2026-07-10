/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/error.hpp>

#include <string>

namespace kmx::aio::someip
{
    namespace detail
    {
        class someip_error_category final: public std::error_category
        {
        public:
            const char* name() const noexcept override { return "someip"; }

            std::string message(const int ev) const override
            {
                switch (static_cast<error>(ev))
                {
                    case error::success:
                        return "success";
                    case error::feature_disabled:
                        return "SOME/IP feature is disabled";
                    case error::not_initialized:
                        return "SOME/IP object is not initialized";
                    case error::invalid_configuration:
                        return "SOME/IP configuration is invalid";
                    case error::start_failed:
                        return "SOME/IP runtime start failed";
                    case error::stopped:
                        return "SOME/IP runtime is stopped";
                    case error::service_not_found:
                        return "SOME/IP service not found";
                    case error::service_unavailable:
                        return "SOME/IP service unavailable";
                    case error::request_failed:
                        return "SOME/IP request failed";
                    case error::response_failed:
                        return "SOME/IP response failed";
                    case error::subscription_closed:
                        return "SOME/IP subscription is closed";
                    case error::timed_out:
                        return "SOME/IP operation timed out";
                    case error::internal_error:
                        return "SOME/IP internal error";
                }

                return "unknown SOME/IP error";
            }
        };

        const someip_error_category someip_error_category_instance {};
    } // namespace detail

    const std::error_category& error_category() noexcept
    {
        return detail::someip_error_category_instance;
    }

    std::error_code make_error_code(const error code) noexcept
    {
        return {static_cast<int>(code), error_category()};
    }

} // namespace kmx::aio::someip
