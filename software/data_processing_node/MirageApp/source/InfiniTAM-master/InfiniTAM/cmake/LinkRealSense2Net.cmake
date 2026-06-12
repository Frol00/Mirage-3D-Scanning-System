###########################
# LinkRealSense2Net.cmake #
###########################

IF(WITH_REALSENSE2_NET)
  TARGET_LINK_LIBRARIES(${targetname} ${RealSense2Net_LIBRARY})
  IF(WIN32)
    TARGET_LINK_LIBRARIES(${targetname} ws2_32)
    IF(RealSense2Net_RUNTIME_DLL AND EXISTS "${RealSense2Net_RUNTIME_DLL}")
      ADD_CUSTOM_COMMAND(TARGET ${targetname} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RealSense2Net_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${targetname}>")
    ELSE()
      GET_FILENAME_COMPONENT(RealSense2Net_LIBRARY_DIR "${RealSense2Net_LIBRARY}" DIRECTORY)
      GET_FILENAME_COMPONENT(RealSense2Net_LIBRARY_PARENT_DIR "${RealSense2Net_LIBRARY_DIR}" DIRECTORY)
      IF(EXISTS "${RealSense2Net_LIBRARY_PARENT_DIR}/Release/realsense2-net.dll")
        ADD_CUSTOM_COMMAND(TARGET ${targetname} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RealSense2Net_LIBRARY_PARENT_DIR}/Release/realsense2-net.dll" "$<TARGET_FILE_DIR:${targetname}>")
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()
