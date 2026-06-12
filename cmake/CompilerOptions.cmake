include(CheckIPOSupported)

function(vaxelis_apply_target_options target_name)
    # cxx_std_26 isn't in MSVC's known-feature list in the CMake CI uses, so on
    # MSVC the standard is requested through the CXX_STANDARD property (mapped to
    # /std:c++latest in the top-level CMakeLists). The portable meta-feature is
    # used elsewhere so the requirement propagates to consumers of the engine.
    if(MSVC)
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD ${CMAKE_CXX_STANDARD}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)
    else()
        target_compile_features(${target_name} PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Werror)
    endif()

    set_target_properties(
        ${target_name}
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON)

    if(VAXELIS_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>)
        target_link_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>)
    endif()

    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
endfunction()
