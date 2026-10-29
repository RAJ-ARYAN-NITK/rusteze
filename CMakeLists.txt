cmake_minimum_required(VERSION 3.16)
project(rustscan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui)
find_package(Threads REQUIRED)

add_library(rustcore src/segmenter.cpp src/severity.cpp src/report.cpp src/ollama.cpp)
target_include_directories(rustcore PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(rustcore PUBLIC ${OpenCV_LIBS} Threads::Threads)
target_include_directories(rustcore SYSTEM PUBLIC third_party)   # httplib + json (no warnings from vendored code)
if(WIN32)
  target_link_libraries(rustcore PUBLIC ws2_32)
endif()
if(MSVC)
  target_compile_options(rustcore PUBLIC /W3 /O2 /bigobj)
else()
  target_compile_options(rustcore PUBLIC -Wall -Wextra -O2)
endif()

add_executable(rust_server apps/server_main.cpp)
target_link_libraries(rust_server PRIVATE rustcore)

add_executable(rust_batch apps/batch_main.cpp)
target_link_libraries(rust_batch PRIVATE rustcore)

add_executable(rust_video apps/video_main.cpp)
target_link_libraries(rust_video PRIVATE rustcore)

enable_testing()
add_executable(rust_tests tests/test_main.cpp)
target_link_libraries(rust_tests PRIVATE rustcore)
add_test(NAME unit COMMAND rust_tests)
