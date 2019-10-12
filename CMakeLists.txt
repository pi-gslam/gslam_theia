cmake_minimum_required(VERSION 2.8)
project(gslam_theia)

include(cmake/PICMake.cmake)

set(CMAKE_CXX_STANDARD 11)

if(THEIA_INSTALLED)
pi_add_target(gslam_theia SHARED TheiaSfM.cpp REQUIRED Theia GSLAM)
else()
pi_collect_packages(VERBOSE REQUIRED GSLAM Eigen3)
set(DISABLE_APPLICATIONS true)
set(BUILD_TESTING OFF)
add_subdirectory(TheiaSfM)
pi_add_target(gslam_theia SHARED TheiaSfM.cpp REQUIRED GSLAM Eigen3 DEPENDENCY theia)
endif()

pi_report_targets()
pi_install(TARGETS gslam_theia)
