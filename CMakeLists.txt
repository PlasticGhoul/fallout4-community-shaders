cmake_minimum_required(VERSION 4.2)

project(CommunityShadersFO4 VERSION 0.1.0 LANGUAGES CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "in-source builds are not allowed")
endif()

option(FO4CS_BUILD_TESTS "Build the host tests" ON)
set(FO4CS_DEPLOY_DIR
    ""
    CACHE PATH
    "If set, the built plugin is copied to <dir>/F4SE/Plugins after linking"
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------- git identity
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

# ------------------------------------------------------------------ commonlibf4
# Upstream ships xmake.lua only, so a CMake shim is generated in the build tree.
set(COMMONLIBF4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/CommonLibF4")
if(NOT EXISTS "${COMMONLIBF4_SOURCE_DIR}/xmake.lua")
    message(
        FATAL_ERROR
        "extern/CommonLibF4 is empty. Run: git submodule update --init --recursive"
    )
endif()

set(COMMONLIBF4_SHIM_DIR "${CMAKE_BINARY_DIR}/CommonLibF4-shim")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/CommonLibF4.cmake.in"
    "${COMMONLIBF4_SHIM_DIR}/CMakeLists.txt"
    @ONLY
)
add_subdirectory("${COMMONLIBF4_SHIM_DIR}" CommonLibF4 EXCLUDE_FROM_ALL)

# --------------------------------------------------------------- plugin target
add_library(${PROJECT_NAME} SHARED)

file(
    GLOB_RECURSE PLUGIN_SOURCES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)
target_sources(${PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/Plugin.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
    @ONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/Version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
    @ONLY
)
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
)

# The Plugin.h above is stamped at configure time and goes stale on the next
# commit, so a plain rebuild would ship a binary that misreports which commit
# produced it. Re-derive the identity before every build instead. The script
# uses configure_file, which leaves the header alone when nothing changed, so
# this costs one git call and never forces a recompile.
add_custom_target(
    ${PROJECT_NAME}_git_version
    COMMAND
        "${CMAKE_COMMAND}" -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}" -D
        "IN_FILE=${CMAKE_SOURCE_DIR}/cmake/Plugin.h.in" -D
        "OUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h" -D
        "PROJECT_NAME=${PROJECT_NAME}" -D
        "PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}" -D
        "PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}" -D
        "PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}" -P
        "${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
    COMMENT "Refreshing the build identity"
    VERBATIM
)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_git_version)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibF4::CommonLibF4)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
    )
    # /MP is an MSBuild-only knob; Ninja parallelises on its own and warns.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_options(${PROJECT_NAME} PRIVATE /MP)
    endif()
endif()

if(FO4CS_DEPLOY_DIR)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${FO4CS_DEPLOY_DIR}/F4SE/Plugins"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>" "${FO4CS_DEPLOY_DIR}/F4SE/Plugins/"
        COMMENT "Deploying to ${FO4CS_DEPLOY_DIR}/F4SE/Plugins"
        VERBATIM
    )
endif()

# ------------------------------------------------------------------ host tests
if(FO4CS_BUILD_TESTS)
    enable_testing()

    add_executable(
        RuntimeTests
        "${CMAKE_SOURCE_DIR}/tests/RuntimeTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Runtime.cpp"
    )

    target_include_directories(
        RuntimeTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(RuntimeTests PRIVATE cxx_std_23)
    target_precompile_headers(
        RuntimeTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(RuntimeTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            RuntimeTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME Runtime COMMAND RuntimeTests)

    add_executable(
        VTablePatchTests
        "${CMAKE_SOURCE_DIR}/tests/VTablePatchTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/VTablePatch.cpp"
    )

    target_include_directories(
        VTablePatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(VTablePatchTests PRIVATE cxx_std_23)
    target_precompile_headers(
        VTablePatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(VTablePatchTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            VTablePatchTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME VTablePatch COMMAND VTablePatchTests)

    add_executable(
        FormatNamesTests
        "${CMAKE_SOURCE_DIR}/tests/FormatNamesTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/FormatNames.cpp"
    )

    target_include_directories(
        FormatNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(FormatNamesTests PRIVATE cxx_std_23)
    target_precompile_headers(
        FormatNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(FormatNamesTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            FormatNamesTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME FormatNames COMMAND FormatNamesTests)
endif()
