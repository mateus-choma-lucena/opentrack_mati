cmake_minimum_required(VERSION 3.13)
project(opentrack_verify_opencv LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OpenCV CI verification requires an MSVC x64 developer environment. Run through tools/dev-command.bat.")
endif()
if(NOT EXISTS "${OpenCV_DIR}/OpenCVConfig.cmake")
    message(FATAL_ERROR "OpenCVConfig.cmake is missing from OpenCV_DIR='${OpenCV_DIR}'. Run tools/install-opencv.ps1 first.")
endif()

set(_components core imgproc video imgcodecs videoio calib3d highgui flann features2d)
find_package(OpenCV 4.12.0 EXACT REQUIRED CONFIG COMPONENTS ${_components})
if(NOT OpenCV_SHARED)
    message(FATAL_ERROR "The pinned Windows SDK should contain shared OpenCV libraries.")
endif()
foreach(_component IN LISTS _components)
    if(NOT TARGET opencv_${_component})
        message(FATAL_ERROR "Required OpenCV target is missing: opencv_${_component}")
    endif()
    foreach(_property IMPORTED_IMPLIB_RELEASE IMPORTED_LOCATION_RELEASE)
        get_target_property(_file opencv_${_component} ${_property})
        if(NOT _file OR NOT EXISTS "${_file}")
            message(FATAL_ERROR "OpenCV ${_component} ${_property} is missing: '${_file}'. Reinstall the SDK.")
        endif()
    endforeach()
endforeach()

# Prove that the headers and libraries work together with the active compiler,
# including the Lucas-Kanade optical-flow symbols used by NeuralNet.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
try_compile(_opencv_linked "${CMAKE_CURRENT_BINARY_DIR}/link-check"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/probe.cpp"
    LINK_LIBRARIES ${OpenCV_LIBS}
    OUTPUT_VARIABLE _link_log)
if(NOT _opencv_linked)
    message(FATAL_ERROR "OpenCV development SDK could not compile/link with MSVC x64. Reinstall the SDK and check the compiler environment.\n${_link_log}")
endif()
message(STATUS "Verified OpenCV ${OpenCV_VERSION}: ${_components}; headers, Release import libraries, runtime DLLs, and optical-flow linking")
