import qbs

Project {
    condition: project.enable_cuda
    references: [
        "image_processing/sample-gpu-image-processing.qbs",
    ]
}
