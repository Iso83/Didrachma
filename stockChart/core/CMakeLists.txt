# ============================================================================
# Source files
# ============================================================================

file(GLOB_RECURSE MODULE_SRC CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB_RECURSE MODULE_PRIVATE_HEADERS CONFIGURE_DEPENDS
    src/*.h
)

file(GLOB_RECURSE MODULE_PUBLIC_HEADERS CONFIGURE_DEPENDS
    include/*.h
)

# ============================================================================
# Target
# ============================================================================

string(REPLACE "/" "_" MODULE_NAME "stockChart/core")

set(MODULE_TARGET "${PROJECT_NAME}_${MODULE_NAME}")

add_library(${MODULE_TARGET}
    ${MODULE_SRC}
    ${MODULE_PRIVATE_HEADERS}
    ${MODULE_PUBLIC_HEADERS}
)

add_library(${PROJECT_NAME}::${MODULE_NAME} ALIAS ${MODULE_TARGET})

# ============================================================================
# Build configuration
# ============================================================================

cppcmake_version_target(${MODULE_TARGET})
cppcmake_helper_include_target(${MODULE_TARGET})

target_include_directories(${MODULE_TARGET}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(${MODULE_TARGET}
    PUBLIC
        cxx_std_20
)

# ============================================================================
# IDE organization
# ============================================================================

cppcmake_project_set_ide_folder(${MODULE_TARGET} "stockChart/core")

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/${PROJECT_NAME}/stockChart/core"
    PREFIX "Include"
    FILES
        ${MODULE_PUBLIC_HEADERS}
)

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PREFIX "Source"
    FILES
        ${MODULE_SRC}
        ${MODULE_PRIVATE_HEADERS}
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(${MODULE_TARGET}
    PUBLIC
        ${PROJECT_NAME}::market_core
        ${PROJECT_NAME}::analysis_core
    PRIVATE
        ${JSON}
)

# ============================================================================
# Tests
# ============================================================================

cppcmake_test_add(
    NAME
        Test_${MODULE_TARGET}_Document
    TARGET
        ${MODULE_TARGET}
    MODULE
        "stockChart/core"
    SOURCES
        tests/Document.cpp
)

cppcmake_test_add(
    NAME
        Test_${MODULE_TARGET}_Profile
    TARGET
        ${MODULE_TARGET}
    MODULE
        "stockChart/core"
    SOURCES
        tests/Profile.cpp
)
