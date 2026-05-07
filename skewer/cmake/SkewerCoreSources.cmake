set(_SKEWER_CORE_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set(SKEWER_CORE_SOURCES
    "${_SKEWER_CORE_SOURCE_ROOT}/src/session/render_session.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/scene/scene.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/scene/interp_curve.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/scene/animation.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/film/film.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/film/image_buffer.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/integrators/path_trace.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/integrators/normals.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/accelerators/bvh.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/accelerators/tlas.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/scene/light.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/io/obj_loader.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/io/graph_from_json.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/io/scene_loader.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/io/image_io.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/materials/bsdf.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/materials/texture.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/core/spectral/rgb2spec.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/kernels/path_kernel.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/kernels/sample_media.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/kernels/volume_dispatch.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/kernels/utils/visibility.cc"
    "${_SKEWER_CORE_SOURCE_ROOT}/src/kernels/utils/volume_tracking.cc"
)

unset(_SKEWER_CORE_SOURCE_ROOT)
