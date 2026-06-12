

SET(REALSENSE2_ROOT "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1" CACHE PATH "Root directory of librealsense")

FIND_PATH(RealSense2_INCLUDE_DIR librealsense2/rs.hpp
  HINTS
    "${REALSENSE2_ROOT}/include"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/include"
    "${PROJECT_SOURCE_DIR}/../../librealsense-master/include")
FIND_LIBRARY(RealSense2_LIBRARY NAMES realsense2 librealsense2
  HINTS
    "${REALSENSE2_ROOT}/build-vs18-net-winusb/Release"
    "${REALSENSE2_ROOT}/build-vs18-net/Release"
    "${REALSENSE2_ROOT}/lib"
    "${REALSENSE2_ROOT}/lib/librealsense2.dylib"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net-winusb/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-master/build-vs18/Release")

FIND_FILE(RealSense2_RUNTIME_DLL NAMES realsense2.dll
  HINTS
    "${REALSENSE2_ROOT}/build-vs18-net-winusb/Release"
    "${REALSENSE2_ROOT}/build-vs18-net/Release"
    "${REALSENSE2_ROOT}/bin"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net-winusb/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-2.53.1/build-vs18-net/Release"
    "${PROJECT_SOURCE_DIR}/../../librealsense-master/build-vs18/Release")

# handle the QUIETLY and REQUIRED arguments and set REALSENSE2_FOUND to TRUE if
# all listed variables are TRUE
#include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
#include(${CMAKE_MODULE_PATH}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(RealSense2 DEFAULT_MSG RealSense2_LIBRARY RealSense2_INCLUDE_DIR)

#if(OPENNI_FOUND)
#  set(OpenNI_LIBRARIES ${OpenNI_LIBRARY})
#endif()

MARK_AS_ADVANCED(RealSense2_LIBRARY RealSense2_INCLUDE_DIR RealSense2_RUNTIME_DLL)
