# GPU / CUDA Completion Model

Namespace: `kmx::aio::gpu`

This feature provides a lightweight executor model for awaiting CUDA event completion in coroutine code.

## Components

- `gpu::executor`
- `gpu::stream`
- `gpu::event`

Feature-gated through `project.enable_cuda`. Requires the NVIDIA driver and CUDA runtime/toolkit. Degrades gracefully when `enable_cuda:false`.

## C++ Key Methods

Minimal API flow used by the GPU image-processing sample:

```cpp
auto io_exec = std::make_shared<kmx::aio::completion::executor>();

kmx::aio::gpu::executor_config gpu_cfg {
    .max_events = 64u,
    .thread_count = 1u,
    .core_id = -1,
    .gpu_device = cfg.gpu_device,
};
auto gpu_exec = std::make_shared<kmx::aio::gpu::executor>(gpu_cfg);

auto cap = kmx::aio::completion::v4l2::capture::create(io_exec, cap_cfg);
auto frame = co_await cap->next_frame();

kmx::aio::gpu::stream stream;
auto event = stream.create_event();
co_await event;

gpu_exec->spawn(gpu_process_frame(std::move(host_frame)));
const auto& stats = gpu_exec->get_statistics();
```

## Full C++ Sample Project

The complete buildable sample is already available in the repository:

- [source/sample/gpu/gpu.qbs](source/sample/gpu/gpu.qbs)
- [source/sample/gpu/image_processing/sample-gpu-image-processing.qbs](source/sample/gpu/image_processing/sample-gpu-image-processing.qbs)
- [source/sample/gpu/image_processing/src/main.cpp](source/sample/gpu/image_processing/src/main.cpp)
- [source/sample/gpu/image_processing/inc/kmx/aio/sample/gpu/image_processing/manager.hpp](source/sample/gpu/image_processing/inc/kmx/aio/sample/gpu/image_processing/manager.hpp)
- [source/sample/gpu/image_processing/src/kmx/aio/sample/gpu/image_processing/manager.cpp](source/sample/gpu/image_processing/src/kmx/aio/sample/gpu/image_processing/manager.cpp)

## Build and Run

Build and run the GPU image-processing sample:

```bash
qbs build --products sample-gpu-image-processing -f source/source.qbs config:debug \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false \
    project.enable_cuda:true

GPU_BIN="$(find debug -type f -name sample-gpu-image-processing | head -n 1)"
"$GPU_BIN" --max-frames 1 --width 320 --height 240 --buffer-count 2 --gpu-device 0
```

Force NVIDIA on PRIME on-demand systems:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 \
__GLX_VENDOR_LIBRARY_NAME=nvidia \
__VK_LAYER_NV_optimus=NVIDIA_only \
LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} \
    bash scripts/ci/run-ci-avb-local.sh --only gpu-smoke
```

Troubleshooting:

- `nvidia-smi` missing or no GPUs listed: NVIDIA runtime/driver unavailable.
- `cuda_runtime.h` missing: CUDA toolkit headers are not installed.
- `GLIBCXX_3.4.35 not found`: run with `LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}`.
