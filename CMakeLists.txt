cmake_minimum_required(VERSION 3.16)
project(cv_multi_apps CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui imgcodecs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 1) scan_app
# add_executable(scan_app src/main.cpp)
# target_link_libraries(scan_app PRIVATE ${OpenCV_LIBS})
# target_include_directories(scan_app PRIVATE ${OpenCV_INCLUDE_DIRS})

# # 2) edge_app
# add_executable(edge_app src/text.cpp)  # ← 파일명/경로 확인!
# target_link_libraries(edge_app PRIVATE ${OpenCV_LIBS})
# target_include_directories(edge_app PRIVATE ${OpenCV_INCLUDE_DIRS})

add_executable(warp_app src/warp.cpp)
target_link_libraries(warp_app PRIVATE ${OpenCV_LIBS})
target_include_directories(warp_app PRIVATE ${OpenCV_INCLUDE_DIRS})

# (옵션) 공통 경고
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# (옵션) 빌드 타입 기본값
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 디버그 출력(선택)
message(STATUS "OpenCV_VERSION = ${OpenCV_VERSION}")
message(STATUS "OpenCV_INCLUDE_DIRS = ${OpenCV_INCLUDE_DIRS}")
