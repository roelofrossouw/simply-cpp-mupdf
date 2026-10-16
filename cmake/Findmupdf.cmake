# Finds MuPDF, which ships no CMake package, as mupdf::mupdf.
#
# It comes three ways: Homebrew has a shared libmupdf plus libmupdf-third; Ubuntu resolute (1.27)
# a shared libmupdf with mupdf.pc; jammy (1.19) and noble (1.23) only the static libmupdf.a and
# libmupdf-third.a, without their dependencies. For a static libmupdf the libraries it was built
# against are linked too: the list Debian's mupdf package builds with.
#
# Sets mupdf_FOUND, mupdf_VERSION (from fitz/version.h) and the mupdf::mupdf imported target.

find_path(SC_MUPDF_INCLUDE_DIR NAMES mupdf/fitz.h)
find_library(SC_MUPDF_LIBRARY NAMES mupdf)
find_library(SC_MUPDF_THIRD_LIBRARY NAMES mupdf-third)

if (SC_MUPDF_INCLUDE_DIR AND EXISTS "${SC_MUPDF_INCLUDE_DIR}/mupdf/fitz/version.h")
    file(STRINGS "${SC_MUPDF_INCLUDE_DIR}/mupdf/fitz/version.h" sc_mupdf_version_line REGEX "^#define FZ_VERSION \"")
    string(REGEX REPLACE "^#define FZ_VERSION \"([^\"]*)\".*" "\\1" mupdf_VERSION "${sc_mupdf_version_line}")
endif ()

set(sc_mupdf_dependencies)
if (SC_MUPDF_LIBRARY MATCHES "\\.a$")
    foreach (library mujs gumbo harfbuzz freetype jbig2dec openjp2 jpeg z)
        string(TOUPPER "SC_MUPDF_${library}_LIBRARY" cached)
        find_library(${cached} NAMES ${library})
        if (${cached})
            list(APPEND sc_mupdf_dependencies "${${cached}}")
        else ()
            message(STATUS "mupdf: static libmupdf, but its dependency ${library} wasn't found")
            set(SC_MUPDF_LIBRARY SC_MUPDF_LIBRARY-NOTFOUND)
        endif ()
    endforeach ()
    list(APPEND sc_mupdf_dependencies m)
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(mupdf
        REQUIRED_VARS SC_MUPDF_LIBRARY SC_MUPDF_INCLUDE_DIR
        VERSION_VAR mupdf_VERSION)

if (mupdf_FOUND AND NOT TARGET mupdf::mupdf)
    add_library(mupdf::mupdf UNKNOWN IMPORTED)
    set_target_properties(mupdf::mupdf PROPERTIES
            IMPORTED_LOCATION "${SC_MUPDF_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SC_MUPDF_INCLUDE_DIR}")
    set(sc_mupdf_link)
    if (SC_MUPDF_THIRD_LIBRARY)
        list(APPEND sc_mupdf_link "${SC_MUPDF_THIRD_LIBRARY}")
    endif ()
    list(APPEND sc_mupdf_link ${sc_mupdf_dependencies})
    if (sc_mupdf_link)
        set_target_properties(mupdf::mupdf PROPERTIES INTERFACE_LINK_LIBRARIES "${sc_mupdf_link}")
    endif ()
endif ()
mark_as_advanced(SC_MUPDF_INCLUDE_DIR SC_MUPDF_LIBRARY SC_MUPDF_THIRD_LIBRARY)
