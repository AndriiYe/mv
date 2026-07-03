cmake_minimum_required(VERSION 3.16)

project(cv)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT OpenCV_DIR)
    set(OpenCV_DIR "C:/opencv/build/x64/vc16/lib")
endif()

find_package(OpenCV REQUIRED)

add_executable(cv
    src/main.cpp
    src/Jconfig.cpp
    src/local_tracker.cpp
    src/kalman_filtre.cpp
    src/PIDController.cpp
    src/crsf_rc.cpp
    src/crsf_tcp_packet_sender.cpp
    src/screen_capture.cpp
)

target_include_directories(cv PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(cv PRIVATE ${OpenCV_LIBS})

if(WIN32)
    target_sources(cv PRIVATE app.rc)
    target_link_libraries(cv PRIVATE user32 gdi32 winmm ws2_32)
endif()
