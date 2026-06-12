################################
# FindRealSense2Net.cmake      #
################################

SET(REALSENSE2_ROOT "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1" CACHE PATH "Root directory of librealsense")

FIND_PATH(RealSense2Net_INCLUDE_DIR librealsense2-net/rs_net.hpp
  HINTS
    "${REALSENSE2_ROOT}/include"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/include")

FIND_LIBRARY(RealSense2Net_LIBRARY NAMES realsense2-net
  HINTS
    "${REALSENSE2_ROOT}/build-vs18-net-winusb/src/ethernet/Release"
    "${REALSENSE2_ROOT}/build-vs18-net/src/ethernet/Release"
    "${REALSENSE2_ROOT}/build-vs18-net-winusb/src/ethernet/Debug"
    "${REALSENSE2_ROOT}/build-vs18-net/src/ethernet/Debug"
    "${REALSENSE2_ROOT}/lib"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net-winusb/src/ethernet/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net/src/ethernet/Release")

FIND_FILE(RealSense2Net_RUNTIME_DLL NAMES realsense2-net.dll
  HINTS
    "${REALSENSE2_ROOT}/build-vs18-net-winusb/Release"
    "${REALSENSE2_ROOT}/build-vs18-net/Release"
    "${REALSENSE2_ROOT}/bin"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net-winusb/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net/Release")

find_package_handle_standard_args(RealSense2Net DEFAULT_MSG RealSense2Net_LIBRARY RealSense2Net_INCLUDE_DIR)

MARK_AS_ADVANCED(RealSense2Net_LIBRARY RealSense2Net_INCLUDE_DIR RealSense2Net_RUNTIME_DLL)
