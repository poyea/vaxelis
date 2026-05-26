include(CheckIPOSupported)

function(vaxelis_apply_target_options target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

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
