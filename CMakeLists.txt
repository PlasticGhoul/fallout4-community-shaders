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

# The overlay's backends. Only the plugin links these; the host tests keep the
# state machine away from ImGui on purpose.
find_package(imgui CONFIG REQUIRED)

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
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE CommonLibF4::CommonLibF4 imgui::imgui
)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
    )

    # Ship a pdb. Without one, an Addictol crash log names raw addresses
    # instead of our functions, which is the difference between a diagnosable
    # report and a useless one. /DEBUG on its own turns the linker's
    # optimisations back off, so they are asked for explicitly.
    target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
    target_link_options(${PROJECT_NAME} PRIVATE /DEBUG /OPT:REF /OPT:ICF)

    # /MP is an MSBuild-only knob; Ninja parallelises on its own and warns.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_options(${PROJECT_NAME} PRIVATE /MP)
    endif()
endif()

if(FO4CS_DEPLOY_DIR)
    # The same script the package target uses, so the installation that gets
    # played is the tree that gets shipped. Two copy commands here would be a
    # second rule, and rules in two places drift.
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/package.ps1"
            -SourceRoot "${CMAKE_SOURCE_DIR}" -PluginFile
            "$<TARGET_FILE:${PROJECT_NAME}>" -PdbFile
            "$<TARGET_PDB_FILE:${PROJECT_NAME}>" -Version "${PROJECT_VERSION}"
            -Stage "${FO4CS_DEPLOY_DIR}"
        COMMENT "Staging into ${FO4CS_DEPLOY_DIR}"
        VERBATIM
    )
endif()

# Deliberately outside ALL: the iteration build is the fast way into the game
# and has no business writing three archives every time.
add_custom_target(
    package
    COMMAND
        pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/package.ps1"
        -SourceRoot "${CMAKE_SOURCE_DIR}" -PluginFile
        "$<TARGET_FILE:${PROJECT_NAME}>" -PdbFile
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>" -Version "${PROJECT_VERSION}"
        -CMake "${CMAKE_COMMAND}" -WorkDir "${CMAKE_CURRENT_BINARY_DIR}/package"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Packing dist/"
    VERBATIM
)

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

    add_executable(
        ShaderSourceTests
        "${CMAKE_SOURCE_DIR}/tests/ShaderSourceTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Shader/ShaderSource.cpp"
    )

    target_include_directories(
        ShaderSourceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ShaderSourceTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ShaderSourceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ShaderSourceTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ShaderSourceTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ShaderSource COMMAND ShaderSourceTests)

    add_executable(
        ShaderCompilerTests
        "${CMAKE_SOURCE_DIR}/tests/ShaderCompilerTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Shader/ShaderCompiler.cpp"
    )

    target_include_directories(
        ShaderCompilerTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ShaderCompilerTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ShaderCompilerTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ShaderCompilerTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ShaderCompilerTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ShaderCompiler COMMAND ShaderCompilerTests)

    add_executable(
        FileWatchTests
        "${CMAKE_SOURCE_DIR}/tests/FileWatchTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Util/FileWatch.cpp"
    )

    target_include_directories(
        FileWatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(FileWatchTests PRIVATE cxx_std_23)
    target_precompile_headers(
        FileWatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(FileWatchTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            FileWatchTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME FileWatch COMMAND FileWatchTests)

    add_executable(
        FeatureRegistryTests
        "${CMAKE_SOURCE_DIR}/tests/FeatureRegistryTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Feature/FeatureRegistry.cpp"
    )

    target_include_directories(
        FeatureRegistryTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(FeatureRegistryTests PRIVATE cxx_std_23)
    target_precompile_headers(
        FeatureRegistryTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(FeatureRegistryTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            FeatureRegistryTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME FeatureRegistry COMMAND FeatureRegistryTests)

    add_executable(
        SafeReadTests
        "${CMAKE_SOURCE_DIR}/tests/SafeReadTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Util/SafeRead.cpp"
    )

    target_include_directories(
        SafeReadTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(SafeReadTests PRIVATE cxx_std_23)
    target_precompile_headers(
        SafeReadTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(SafeReadTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            SafeReadTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME SafeRead COMMAND SafeReadTests)

    add_executable(
        MenuGateTests
        "${CMAKE_SOURCE_DIR}/tests/MenuGateTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Menu/MenuGate.cpp"
    )

    target_include_directories(
        MenuGateTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(MenuGateTests PRIVATE cxx_std_23)
    target_precompile_headers(
        MenuGateTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(MenuGateTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            MenuGateTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME MenuGate COMMAND MenuGateTests)

    # Schema only, deliberately: the whole point of the split is that the
    # description of a setting needs neither REX nor a file to be exercised.
    add_executable(
        SettingsSchemaTests
        "${CMAKE_SOURCE_DIR}/tests/SettingsSchemaTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Schema.cpp"
    )

    target_include_directories(
        SettingsSchemaTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(SettingsSchemaTests PRIVATE cxx_std_23)
    target_precompile_headers(
        SettingsSchemaTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(SettingsSchemaTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            SettingsSchemaTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME SettingsSchema COMMAND SettingsSchemaTests)

    add_executable(
        SettingsStoreTests
        "${CMAKE_SOURCE_DIR}/tests/SettingsStoreTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Schema.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Store.cpp"
        "${CMAKE_SOURCE_DIR}/src/Util/FileWatch.cpp"
    )

    target_include_directories(
        SettingsStoreTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(SettingsStoreTests PRIVATE cxx_std_23)
    target_precompile_headers(
        SettingsStoreTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(SettingsStoreTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            SettingsStoreTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME SettingsStore COMMAND SettingsStoreTests)

    add_executable(
        I18nTests
        "${CMAKE_SOURCE_DIR}/tests/I18nTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/I18n/I18n.cpp"
    )

    target_include_directories(
        I18nTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(I18nTests PRIVATE cxx_std_23)
    target_precompile_headers(
        I18nTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(I18nTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            I18nTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME I18n COMMAND I18nTests)

    # Header only: the arithmetic is what is worth pinning, and pulling in the
    # USER32 half would make the test depend on the keyboard layout it runs on.
    add_executable(KeyNamesTests "${CMAKE_SOURCE_DIR}/tests/KeyNamesTests.cpp")

    target_include_directories(
        KeyNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(KeyNamesTests PRIVATE cxx_std_23)
    target_precompile_headers(
        KeyNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(KeyNamesTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            KeyNamesTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME KeyNames COMMAND KeyNamesTests)

    add_executable(
        ShaderLayoutTests
        "${CMAKE_SOURCE_DIR}/tests/ShaderLayoutTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Shader/BSShaderLayout.cpp"
        "${CMAKE_SOURCE_DIR}/src/Util/SafeRead.cpp"
    )

    target_include_directories(
        ShaderLayoutTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ShaderLayoutTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ShaderLayoutTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ShaderLayoutTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ShaderLayoutTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ShaderLayout COMMAND ShaderLayoutTests)

    add_executable(
        ProfilerStatsTests
        "${CMAKE_SOURCE_DIR}/tests/ProfilerStatsTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/PassStatistics.cpp"
    )

    target_include_directories(
        ProfilerStatsTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ProfilerStatsTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ProfilerStatsTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ProfilerStatsTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ProfilerStatsTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ProfilerStats COMMAND ProfilerStatsTests)
endif()
