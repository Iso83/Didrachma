cmake_minimum_required(VERSION 4.0)

project(Didrachma
    VERSION 0.1.0
    LANGUAGES C CXX
)

# =========================================================
# Project configuration
# =========================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# =========================================================
# CppCMake
# =========================================================

include(cmake/Dependencies.cmake)

dependencies_fetch_cppcmake()

cppcmake_project_initialize()

if(CPPCMAKE_PROJECT_ALREADY_REGISTERED)
    return()
endif()

cppcmake_githook_precommit()
cppcmake_version_generate()
cppcmake_test_setup()

# =========================================================
# Dependencies
# =========================================================

cppcmake_dependency_try_local_packages()

cppcmake_gitsubmodule_init_missing_targets(
    WORKING_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}"

    PATH
        "extern/CppDependencies"

    PATH
        "extern/ScopeCanvas"
    TARGET
        "ScopeCanvas_engine_core"
)

include("extern/CppDependencies/CppDependencies.cmake")

cppcmake_dependency_add_subdirectory_unless_target(
    "ScopeCanvas_engine_core"
    "extern/ScopeCanvas"
)

cppdependencies_glad(GLAD)
cppdependencies_glfw(GLFW)
cppdependencies_glm(GLM)
cppdependencies_imgui_docking(IMGUI)
cppdependencies_implot(IMPLOT ${IMGUI})
cppdependencies_curl(CURL SHARED)
cppdependencies_json_nlohmann(JSON)
cppdependencies_cli11(CLI11)
cppdependencies_talib(TALIB SHARED)

# =========================================================
# Modules
# =========================================================

cppcmake_module_add(market/core)
cppcmake_module_add(market/providers/yahoo)

cppcmake_module_add(analysis/core)
cppcmake_module_add(analysis/adapters/talib)

cppcmake_module_add(strategy/core)

cppcmake_module_add(stockChart/core)
cppcmake_module_add(stockChart/render)
cppcmake_module_add(studio/core)

if(${PROJECT_NAME}_BUILD_TESTS)
    cppcmake_module_add(tests/common)
endif()

cppcmake_module_add(apps/chart)
cppcmake_module_add(apps/studio)
