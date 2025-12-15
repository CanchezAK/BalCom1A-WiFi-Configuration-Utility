cmake_minimum_required(VERSION 3.20)
project(BalCom1A_Configuration_Utility C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# GTK4 via pkg-config (MSYS2 mingw64)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

add_library(platform STATIC
  platform/platform.c
  platform/platform.h
  platform/serial.c
  platform/serial.h
)

target_include_directories(platform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(platform PUBLIC PkgConfig::GLIB)

if (WIN32)
  target_link_libraries(platform PRIVATE user32)
endif()

add_executable(BalCom1A_Configuration_Utility
  main.c
  app/app_state.c
  app/app_known_networks.c
  util/util.c
  ui/ui.c
  ui/ui_handlers.c
  device/device_discovery.c
  device/device_protocol.c
  device/device_status.c
  loopback/loopback.c
)

# On Windows, avoid opening an extra console window when launching by double-click.
if (WIN32)
  set_target_properties(BalCom1A_Configuration_Utility PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Human-friendly binary name (shown to end users).
set_target_properties(BalCom1A_Configuration_Utility PROPERTIES OUTPUT_NAME "BalCom1A Configuration Utility")

# Optional: console variant for debugging (keeps stdout/stderr visible).
add_executable(BalCom1A_Configuration_Utility_console
  main.c
  app/app_state.c
  app/app_known_networks.c
  util/util.c
  ui/ui.c
  ui/ui_handlers.c
  device/device_discovery.c
  device/device_protocol.c
  device/device_status.c
  loopback/loopback.c
)

set_target_properties(BalCom1A_Configuration_Utility_console PROPERTIES OUTPUT_NAME "BalCom1A Configuration Utility Console")

target_link_libraries(BalCom1A_Configuration_Utility PRIVATE PkgConfig::GTK4 platform)
target_link_libraries(BalCom1A_Configuration_Utility_console PRIVATE PkgConfig::GTK4 platform)

target_include_directories(BalCom1A_Configuration_Utility PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(BalCom1A_Configuration_Utility_console PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Ensure UI file is available next to the executable (important when running from build dir / VS Code)
add_custom_command(TARGET BalCom1A_Configuration_Utility POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/ui/glade.ui
          $<TARGET_FILE_DIR:BalCom1A_Configuration_Utility>/glade.ui
  COMMENT "Copy glade.ui next to executable"
)

add_custom_command(TARGET BalCom1A_Configuration_Utility_console POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/ui/glade.ui
          $<TARGET_FILE_DIR:BalCom1A_Configuration_Utility_console>/glade.ui
  COMMENT "Copy glade.ui next to executable (console target)"
)

# Optional: make sure we show useful warnings
if (MSVC)
  target_compile_options(BalCom1A_Configuration_Utility PRIVATE /W4)
  target_compile_options(BalCom1A_Configuration_Utility_console PRIVATE /W4)
else()
  target_compile_options(BalCom1A_Configuration_Utility PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(BalCom1A_Configuration_Utility_console PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Convenience: run target with MSYS2 DLLs in PATH (so you can run from build dir)
if (WIN32)
  add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E env
      "PATH=C:/msys64/mingw64/bin;$ENV{PATH}"
      $<TARGET_FILE:BalCom1A_Configuration_Utility>
    DEPENDS BalCom1A_Configuration_Utility
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Run BalCom1A Configuration Utility with MSYS2 mingw64 runtime in PATH"
    VERBATIM
  )
endif()