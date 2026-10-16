cmake_minimum_required(VERSION 3.22)
set(SC_MODULE mupdf)
project(simply-cpp-${SC_MODULE} LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_MUPDF_BUILD_TESTS "Build simply-cpp-mupdf tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.6.1)
# MuPDF has no CMake package: cmake/Findmupdf.cmake finds it (Homebrew's, or Ubuntu's libmupdf-dev:
# static on jammy and noble, shared on resolute).
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_or_install_package(mupdf libmupdf-dev mupdf)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

# MuPDF stays out of the installed header; mupdf.h only names its types.
add_sc_object(mupdf
        LINK_LIBRARIES sc::sc-core mupdf::mupdf
        PUBLIC_LINK_LIBRARIES sc::sc-core mupdf::mupdf)

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

# The runtime dependencies (resolute's libmupdf, or the libraries a static libmupdf needs) come from
# dpkg-shlibdeps. A consumer links the static library too, so the -dev package brings MuPDF's
# headers and, for jammy's and noble's static libmupdf, the -dev packages of what it needs.
package_sc_module(
        DEV_DEPENDS "simply-cpp-dev (>= 1.6.1~)" libmupdf-dev libmujs-dev libgumbo-dev libharfbuzz-dev
        libfreetype-dev libjbig2dec0-dev libopenjp2-7-dev libjpeg-dev zlib1g-dev)
