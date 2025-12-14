cmake_minimum_required(VERSION 3.20)
project(GTK_Test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# GTK4 via pkg-config (MSYS2 mingw64)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_executable(GTK_Test
  main.c
  app/app_state.c
  util/util.c
  ui/ui.c
  ui/ui_handlers.c
  device/device_protocol.c
  device/device_status.c
  loopback/loopback.c
)

if (WIN32)
  target_sources(GTK_Test PRIVATE
    serial/serial_win32.c
    platform/platform_win32.c
    device/device_discovery_win32.c
  )
else()
  target_sources(GTK_Test PRIVATE
    serial/serial_stub.c
    platform/platform_stub.c
    device/device_discovery_stub.c
  )
endif()

target_link_libraries(GTK_Test PRIVATE PkgConfig::GTK4)

target_include_directories(GTK_Test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Ensure UI file is available next to the executable (important when running from build dir / VS Code)
add_custom_command(TARGET GTK_Test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/ui/glade.ui
          $<TARGET_FILE_DIR:GTK_Test>/glade.ui
  COMMENT "Copy glade.ui next to executable"
)

# Optional: make sure we show useful warnings
if (MSVC)
  target_compile_options(GTK_Test PRIVATE /W4)
else()
  target_compile_options(GTK_Test PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Convenience: run target with MSYS2 DLLs in PATH (so you can run from build dir)
if (WIN32)
  add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E env
      "PATH=C:/msys64/mingw64/bin;$ENV{PATH}"
      $<TARGET_FILE:GTK_Test>
    DEPENDS GTK_Test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Run GTK_Test with MSYS2 mingw64 runtime in PATH"
    VERBATIM
  )
endif()