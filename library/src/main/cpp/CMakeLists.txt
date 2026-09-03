cmake_minimum_required(VERSION 3.22.1)
project("cvulkan")

# --- Shader ---
set(SHADER_SRC_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
set(SHADER_ASSET_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/shaders)
file(MAKE_DIRECTORY ${SHADER_ASSET_DIR})
find_program(GLSLC_COMPILER glslc)
set(SHADERS
        ${SHADER_SRC_DIR}/triangle.vert
        ${SHADER_SRC_DIR}/triangle.frag
)
set(SPIRV_SHADERS "")
foreach (SHADER ${SHADERS})
    get_filename_component(FILE_NAME_WE
            ${SHADER}
            NAME_WE)
    get_filename_component(FILE_EXT
            ${SHADER}
            EXT)
    string(SUBSTRING
            ${FILE_EXT}
            1
            -1
            EXT_NO_DOT)
    set(SPIRV_OUT
            ${SHADER_ASSET_DIR}/${FILE_NAME_WE}_${EXT_NO_DOT}.spv)
    add_custom_command(
            OUTPUT ${SPIRV_OUT}
            COMMAND
            ${GLSLC_COMPILER}
            ${SHADER}
            -o
            ${SPIRV_OUT}
            -O
            DEPENDS ${SHADER}
            COMMENT
            "Compiling ${SHADER} -> ${SPIRV_OUT}"
            VERBATIM
    )
    list(APPEND SPIRV_SHADERS
            ${SPIRV_OUT})
endforeach ()
add_custom_target(
        CompileShaders
        ALL
        DEPENDS ${SPIRV_SHADERS}
)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)
find_library(mediandk-lib mediandk)
find_library(nativewindow-lib nativewindow)

# --- FFmpeg ---
set(FFMPEG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)
include_directories(${FFMPEG_ROOT}/include/${ANDROID_ABI})

add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/${ANDROID_ABI}/libavutil.so)

add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/${ANDROID_ABI}/libavcodec.so)

add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/${ANDROID_ABI}/libavformat.so)

add_library(swresample SHARED IMPORTED)
set_target_properties(swresample PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/${ANDROID_ABI}/libswresample.so)

add_library(
        cvulkan
        SHARED
        cvulkan_jni.cpp
        CVulkanEngine.cpp
        CVulkanStreamManager.cpp)

add_dependencies(cvulkan CompileShaders)

target_link_libraries(
        cvulkan
        ${log-lib}
        ${android-lib}
        ${vulkan-lib}
        ${mediandk-lib}
        ${nativewindow-lib})