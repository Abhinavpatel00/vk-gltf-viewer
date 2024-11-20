function(target_link_shaders TARGET)
    if (${Vulkan_glslc_FOUND})
        message(STATUS "Using Vulkan glslc for shader compilation.")
    elseif (${Vulkan_glslangValidator_FOUND})
        message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
    else()
        message(FATAL_ERROR "No shader compiler found.")
    endif ()

    foreach (source IN LISTS ARGN)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make C-style identifier from filename.
        string(MAKE_C_IDENTIFIER ${filename} identifier)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source)

        # Create shader interface file.
        set(output_interface "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm")
        file(WRITE "${output_interface}" "export module vk_gltf_viewer:vulkan.shader.${identifier}; import std; namespace vk_gltf_viewer::vulkan::shader { export std::span<const std::uint32_t> ${identifier}() noexcept; }")
        list(APPEND output_interfaces "${output_interface}")

        # Create shader implementation file.
        set(output_impl "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp")
        file(WRITE "${output_impl}" "module vk_gltf_viewer; import :vulkan.shader.${identifier}; import std; std::uint32_t ${identifier}_source[] = { \n#include \"${identifier}.spv\"\n }; std::span<const std::uint32_t> vk_gltf_viewer::vulkan::shader::${identifier}() noexcept { return ${identifier}_source; }")
        list(APPEND output_impls "${output_impl}")

        # Compile shader.
        set(output_header "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.spv")
        if (${Vulkan_glslc_FOUND})
            set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.d")
            add_custom_command(
                OUTPUT "${output_header}"
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF "${depfile}" $<$<NOT:$<CONFIG:Debug>>:-O> -mfmt=num --target-env=vulkan1.2 "${source}" -o "${output_header}"
                DEPENDS "${source}"
                BYPRODUCTS "${depfile}"
                DEPFILE "${depfile}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT "${output_header}"
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V -x --target-env vulkan1.2 "${source}" -o "${output_header}"
                DEPENDS "${source}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        endif ()
        list(APPEND output_headers "${output_header}")
    endforeach ()

    # Link files to the target.
    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${output_interfaces})
    target_sources(${TARGET} PRIVATE FILE_SET HEADERS FILES ${output_headers})
    target_sources(${TARGET} PRIVATE ${output_impls})
endfunction()