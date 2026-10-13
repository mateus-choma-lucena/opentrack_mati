include(opentrack-boilerplate)
find_package(OpenCV QUIET)

if(OpenCV_FOUND)
    # OpenCV 5.0 split the calib3d module into calib, geometry, stereo, and
    # ptcloud, and renamed features2d to features. The opencv_calib3d and
    # opencv_features2d CMake targets no longer exist in OpenCV 5.
    #
    # Detect availability via TARGET checks (the two module sets are mutually
    # exclusive across OpenCV 4 and 5) rather than version-string parsing.
    # On OpenCV 4 these expand to exactly the previous target names, so
    # existing behavior is unchanged.
    if(TARGET opencv_calib3d)
        set(OTR_OPENCV_CALIB_MODULES opencv_calib3d)
        set(OTR_OPENCV_FEATURES_MODULES opencv_features2d)
    elseif(TARGET opencv_calib)
        set(OTR_OPENCV_CALIB_MODULES opencv_calib opencv_geometry)
        set(OTR_OPENCV_FEATURES_MODULES opencv_features)
    else()
        set(OTR_OPENCV_CALIB_MODULES "")
        set(OTR_OPENCV_FEATURES_MODULES "")
    endif()

    # Keep the module variables in each caller's directory scope, but register
    # runtime installation only once across all OpenCV consumers.
    get_property(_opencv_install_registered GLOBAL PROPERTY OTR_OPENCV_INSTALL_REGISTERED)
    if(NOT _opencv_install_registered)
        set_property(GLOBAL PROPERTY OTR_OPENCV_INSTALL_REGISTERED TRUE)
        foreach(i ${OTR_OPENCV_CALIB_MODULES} ${OTR_OPENCV_FEATURES_MODULES} opencv_flann
                opencv_core opencv_highgui opencv_imgcodecs opencv_imgproc opencv_video opencv_videoio)
            otr_install_pdb_for_imported_target(${i})
            # Component targets resolve to opencv_world in official SDKs, or
            # individual DLLs in shared source builds. The helper skips .lib.
            otr_install_lib(${i} "${opentrack-libexec}")
        endforeach()
        if(WIN32 AND OpenCV_SHARED AND TARGET opencv_videoio)
            get_target_property(_opencv_videoio_runtime opencv_videoio IMPORTED_LOCATION_RELEASE)
            if(_opencv_videoio_runtime)
                get_filename_component(_opencv_runtime_directory "${_opencv_videoio_runtime}" DIRECTORY)
                file(GLOB _opencv_videoio_plugins "${_opencv_runtime_directory}/opencv_videoio_ffmpeg*.dll")
                foreach(_plugin IN LISTS _opencv_videoio_plugins)
                    otr_install_lib("${_plugin}" "${opentrack-libexec}")
                endforeach()
            endif()
        endif()
    endif()
endif()
