cmake_minimum_required(VERSION 3.22)
set(SC_MODULE mupdf)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-mupdf" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_MUPDF_BUILD_TESTS "Build simply-cpp-mupdf tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.20.0)
# sc::canvas, which the writer implements, and sc::drawing.
find_or_install_package(sc-graphics simply-cpp-graphics-dev simply-cpp-graphics 6.3.0)
# MuPDF has no CMake package: cmake/Findmupdf.cmake finds it (Homebrew's, or Ubuntu's libmupdf-dev:
# static on jammy and noble, shared on resolute).
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_or_install_package(mupdf libmupdf-dev mupdf)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

# MuPDF stays out of the installed headers (fonts are found by sc-graphics' sc::font_face); mupdf.h only names MuPDF's types.
add_sc_object(mupdf
        SOURCES src/mupdf_writer.cpp include/mupdf_writer.h src/fitz_call.h
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        LINK_LIBRARIES sc::sc-core sc::sc-graphics mupdf::mupdf
        PUBLIC_LINK_LIBRARIES sc::sc-core sc::sc-graphics mupdf::mupdf)

# Newer MuPDFs (resolute's 1.27, Homebrew's) can embed only the glyphs a PDF uses; jammy's 1.19
# and noble's 1.23 embed whole fonts.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${SC_MUPDF_INCLUDE_DIR})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY) # compile only: linking MuPDF here isn't needed
check_cxx_source_compiles("#include <mupdf/pdf.h>
int main() { pdf_subset_fonts(nullptr, nullptr, 0, nullptr); }" SC_MUPDF_HAS_SUBSET_FONTS)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
if (SC_MUPDF_HAS_SUBSET_FONTS)
    target_compile_definitions(sc-obj-mupdf PRIVATE SC_MUPDF_HAS_SUBSET_FONTS)
endif ()

add_sc_libraries(DESCRIPTION "simply-cpp-mupdf")

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake cmake/Findmupdf.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-mupdf
        COMPONENT development)

if (SC_MUPDF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-mupdf-test)
endif ()

add_sc_example(sc-mupdf-demo)
add_sc_example(sc-mupdf-write)

# The runtime dependencies (resolute's libmupdf, or the libraries a static libmupdf needs) come from
# dpkg-shlibdeps. A consumer links the static library too, so the -dev package brings MuPDF's
# headers and, for jammy's and noble's static libmupdf, the -dev packages of what it needs.
package_sc_module(
        DEV_DEPENDS "simply-cpp-dev (>= 1.20.0~)" "simply-cpp-graphics-dev (>= 6.3.0~)" libmupdf-dev libmujs-dev libgumbo-dev libharfbuzz-dev
        libfreetype-dev libjbig2dec0-dev libopenjp2-7-dev libjpeg-dev zlib1g-dev)
