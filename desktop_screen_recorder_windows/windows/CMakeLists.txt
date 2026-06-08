cmake_minimum_required(VERSION 3.14)
set(PROJECT_NAME "desktop_screen_recorder_windows")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "desktop_screen_recorder_windows_plugin")

add_library(${PLUGIN_NAME} SHARED
  "desktop_screen_recorder_windows_plugin.cpp"
  "desktop_screen_recorder_windows_plugin_c_api.cpp"
  "windows_recorder_plugin.cpp"
  "windows_native_bridge.cpp"
  "windows_native_bridge_capture.cpp"
  "windows_native_bridge_clipboard.cpp"
  "windows_native_bridge_recording.cpp"
  "windows_native_bridge_capability.cpp"
  "windows_native_bridge_video_editor.cpp"
  "windows_native_runtime.cpp"
  "audio_capture.cpp"
  "gif_encoder.cpp"
  "recording_overlay.cpp"
  "recording_session.cpp"
)

apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  d3d11
  dxgi
  dwmapi
  mf
  mfplat
  mfreadwrite
  mfuuid
  ole32
  propsys
  runtimeobject
  shcore
  windowscodecs
)

set(desktop_screen_recorder_windows_bundled_libraries
  ""
  PARENT_SCOPE
)
