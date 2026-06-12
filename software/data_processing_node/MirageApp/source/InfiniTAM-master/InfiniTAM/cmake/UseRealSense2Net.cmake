##########################
# UseRealSense2Net.cmake #
##########################

OPTION(WITH_REALSENSE2_NET "Build with Intel RealSense SDK 2 network support?" OFF)

IF(WITH_REALSENSE2_NET)
  IF(NOT WITH_REALSENSE2)
    SET(WITH_REALSENSE2 ON CACHE BOOL "Build with Intel RealSense SDK 2 support?" FORCE)
    FIND_PACKAGE(RealSense2 REQUIRED)
    INCLUDE_DIRECTORIES(${RealSense2_INCLUDE_DIR})
    ADD_DEFINITIONS(-DCOMPILE_WITH_RealSense2)
  ENDIF()

  FIND_PACKAGE(RealSense2Net REQUIRED)
  INCLUDE_DIRECTORIES(${RealSense2Net_INCLUDE_DIR})
  ADD_DEFINITIONS(-DCOMPILE_WITH_RealSense2Net)
ENDIF()
