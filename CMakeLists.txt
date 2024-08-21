cmake_minimum_required(VERSION 3.29)
project(per_meshlet_nuances)

set(CMAKE_CXX_STANDARD 20)

# add dependencies
include(cmake/CPM.cmake)

CPMAddPackage("gh:zeux/meshoptimizer#v0.21")
#CPMAddPackage(
#        NAME gklib
#        GIT_REPOSITORY git@github.com:KarypisLab/GKlib.git
#        GIT_TAG METIS-v5.1.1-DistDGL-0.5
#        DOWNLOAD_ONLY ON
#)
#CPMAddPackage(
#        NAME metis
#        GIT_REPOSITORY git@github.com:KarypisLab/METIS.git
#        GIT_TAG v5.2.1
#        DOWNLOAD_ONLY ON
#)
#execute_process(COMMAND bash -c "make -C ${metis_SOURCE_DIR} config")
#set(GKLIB_PATH ${gklib_SOURCE_DIR})
#add_subdirectory(${metis_SOURCE_DIR})
include(cmake/metis.cmake)

#CPMAddPackage("gh:scivision/METIS#v5.2.1.1")

add_library(per_meshlet_nuances src/main.cpp)


add_executable(example src/main.cpp)
target_link_libraries(example meshoptimizer)
target_link_libraries(example libmetis)
