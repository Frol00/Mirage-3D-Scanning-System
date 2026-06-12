########################
# LinkRealSense2.cmake #
########################

IF(WITH_REALSENSE2)
  TARGET_LINK_LIBRARIES(${targetname} ${RealSense2_LIBRARY})
  IF(WIN32)
    IF(RealSense2_RUNTIME_DLL AND EXISTS "${RealSense2_RUNTIME_DLL}")
      ADD_CUSTOM_COMMAND(TARGET ${targetname} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RealSense2_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${targetname}>")
    ELSE()
      GET_FILENAME_COMPONENT(RealSense2_LIBRARY_DIR "${RealSense2_LIBRARY}" DIRECTORY)
      IF(EXISTS "${RealSense2_LIBRARY_DIR}/realsense2.dll")
        ADD_CUSTOM_COMMAND(TARGET ${targetname} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RealSense2_LIBRARY_DIR}/realsense2.dll" "$<TARGET_FILE_DIR:${targetname}>")
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()
