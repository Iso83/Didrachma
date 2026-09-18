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

string(REPLACE "/" "_" MODULE_NAME "apps/studio")

set(MODULE_TARGET "${PROJECT_NAME}_${MODULE_NAME}")

add_executable(${MODULE_TARGET}
    ${MODULE_SRC}
    ${MODULE_PRIVATE_HEADERS}
    ${MODULE_PUBLIC_HEADERS}
)

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

target_compile_definitions(${MODULE_TARGET} 
    PRIVATE
        GLFW_INCLUDE_NONE
)

# ============================================================================
# IDE organization
# ============================================================================

cppcmake_project_set_ide_folder(${MODULE_TARGET} "apps/studio")

source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/include/${PROJECT_NAME}/apps/studio"
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
    PRIVATE
        ${PROJECT_NAME}::studio_core
        ${PROJECT_NAME}::stockChart_render
        ${PROJECT_NAME}::market_providers_yahoo
        ${PROJECT_NAME}_analysis_adapters_talib
        ScopeCanvas::integration_imgui
        ${IMGUI}
        ${GLFW}
        glad
)

cppcmake_dependency_copy_runtime(${MODULE_TARGET})
