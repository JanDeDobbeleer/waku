# app: process services (single instance, CLI pipe, hotkeys, tray, file watcher, logging).
add_library(yasb_app STATIC
  app_services.h
  app_services.cpp
  runtime.h
  runtime.cpp
  cli_server.h
  cli_server.cpp
  file_watcher.h
  file_watcher.cpp
  hotkeys.h
  hotkeys.cpp
  log.h
  log.cpp
  single_instance.h
  single_instance.cpp
  tray_icon.h
  tray_icon.cpp
)
target_include_directories(yasb_app PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_app
  PUBLIC yasb_widgets
  PRIVATE yasb_options)

# Composition root. main.cpp is the only WinRT user (winrt::init_apartment), so the WinRT libs
# stay on the executable.
add_executable(yasb-native WIN32 main.cpp)
target_link_libraries(yasb-native PRIVATE yasb_app yasb_options windowsapp runtimeobject)
# The tray hook DLL ships next to the executable.
add_dependencies(yasb-native YASBNativeTrayHook)
# Keep build\<preset>\yasb-native.exe where docs and tools expect it.
set_target_properties(yasb-native PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# Sounds and other runtime assets sit in assets\ next to the executable (the clock's alarm sound).
add_custom_command(TARGET yasb-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:yasb-native>/assets"
  VERBATIM)
