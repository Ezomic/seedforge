# Imported targets for the ozz libraries this engine uses. The upstream build
# installs the static libraries and headers but no export file.
get_filename_component(_ozz_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(_ozz_add_library name)
    if(TARGET ozz::${name})
        return()
    endif()
    add_library(ozz::${name} STATIC IMPORTED)
    set_target_properties(ozz::${name} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ozz_prefix}/include"
        IMPORTED_LOCATION_RELEASE "${_ozz_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${name}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        IMPORTED_LOCATION_DEBUG "${_ozz_prefix}/debug/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${name}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
    )
    set_target_properties(ozz::${name} PROPERTIES MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
endfunction()

_ozz_add_library(ozz_base)
_ozz_add_library(ozz_animation)
_ozz_add_library(ozz_animation_offline)
_ozz_add_library(ozz_geometry)
set_property(TARGET ozz::ozz_animation APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::ozz_base)
set_property(TARGET ozz::ozz_animation_offline APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::ozz_animation)
set_property(TARGET ozz::ozz_geometry APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::ozz_base)

set(ozz-animation_FOUND TRUE)
