cmake_minimum_required(VERSION 3.14)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug or Release")
endif()

CPMAddPackage(
        NAME gklib
        GIT_REPOSITORY git@github.com:KarypisLab/GKlib.git
        GIT_TAG METIS-v5.1.1-DistDGL-0.5
)
CPMAddPackage(
        NAME METIS
        GIT_REPOSITORY git@github.com:KarypisLab/METIS.git
        GIT_TAG v5.2.1
        DOWNLOAD_ONLY ON
)

set(CMAKE_C_STANDARD 99)

add_compile_definitions(
        "REALTYPEWIDTH=$<IF:$<BOOL:${REALTYPEWIDTH}>,${REALTYPEWIDTH},32>"
        "IDXTYPEWIDTH=$<IF:$<BOOL:${IDXTYPEWIDTH}>,${IDXTYPEWIDTH},32>"
)

include("${METIS_SOURCE_DIR}/conf/gkbuild.cmake")

# Build libmetis.
add_library(libmetis)
target_sources(libmetis PRIVATE
        ${METIS_SOURCE_DIR}/libmetis/auxapi.c
        ${METIS_SOURCE_DIR}/libmetis/contig.c
        ${METIS_SOURCE_DIR}/libmetis/graph.c
        ${METIS_SOURCE_DIR}/libmetis/mesh.c
        ${METIS_SOURCE_DIR}/libmetis/options.c
        ${METIS_SOURCE_DIR}/libmetis/srefine.c
        ${METIS_SOURCE_DIR}/libmetis/balance.c
        ${METIS_SOURCE_DIR}/libmetis/debug.c
        ${METIS_SOURCE_DIR}/libmetis/initpart.c
        ${METIS_SOURCE_DIR}/libmetis/meshpart.c
        ${METIS_SOURCE_DIR}/libmetis/parmetis.c
        ${METIS_SOURCE_DIR}/libmetis/stat.c
        ${METIS_SOURCE_DIR}/libmetis/bucketsort.c
        ${METIS_SOURCE_DIR}/libmetis/fm.c
        ${METIS_SOURCE_DIR}/libmetis/kmetis.c
        ${METIS_SOURCE_DIR}/libmetis/minconn.c
        ${METIS_SOURCE_DIR}/libmetis/pmetis.c
        ${METIS_SOURCE_DIR}/libmetis/timing.c
        ${METIS_SOURCE_DIR}/libmetis/checkgraph.c
        ${METIS_SOURCE_DIR}/libmetis/fortran.c
        ${METIS_SOURCE_DIR}/libmetis/kwayfm.c
        ${METIS_SOURCE_DIR}/libmetis/mincover.c
        ${METIS_SOURCE_DIR}/libmetis/refine.c
        ${METIS_SOURCE_DIR}/libmetis/util.c
        ${METIS_SOURCE_DIR}/libmetis/coarsen.c
        ${METIS_SOURCE_DIR}/libmetis/frename.c
        ${METIS_SOURCE_DIR}/libmetis/kwayrefine.c
        ${METIS_SOURCE_DIR}/libmetis/mmd.c
        ${METIS_SOURCE_DIR}/libmetis/separator.c
        ${METIS_SOURCE_DIR}/libmetis/wspace.c
        ${METIS_SOURCE_DIR}/libmetis/compress.c
        ${METIS_SOURCE_DIR}/libmetis/gklib.c
        ${METIS_SOURCE_DIR}/libmetis/mcutil.c
        ${METIS_SOURCE_DIR}/libmetis/ometis.c
        ${METIS_SOURCE_DIR}/libmetis/sfm.c
)
target_include_directories(libmetis
        PUBLIC
            ${METIS_SOURCE_DIR}/libmetis
            ${METIS_SOURCE_DIR}/include
        PRIVATE
            ${gklib_SOURCE_DIR}
)
target_link_libraries(libmetis
        PUBLIC GKlib
        PRIVATE $<$<BOOL:${UNIX}>:m>
)

add_subdirectory("${METIS_SOURCE_DIR}/libmetis")
