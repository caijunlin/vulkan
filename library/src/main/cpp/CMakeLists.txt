cmake_minimum_required(VERSION 3.22.1)
project("ragnarok_engine")

# =========================================================
# Shader 自动编译配置
# =========================================================

# 查找系统环境中的 glslc 编译器
find_program(GLSLC_COMPILER glslc)
if(NOT GLSLC_COMPILER)
    message(FATAL_ERROR "glslc not found! Please check your Vulkan SDK installation.")
endif()

# 根据当前 CMakeLists.txt 的路径 (main/cpp/) 设置相对路径
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../assets/shaders)

file(MAKE_DIRECTORY ${SHADER_DIR})

# 指定需要编译的源着色器文件
set(SHADERS
        ${SHADER_DIR}/triangle.vert
        ${SHADER_DIR}/triangle.frag
)

set(SPIRV_SHADERS "")

# 遍历并生成自定义编译指令
foreach(SHADER ${SHADERS})
    # 获取文件名（如: triangle）和扩展名（如: .vert）
    get_filename_component(FILE_NAME_WE ${SHADER} NAME_WE)
    get_filename_component(FILE_EXT ${SHADER} EXT)

    # 截取扩展名去掉点 (将 ".vert" 变成 "vert")
    string(SUBSTRING ${FILE_EXT} 1 -1 EXT_NO_DOT)

    # 拼接最终的输出路径 (如: main/assets/shaders/triangle_vert.spv)
    set(SPIRV_OUT ${SHADER_DIR}/${FILE_NAME_WE}_${EXT_NO_DOT}.spv)

    add_custom_command(
            OUTPUT ${SPIRV_OUT}
            COMMAND ${GLSLC_COMPILER} ${SHADER} -o ${SPIRV_OUT} -O
            DEPENDS ${SHADER}
            COMMENT "Compiling ${FILE_NAME_WE}${FILE_EXT} to SPIR-V..."
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUT})
endforeach()

# 创建全局 Target，绑定所有的 .spv 输出
add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_SHADERS})

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)
find_library(mediandk-lib mediandk)
find_library(nativewindow-lib nativewindow)

add_library(
        ragnarok
        SHARED
        ragnarok_bridge.cpp
        VulkanEngine.cpp
        RagnarokStreamManager.cpp)

# 强制在编译 ragnarok 库之前，先执行 CompileShaders 生成 .spv 文件
add_dependencies(ragnarok CompileShaders)

target_link_libraries(
        ragnarok
        ${log-lib}
        ${android-lib}
        ${vulkan-lib}
        ${mediandk-lib}
        ${nativewindow-lib})