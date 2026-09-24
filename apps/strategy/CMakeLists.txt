# ============================================================================
# Source files
# ============================================================================

file(GLOB_RECURSE MODULE_SRC CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB_RECURSE MODULE_PRIVATE_HEADERS CONFIGURE_DEPENDS
    src/*.h
)

set(MODULE_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
list(REMOVE_ITEM MODULE_SRC "${MODULE_MAIN}")

# ============================================================================
# Target
# ============================================================================

string(REPLACE "/" "_" MODULE_NAME "apps/strategy")

set(MODULE_TARGET "${PROJECT_NAME}_${MODULE_NAME}")
set(MODULE_TARGET_SUPPORT "${MODULE_TARGET}_support")

add_executable(${MODULE_TARGET}
    ${MODULE_MAIN}
)

add_library(${MODULE_TARGET_SUPPORT} INTERFACE)

target_sources(${MODULE_TARGET_SUPPORT}
    INTERFACE
        ${MODULE_SRC}
        ${MODULE_PRIVATE_HEADERS}
)

# ============================================================================
# Build configuration
# ============================================================================

cppcmake_version_target(${MODULE_TARGET})
cppcmake_helper_include_target(${MODULE_TARGET})

target_include_directories(${MODULE_TARGET_SUPPORT}
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(${MODULE_TARGET_SUPPORT}
    INTERFACE
        cxx_std_20
)

# ============================================================================
# IDE organization
# ============================================================================

cppcmake_project_set_ide_folder(${MODULE_TARGET} "apps/strategy")

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PREFIX "Source"
    FILES
        ${MODULE_MAIN}
        ${MODULE_SRC}
        ${MODULE_PRIVATE_HEADERS}
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(${MODULE_TARGET_SUPPORT}
    INTERFACE
        ${PROJECT_NAME}::strategy_core
        ${PROJECT_NAME}::analysis_adapters_talib
        ${PROJECT_NAME}::market_providers_yahoo
        ${CLI11}
        ${JSON}
)

target_link_libraries(${MODULE_TARGET}
    PRIVATE
        ${MODULE_TARGET_SUPPORT}
)

cppcmake_dependency_copy_runtime(${MODULE_TARGET})

# ============================================================================
# Tests
# ============================================================================

cppcmake_test_add(
    NAME
        Test_${MODULE_TARGET}_EndToEnd
    TARGET
        ${MODULE_TARGET_SUPPORT}
    MODULE
        "apps/strategy"
    SOURCES
        tests/EndToEnd.cpp
    COMPILE_DEFINITIONS
        DIDRACHMA_STRATEGY_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)

option(DIDRACHMA_ENABLE_YAHOO_STRATEGY_SMOKE "Enable the network-backed AAPL strategy smoke test" OFF)
if(DIDRACHMA_ENABLE_YAHOO_STRATEGY_SMOKE)
    add_test(
        NAME Test_${MODULE_TARGET}_YahooSmoke
        COMMAND ${MODULE_TARGET}
            --strategy "${CMAKE_CURRENT_SOURCE_DIR}/examples/aapl-yahoo-smoke.strategy.json"
            --subject AAPL
            --from 2025-01-02T00:00:00Z
            --through 2025-01-31T23:59:59Z
            --provider yahoo
    )
    set_tests_properties(Test_${MODULE_TARGET}_YahooSmoke PROPERTIES LABELS "integration;network")
endif()
