cmake_minimum_required(VERSION 3.20)
project(BalCom1A_Configuration_Utility VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -----------------------------
# Optional: bundle flashing tool (esptool)
# -----------------------------
# End users typically won't have Python/esptool installed.
# When enabled, we copy an existing flasher executable into the install folder
# (and next to the built .exe for local runs).
option(BALCOM_BUNDLE_FLASH_TOOL "Bundle a flashing tool (esptool) with the app" OFF)
set(BALCOM_FLASH_TOOL_EXECUTABLE "" CACHE FILEPATH "Path to flasher executable to bundle (recommended: standalone esptool/espflash binary)")
set(BALCOM_FLASH_TOOL_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/flasher/THIRD_PARTY_NOTICES.txt" CACHE FILEPATH "Optional third-party notices file to ship with the bundled flasher")

# Optional: let the Windows NSIS installer download esptool.exe during installation.
# This keeps the installer smaller and avoids bundling the flasher inside the .exe.
option(BALCOM_INSTALLER_DOWNLOAD_FLASH_TOOL "(Windows/NSIS) Download esptool.exe during installation" OFF)
set(BALCOM_FLASH_TOOL_DOWNLOAD_URL "" CACHE STRING "(Windows/NSIS) URL to download esptool.exe from")
set(BALCOM_FLASH_TOOL_SHA256 "" CACHE STRING "(Windows/NSIS) Expected SHA256 of downloaded esptool.exe (hex, optional; if empty, skip verification)")

# If a prebuilt flasher binary is present in packaging/flasher/, enable bundling
# by default so that released installers (.exe / .deb) always include the tool
# without requiring extra CMake flags.
if (BALCOM_FLASH_TOOL_EXECUTABLE STREQUAL "")
  if (WIN32)
    set(_BALCOM_DEFAULT_FLASHER "${CMAKE_CURRENT_SOURCE_DIR}/packaging/flasher/esptool.exe")
  else()
    set(_BALCOM_DEFAULT_FLASHER "${CMAKE_CURRENT_SOURCE_DIR}/packaging/flasher/esptool")
  endif()

  if (EXISTS "${_BALCOM_DEFAULT_FLASHER}")
    set(BALCOM_FLASH_TOOL_EXECUTABLE "${_BALCOM_DEFAULT_FLASHER}" CACHE FILEPATH "Path to flasher executable to bundle (auto-detected from packaging/flasher)" FORCE)
    if (NOT BALCOM_BUNDLE_FLASH_TOOL)
      set(BALCOM_BUNDLE_FLASH_TOOL ON CACHE BOOL "Bundle a flashing tool (esptool) with the app" FORCE)
    endif()
  endif()
endif()

# -----------------------------
# Application icon (Windows)
# -----------------------------
# - Embed icon into the produced .exe (taskbar/start menu/shortcuts will show it)
# - Use the same icon for the NSIS installer UI
set(BALCOM_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/B1A Configuration Utility.ico")
set(BALCOM_APP_ICON_RC  "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")

# -----------------------------
# Versioning (installer filename)
# -----------------------------
# Goal:
# - CPACK_PACKAGE_VERSION should be a clean numeric dotted version for installers.
# - CPACK_PACKAGE_FILE_NAME should include a git/date suffix to distinguish builds.

set(BALCOM_VERSION_NUM "${PROJECT_VERSION}")
string(TIMESTAMP BALCOM_BUILD_DATE "%Y.%m.%d" UTC)
set(BALCOM_VERSION_SUFFIX "${BALCOM_BUILD_DATE}")

if (WIN32)
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BALCOM_SYSTEM_TAG "win64")
  else()
    set(BALCOM_SYSTEM_TAG "win32")
  endif()
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" BALCOM_SYSTEM_TAG)
endif()

find_package(Git QUIET)
if (GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --dirty --always
    RESULT_VARIABLE _git_rc
    OUTPUT_VARIABLE _git_desc
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (_git_rc EQUAL 0 AND NOT _git_desc STREQUAL "")
    # If tag looks like vX.Y.Z or X.Y.Z, use it as numeric version.
    set(_git_tag "${_git_desc}")
    string(REGEX REPLACE "^v" "" _git_tag "${_git_tag}")
    string(REGEX MATCH "^([0-9]+\.[0-9]+\.[0-9]+)" _semver_match "${_git_tag}")
    if (_semver_match)
      set(BALCOM_VERSION_NUM "${CMAKE_MATCH_1}")
    endif()

    # For file name suffix, keep it short and filesystem-friendly.
    string(REPLACE "/" "_" _git_suffix "${_git_desc}")
    string(REPLACE "\\" "_" _git_suffix "${_git_suffix}")
    set(BALCOM_VERSION_SUFFIX "${_git_suffix}")
  endif()
endif()

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
  firmware/firmware_upgrade.c
  device/device_discovery.c
  device/device_protocol.c
  device/device_status.c
  loopback/loopback.c
)

if (WIN32 AND EXISTS "${BALCOM_APP_ICON_ICO}" AND EXISTS "${BALCOM_APP_ICON_RC}")
  target_sources(BalCom1A_Configuration_Utility PRIVATE "${BALCOM_APP_ICON_RC}")
endif()

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
  firmware/firmware_upgrade.c
  device/device_discovery.c
  device/device_protocol.c
  device/device_status.c
  loopback/loopback.c
)

if (WIN32 AND EXISTS "${BALCOM_APP_ICON_ICO}" AND EXISTS "${BALCOM_APP_ICON_RC}")
  target_sources(BalCom1A_Configuration_Utility_console PRIVATE "${BALCOM_APP_ICON_RC}")
endif()

set_target_properties(BalCom1A_Configuration_Utility_console PROPERTIES OUTPUT_NAME "BalCom1A Configuration Utility Console")

target_link_libraries(BalCom1A_Configuration_Utility PRIVATE PkgConfig::GTK4 platform)
target_link_libraries(BalCom1A_Configuration_Utility_console PRIVATE PkgConfig::GTK4 platform)

if (BALCOM_BUNDLE_FLASH_TOOL)
  if (BALCOM_FLASH_TOOL_EXECUTABLE STREQUAL "")
    find_program(BALCOM_FLASH_TOOL_EXECUTABLE NAMES esptool esptool.exe balcom-esptool balcom-esptool.exe)
  endif()

  if (NOT BALCOM_FLASH_TOOL_EXECUTABLE)
    message(FATAL_ERROR "BALCOM_BUNDLE_FLASH_TOOL is ON, but BALCOM_FLASH_TOOL_EXECUTABLE was not found. Set -DBALCOM_FLASH_TOOL_EXECUTABLE=... to a flasher executable.")
  endif()

  if (NOT EXISTS "${BALCOM_FLASH_TOOL_EXECUTABLE}")
    message(FATAL_ERROR "BALCOM_BUNDLE_FLASH_TOOL is ON, but BALCOM_FLASH_TOOL_EXECUTABLE does not exist: ${BALCOM_FLASH_TOOL_EXECUTABLE}")
  endif()

  # Copy next to binaries for local runs from build dir.
  add_custom_command(TARGET BalCom1A_Configuration_Utility POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BALCOM_FLASH_TOOL_EXECUTABLE}"
            $<TARGET_FILE_DIR:BalCom1A_Configuration_Utility>/$<IF:$<BOOL:WIN32>,esptool.exe,esptool>
    COMMENT "Copy flashing tool next to executable"
  )

  add_custom_command(TARGET BalCom1A_Configuration_Utility_console POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BALCOM_FLASH_TOOL_EXECUTABLE}"
            $<TARGET_FILE_DIR:BalCom1A_Configuration_Utility_console>/$<IF:$<BOOL:WIN32>,esptool.exe,esptool>
    COMMENT "Copy flashing tool next to executable (console target)"
  )
endif()

# Linux packaging: allow looking up UI file in the installed data dir.
if (UNIX AND NOT APPLE)
  target_compile_definitions(BalCom1A_Configuration_Utility PRIVATE
    BALCOM_UI_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/balcom1a-configuration-utility"
  )
  target_compile_definitions(BalCom1A_Configuration_Utility_console PRIVATE
    BALCOM_UI_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/balcom1a-configuration-utility"
  )
endif()

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

# -----------------------------
# Install + packaging (CPack)
# -----------------------------

# -----------------------------
# Linux desktop integration (.desktop + icon)
# -----------------------------
if (UNIX AND NOT APPLE)
  # Install icon (we have .ico; desktop files support absolute icon paths).
  set(BALCOM_LINUX_ICON_BASENAME "balcom1a-configuration-utility")
  set(BALCOM_LINUX_ICON_INSTALL_NAME "${BALCOM_LINUX_ICON_BASENAME}.ico")
  set(BALCOM_LINUX_ICON_ABS "${CMAKE_INSTALL_FULL_DATAROOTDIR}/pixmaps/${BALCOM_LINUX_ICON_INSTALL_NAME}")

  # Install a wrapper command without spaces so .desktop Exec is robust.
  # Expose it to users as "b1a-config" (also used as DEB package name).
  set(BALCOM_LINUX_WRAPPER_NAME "b1a-config")
  set(_balcom_wrapper_in "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/balcom1a-wifi-config.sh.in")
  set(_balcom_wrapper_out "${CMAKE_CURRENT_BINARY_DIR}/${BALCOM_LINUX_WRAPPER_NAME}")
  configure_file("${_balcom_wrapper_in}" "${_balcom_wrapper_out}" @ONLY)

  # Configure desktop entry.
  set(BALCOM_DESKTOP_EXEC "${BALCOM_LINUX_WRAPPER_NAME}")
  # Use the icon "theme name" (without path/extension) so it stays valid
  # even when CPack relocates the install prefix (e.g. in DEB packages).
  set(BALCOM_DESKTOP_ICON "${BALCOM_LINUX_ICON_BASENAME}")
  set(_balcom_desktop_in "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/balcom1a-wifi-configuration-utility.desktop.in")
  set(_balcom_desktop_out "${CMAKE_CURRENT_BINARY_DIR}/balcom1a-wifi-configuration-utility.desktop")
  configure_file("${_balcom_desktop_in}" "${_balcom_desktop_out}" @ONLY)
endif()

if (WIN32)
  install(TARGETS BalCom1A_Configuration_Utility
    RUNTIME DESTINATION .
  )

  # Optional console variant (installed too; useful for field debugging).
  install(TARGETS BalCom1A_Configuration_Utility_console
    RUNTIME DESTINATION .
  )

  # UI definition must be next to the executable at runtime.
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ui/glade.ui
    DESTINATION .
    RENAME glade.ui
  )

  if (BALCOM_BUNDLE_FLASH_TOOL)
    get_filename_component(_balcom_flasher_dir "${BALCOM_FLASH_TOOL_EXECUTABLE}" DIRECTORY)

    install(PROGRAMS "${BALCOM_FLASH_TOOL_EXECUTABLE}"
      DESTINATION .
      RENAME esptool.exe
    )

    # PyInstaller bootloaders are commonly linked against MSVC runtime.
    # If these DLLs are present next to the built esptool.exe (CI can provide them),
    # ship them alongside so esptool.exe runs on clean Windows machines.
    foreach(_balcom_vc_dll IN ITEMS vcruntime140.dll vcruntime140_1.dll msvcp140.dll)
      if (EXISTS "${_balcom_flasher_dir}/${_balcom_vc_dll}")
        install(FILES "${_balcom_flasher_dir}/${_balcom_vc_dll}" DESTINATION .)
      endif()
    endforeach()

    if (EXISTS "${BALCOM_FLASH_TOOL_NOTICES}")
      install(FILES "${BALCOM_FLASH_TOOL_NOTICES}"
        DESTINATION .
        RENAME THIRD_PARTY_NOTICES.txt
      )
    endif()
  endif()

  # If the installer will download the flash tool, still ship notices.
  if (BALCOM_INSTALLER_DOWNLOAD_FLASH_TOOL)
    if (EXISTS "${BALCOM_FLASH_TOOL_NOTICES}")
      install(FILES "${BALCOM_FLASH_TOOL_NOTICES}"
        DESTINATION .
        RENAME THIRD_PARTY_NOTICES.txt
      )
    endif()
  endif()
else()
  # Linux/Unix: use standard filesystem layout.
  install(TARGETS BalCom1A_Configuration_Utility
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  install(TARGETS BalCom1A_Configuration_Utility_console
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  # Install UI file to a data dir; the app will look it up via BALCOM_UI_DATADIR.
  if (BALCOM_BUNDLE_FLASH_TOOL)
    install(PROGRAMS "${BALCOM_FLASH_TOOL_EXECUTABLE}"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/balcom1a-configuration-utility
      RENAME esptool
    )

    if (EXISTS "${BALCOM_FLASH_TOOL_NOTICES}")
      install(FILES "${BALCOM_FLASH_TOOL_NOTICES}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/balcom1a-configuration-utility
        RENAME THIRD_PARTY_NOTICES.txt
      )
    endif()
  endif()
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ui/glade.ui
    DESTINATION ${CMAKE_INSTALL_DATADIR}/balcom1a-configuration-utility
    RENAME glade.ui
  )

  # Desktop integration
  if (EXISTS "${BALCOM_APP_ICON_ICO}")
    install(FILES "${BALCOM_APP_ICON_ICO}"
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
      RENAME "${BALCOM_LINUX_ICON_INSTALL_NAME}"
    )
  endif()

  if (EXISTS "${_balcom_desktop_out}")
    install(FILES "${_balcom_desktop_out}"
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )
  endif()

  if (EXISTS "${_balcom_wrapper_out}")
    install(PROGRAMS "${_balcom_wrapper_out}"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif()

# On Windows, also install the .ico file so shortcuts can explicitly reference it
# (more reliable than depending on Explorer icon cache of the .exe).
if (WIN32 AND EXISTS "${BALCOM_APP_ICON_ICO}")
  install(FILES "${BALCOM_APP_ICON_ICO}"
    DESTINATION .
    RENAME "BalCom1A Configuration Utility.ico"
  )
endif()

option(BALCOM_BUNDLE_GTK_RUNTIME "Bundle GTK/GLib runtime into the installer (Windows/MSYS2)" ON)

# MSYS2 MinGW64 prefix used to locate gtk runtime tools/data while packaging.
set(MSYS2_MINGW64_PREFIX "C:/msys64/mingw64" CACHE PATH "MSYS2 MinGW64 prefix (contains bin/, lib/, share/)")

if (WIN32 AND BALCOM_BUNDLE_GTK_RUNTIME)
  install(CODE
    "set(MSYS2_MINGW64_PREFIX \"${MSYS2_MINGW64_PREFIX}\")\ninclude(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_gtk_runtime.cmake\")"
  )
endif()

set(CPACK_PACKAGE_NAME "BalCom1A Configuration Utility")
set(CPACK_PACKAGE_VENDOR "BalCom")
set(CPACK_PACKAGE_VERSION "${BALCOM_VERSION_NUM}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "BalCom1A Configuration Utility")
set(CPACK_VERBATIM_VARIABLES TRUE)

# Always write the produced installer into this build directory (even if cpack
# is invoked from another working directory).
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Include git/date suffix in the produced installer file name.
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${BALCOM_VERSION_SUFFIX}-${BALCOM_SYSTEM_TAG}")

if (WIN32)
  set(CPACK_GENERATOR "NSIS")
  # NSIS-specific tweaks
  set(CPACK_NSIS_DISPLAY_NAME "BalCom1A Configuration Utility")
  set(CPACK_NSIS_PACKAGE_NAME "BalCom1A Configuration Utility")
  set(CPACK_NSIS_MODIFY_PATH OFF)

  # When a previous version is installed, prompt to uninstall it first.
  # This helps ensure Start Menu/Desktop shortcuts get refreshed.
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

  # Installer + installed app icon
  if (EXISTS "${BALCOM_APP_ICON_ICO}")
    set(CPACK_NSIS_MUI_ICON "${BALCOM_APP_ICON_ICO}")
    set(CPACK_NSIS_MUI_UNIICON "${BALCOM_APP_ICON_ICO}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "BalCom1A Configuration Utility.exe")
  else()
    message(WARNING "App icon not found: ${BALCOM_APP_ICON_ICO}")
  endif()

  # Try to find NSIS (makensis) even if it's not on PATH.
  find_program(NSIS_MAKENSIS_EXECUTABLE
    NAMES makensis makensis.exe
    HINTS
      "C:/Program Files (x86)/NSIS" "C:/Program Files (x86)/NSIS/Bin"
      "C:/Program Files/NSIS" "C:/Program Files/NSIS/Bin"
      "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramFiles}/NSIS/Bin"
  )
  if (NSIS_MAKENSIS_EXECUTABLE)
    set(CPACK_NSIS_EXECUTABLE "${NSIS_MAKENSIS_EXECUTABLE}")
    message(STATUS "NSIS found: ${NSIS_MAKENSIS_EXECUTABLE}")
  else()
    message(WARNING "NSIS (makensis) not found. Install NSIS or add it to PATH to build the installer.")
  endif()

  # Start Menu + Desktop shortcuts
  # - Start Menu entries are created from CPACK_PACKAGE_EXECUTABLES
  # - Desktop links require corresponding Start Menu shortcut names.
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_PACKAGE_EXECUTABLES
    "BalCom1A Configuration Utility" "BalCom1A Configuration Utility"
  )
  set(CPACK_CREATE_DESKTOP_LINKS
    "BalCom1A Configuration Utility"
  )

  # Recreate shortcuts explicitly using our .ico to avoid stale icon caches.
  # (CPack also creates shortcuts; we delete/recreate to force refresh.)
  set(CPACK_NSIS_CREATE_ICONS_EXTRA [=[
    Delete "$SMPROGRAMS\$STARTMENU_FOLDER\BalCom1A Configuration Utility.lnk"
    Delete "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk"
    Delete "$DESKTOP\BalCom1A Configuration Utility.lnk"

    ; If the InstallOptions page was skipped (e.g. silent install), default to
    ; creating the Desktop shortcut.
    StrCmp "$INSTALL_DESKTOP" "" 0 +2
      StrCpy $INSTALL_DESKTOP "1"

    CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\BalCom1A Configuration Utility.lnk" "$INSTDIR\.\BalCom1A Configuration Utility.exe" "" "$INSTDIR\BalCom1A Configuration Utility.ico" 0
    StrCmp "$INSTALL_DESKTOP" "1" 0 +2
      CreateShortCut "$DESKTOP\BalCom1A Configuration Utility.lnk" "$INSTDIR\.\BalCom1A Configuration Utility.exe" "" "$INSTDIR\BalCom1A Configuration Utility.ico" 0

    CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
  ]=])

  set(CPACK_NSIS_DELETE_ICONS_EXTRA [=[
    Delete "$SMPROGRAMS\$STARTMENU_FOLDER\BalCom1A Configuration Utility.lnk"
    Delete "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk"
    Delete "$DESKTOP\BalCom1A Configuration Utility.lnk"
  ]=])

  # Default the "Create Desktop Icon" checkbox to ON.
  # CPack's NSIS template uses NSIS.InstallOptions.ini Field 5 for desktop icon.
  set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE [=[
!define MUI_CUSTOMFUNCTION_GUIINIT BalComGUIInit
Function BalComGUIInit
  WriteINIStr "$PLUGINSDIR/NSIS.InstallOptions.ini" "Field 5" "State" "1"
  ; Some Windows themes/scaling result in a smaller InstallOptions client area.
  ; Move the Desktop Icon checkbox upward so it remains visible.
  WriteINIStr "$PLUGINSDIR/NSIS.InstallOptions.ini" "Field 5" "Top" "65"
  WriteINIStr "$PLUGINSDIR/NSIS.InstallOptions.ini" "Field 5" "Bottom" "75"
FunctionEnd
]=])

  option(BALCOM_ENABLE_INSTALLER "Enable NSIS installer target (make_installer)" ON)
  if (BALCOM_ENABLE_INSTALLER)
    if (BALCOM_INSTALLER_DOWNLOAD_FLASH_TOOL)
      if (BALCOM_FLASH_TOOL_DOWNLOAD_URL STREQUAL "")
        message(FATAL_ERROR "BALCOM_INSTALLER_DOWNLOAD_FLASH_TOOL is ON, but BALCOM_FLASH_TOOL_DOWNLOAD_URL is empty")
      endif()

      # Download esptool.exe at install time if it isn't already present.
      # Robustness rules:
      # - never delete a bundled/existing esptool.exe on failures
      # - download into $PLUGINSDIR first, then copy into $INSTDIR only on success
      # Uses NSISdl (bundled with NSIS). HTTPS is supported via WinINet.
      string(JOIN "\n" CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "        ; --- BalCom: ensure esptool.exe is present ---"
        "        StrCpy $R9 \"$INSTDIR\\esptool.exe\""
        "        StrCpy $R8 \"$PLUGINSDIR\\balcom_esptool_download.exe\""
        "        IfFileExists \"$R9\" BalCom_Esptool_Have BalCom_Esptool_Need"
        "        BalCom_Esptool_Need:"
        "          DetailPrint \"Downloading esptool.exe ...\""
        "          Delete \"$R8\""
        "          NSISdl::download /TIMEOUT=30000 \"${BALCOM_FLASH_TOOL_DOWNLOAD_URL}\" \"$R8\""
        "          Pop $0"
        "          StrCmp $0 \"success\" 0 BalCom_Esptool_DownloadFailed"
        "          CopyFiles /SILENT \"$R8\" \"$R9\""
        "          Goto BalCom_Esptool_Verify"
        "        BalCom_Esptool_DownloadFailed:"
        "          MessageBox MB_ICONEXCLAMATION \"Failed to download esptool.exe ($0).\\r\\n\\r\\nInstallation will continue, but Firmware Upgrade may not work until you place esptool.exe into:\\r\\n$INSTDIR\""
        "          Goto BalCom_Esptool_Done"
        "        BalCom_Esptool_Have:"
        "          ; esptool.exe already present (bundled or from a previous install)."
        "        BalCom_Esptool_Verify:"
      )

      if (NOT BALCOM_FLASH_TOOL_SHA256 STREQUAL "")
        # Append SHA256 verification. We compute actual hash via PowerShell and compare.
        # On mismatch, warn but DO NOT delete esptool.exe (to avoid breaking bundled installs).
        string(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "\n")
        string(JOIN "\n" _balcom_sha_verify
          "        ; --- BalCom: verify SHA256 of esptool.exe (best-effort) ---"
          "        IfFileExists \"$R9\" 0 BalCom_Esptool_Done"
          "          DetailPrint \"Verifying esptool.exe SHA256 ...\""
          "          StrCpy $1 \"$PLUGINSDIR\\balcom_esptool_sha256.txt\""
          "          Delete $1"
          "          nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -Command \"$h=(Get-FileHash -Algorithm SHA256 -LiteralPath ''$R9'').Hash.ToLower(); Set-Content -NoNewline -LiteralPath ''$PLUGINSDIR\\balcom_esptool_sha256.txt'' -Value $h\"'"
          "          IfFileExists \"$PLUGINSDIR\\balcom_esptool_sha256.txt\" 0 BalCom_Esptool_Done"
          "            FileOpen $2 \"$PLUGINSDIR\\balcom_esptool_sha256.txt\" r"
          "            FileRead $2 $3"
          "            FileClose $2"
          "            ; $3 may contain trailing CR/LF; compare only first 64 chars"
          "            StrCpy $3 $3 64"
          "            StrCmp $3 \"${BALCOM_FLASH_TOOL_SHA256}\" BalCom_Esptool_Done 0"
          "              MessageBox MB_ICONEXCLAMATION \"esptool.exe failed SHA256 verification.\\r\\n\\r\\nInstallation will continue, but Firmware Upgrade may not work.\""
        )
        string(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "${_balcom_sha_verify}")
      endif()

      string(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "\n        BalCom_Esptool_Done:")
    endif()

    # Older builds produced a legacy filename without the git/date suffix. Remove
    # it so users don't accidentally pick the wrong installer.
    set(BALCOM_LEGACY_INSTALLER_FILE
      "${CMAKE_CURRENT_BINARY_DIR}/${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${BALCOM_SYSTEM_TAG}.exe"
    )

    add_custom_target(make_installer
      COMMAND ${CMAKE_COMMAND} -E rm -f "${BALCOM_LEGACY_INSTALLER_FILE}"
      COMMAND ${CMAKE_CPACK_COMMAND} -G NSIS --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
      DEPENDS BalCom1A_Configuration_Utility BalCom1A_Configuration_Utility_console
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Make Installer (CPack + NSIS)"
      VERBATIM
    )
  endif()
endif()

if (UNIX AND NOT APPLE)
  # Debian package
  set(CPACK_GENERATOR "DEB")
  # Debian policy: /usr, not /usr/local
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  # Package is installed/removed as "b1a-config" (e.g. `apt install b1a-config`).
  set(CPACK_DEBIAN_PACKAGE_NAME "b1a-config")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "BalCom")
  # Let dpkg-shlibdeps compute dependencies from linked libs (recommended).
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  option(BALCOM_ENABLE_DEB "Enable DEB package target (make_deb)" ON)
  if (BALCOM_ENABLE_DEB)
    add_custom_target(make_deb
      COMMAND ${CMAKE_CPACK_COMMAND} -G DEB --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
      DEPENDS BalCom1A_Configuration_Utility BalCom1A_Configuration_Utility_console
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Make DEB package (CPack + DEB)"
      VERBATIM
    )
  endif()
endif()

include(CPack)