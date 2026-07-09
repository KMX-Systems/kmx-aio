#include <kmx/aio/sample/gpu/image_processing/manager.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/v4l2/capture.hpp>
#include <kmx/aio/gpu/executor.hpp>
#include <kmx/aio/task.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#if defined(KMX_AIO_FEATURE_CUDA)
    #include <cuda_runtime.h>
#endif

namespace kmx::aio::sample::gpu::image_processing
{
    namespace detail
    {
        kmx::aio::task<void> gpu_process_frame(std::vector<std::uint8_t> host_frame)        {
            kmx::aio::gpu::stream gpu_stream;

#if defined(KMX_AIO_FEATURE_CUDA)
            std::uint8_t* device_frame = nullptr;
            const auto device_size = host_frame.size();

            if (::cudaMalloc(reinterpret_cast<void**>(&device_frame), device_size) != cudaSuccess)
            {
                std::cerr << "[GPU Image Processing] cudaMalloc failed\n";
                co_return;
            }

            const auto stream_handle = static_cast<cudaStream_t>(gpu_stream.handle());
            const bool submit_ok =
                ::cudaMemcpyAsync(device_frame, host_frame.data(), device_size, cudaMemcpyHostToDevice, stream_handle) == cudaSuccess &&
                ::cudaMemsetAsync(device_frame, 0x11u, device_size, stream_handle) == cudaSuccess &&
                ::cudaMemcpyAsync(host_frame.data(), device_frame, device_size, cudaMemcpyDeviceToHost, stream_handle) == cudaSuccess;

            if (!submit_ok)
            {
                ::cudaFree(device_frame);
                std::cerr << "[GPU Image Processing] CUDA pipeline submission failed\n";
                co_return;
            }
#endif

            // Record event after all async submissions — fires when GPU finishes all enqueued work.
            auto gpu_done_event = gpu_stream.create_event();
            co_await gpu_done_event;

#if defined(KMX_AIO_FEATURE_CUDA)
            // Device memory is safe to release now: event has fired, stream is done.
            ::cudaFree(device_frame);
#endif

            const std::uint64_t checksum = std::accumulate(host_frame.begin(), host_frame.end(), std::uint64_t {0u});
            std::cout << "[GPU Image Processing] frame_bytes=" << host_frame.size() << " checksum=" << checksum << "\n";

            co_return;
        }

        kmx::aio::task<void> capture_and_process(std::shared_ptr<kmx::aio::completion::executor> io_exec,
                             std::shared_ptr<kmx::aio::gpu::executor> gpu_exec,
                             const config& cfg) noexcept(false)
        {
            kmx::aio::completion::v4l2::capture_config cap_cfg {
                .device = cfg.device,
                .format = cfg.format,
                .size = cfg.size,
                .fps = cfg.fps,
                .buffer_count = cfg.buffer_count,
            };

            auto cap_res = kmx::aio::completion::v4l2::capture::create(io_exec, std::move(cap_cfg));
            if (!cap_res)
            {
                std::cerr << "[GPU Image Processing] V4L2 unavailable, using synthetic frame fallback\n";
                std::vector<std::uint8_t> synthetic(cfg.size.width * cfg.size.height, 0x2Au);
                gpu_exec->spawn(gpu_process_frame(std::move(synthetic)));
                io_exec->stop();
                co_return;
            }

            auto cap = std::move(*cap_res);

            for (std::uint64_t i = 0; i < cfg.max_frames; ++i)
            {
                auto frame_res = co_await cap.next_frame();
                if (!frame_res)
                    break;

                const auto frame_bytes = frame_res->data();
                std::vector<std::uint8_t> host_frame(frame_bytes.size());
                std::memcpy(host_frame.data(), frame_bytes.data(), frame_bytes.size());
                gpu_exec->spawn(gpu_process_frame(std::move(host_frame)));
            }

            io_exec->stop();
            co_return;
        }
    }

    bool manager::run() noexcept
    {
        try
        {
            auto io_exec = std::make_shared<kmx::aio::completion::executor>();
            const kmx::aio::gpu::executor_config config {
                .max_events = 64u,
                .thread_count = 1u,
                .core_id = -1,
                .gpu_device = config_.gpu_device,
            };

            auto gpu_exec = std::make_shared<kmx::aio::gpu::executor>(config);

            io_exec->spawn(detail::capture_and_process(io_exec, gpu_exec, config_));
            io_exec->run();

            const auto& stats = gpu_exec->get_statistics();
            std::cout << "[GPU Image Processing] tasks_spawned=" << stats.total_tasks_spawned.load() << "\n";
            return stats.total_tasks_spawned.load() > 0u;
        }
        catch (const std::exception& e)
        {
            std::cerr << "[GPU Image Processing] error: " << e.what() << "\n";
            return false;
        }
    }
}
