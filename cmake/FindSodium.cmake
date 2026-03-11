# FindSodium.cmake — locate the libsodium library
#
# Sets:
#   Sodium_FOUND
#   Sodium_INCLUDE_DIR
#   Sodium_LIBRARY
#   Sodium_VERSION
#
# Creates imported target: Sodium::Sodium

find_path(Sodium_INCLUDE_DIR
    NAMES sodium.h
    PATH_SUFFIXES sodium)

find_library(Sodium_LIBRARY
    NAMES sodium libsodium)

if (Sodium_INCLUDE_DIR AND Sodium_LIBRARY)
    # Extract version from sodium/version.h
    if (EXISTS "${Sodium_INCLUDE_DIR}/sodium/version.h")
        file(STRINGS "${Sodium_INCLUDE_DIR}/sodium/version.h" _ver
            REGEX "SODIUM_VERSION_STRING")
        string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ ${_ver})
        set(Sodium_VERSION "${CMAKE_MATCH_1}")
    elseif(EXISTS "${Sodium_INCLUDE_DIR}/sodium.h")
        # May be a flat layout
        file(STRINGS "${Sodium_INCLUDE_DIR}/sodium.h" _inc_lines REGEX "#include")
        # try version file alongside
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sodium
    REQUIRED_VARS Sodium_LIBRARY Sodium_INCLUDE_DIR
    VERSION_VAR   Sodium_VERSION)

if (Sodium_FOUND AND NOT TARGET Sodium::Sodium)
    add_library(Sodium::Sodium UNKNOWN IMPORTED)
    set_target_properties(Sodium::Sodium PROPERTIES
        IMPORTED_LOCATION             "${Sodium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Sodium_INCLUDE_DIR}")
endif()

mark_as_advanced(Sodium_INCLUDE_DIR Sodium_LIBRARY)
