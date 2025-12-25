# Bundle GTK4/GLib runtime for a MinGW-built GTK application on Windows.
# This script is executed at install time (via install(CODE ... include())).
# It copies dependent DLLs and required GTK runtime data into the install prefix.

if (NOT WIN32)
  return()
endif()

if (NOT DEFINED MSYS2_MINGW64_PREFIX)
  set(MSYS2_MINGW64_PREFIX "C:/msys64/mingw64")
endif()

# Fail hard if MSYS2 runtime with GTK4 is not present on the build machine.
set(_gtk_check_dll "${MSYS2_MINGW64_PREFIX}/bin/libgtk-4-1.dll")
if (NOT EXISTS "${_gtk_check_dll}")
  message(FATAL_ERROR
    "Bundling GTK runtime failed: expected GTK4 runtime under "
    "${MSYS2_MINGW64_PREFIX} (missing ${_gtk_check_dll}). "
    "Install MSYS2 with mingw-w64-x86_64-gtk4 into this prefix, or set MSYS2_MINGW64_PREFIX accordingly before building the installer.")
endif()

set(_dest "${CMAKE_INSTALL_PREFIX}")

# Main EXE name is set via OUTPUT_NAME in CMake.
set(_app_exe "${_dest}/BalCom1A Configuration Utility.exe")
set(_console_exe "${_dest}/BalCom1A Configuration Utility Console.exe")

if (NOT EXISTS "${_app_exe}")
  message(FATAL_ERROR "Bundling GTK runtime failed: expected ${_app_exe} to exist. Did install() place the EXE in the install prefix?")
endif()

# 1) Copy DLL dependencies for the application.
# Exclude system DLLs; keep MSYS2/MinGW runtime + GTK DLLs.
set(_resolved "")
set(_unresolved "")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES
    "${_app_exe}"
    "${_console_exe}"
  DIRECTORIES
    "${MSYS2_MINGW64_PREFIX}/bin"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  PRE_EXCLUDE_REGEXES
    "api-ms-win-.*"
    "ext-ms-.*"
  POST_EXCLUDE_REGEXES
    ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
    ".*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
    ".*[/\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\].*"
)

if (_unresolved)
  message(WARNING "Unresolved runtime deps: ${_unresolved}")
endif()

foreach(_dll IN LISTS _resolved)
  # Place all DLLs next to the EXE.
  file(INSTALL DESTINATION "${_dest}" TYPE FILE FILES "${_dll}")
endforeach()

# 2) Copy GTK/GLib runtime data files.
# Schemas (GSettings)
set(_schemas_src "${MSYS2_MINGW64_PREFIX}/share/glib-2.0/schemas")
set(_schemas_dst "${_dest}/share/glib-2.0/schemas")
if (NOT EXISTS "${_schemas_src}")
  message(FATAL_ERROR "Schemas dir not found: ${_schemas_src} (is glib2 installed in MSYS2?)")
endif()

file(INSTALL DESTINATION "${_dest}/share/glib-2.0" TYPE DIRECTORY FILES "${_schemas_src}")

set(_glib_compile_schemas "${MSYS2_MINGW64_PREFIX}/bin/glib-compile-schemas.exe")
if (NOT EXISTS "${_glib_compile_schemas}")
  message(FATAL_ERROR "glib-compile-schemas.exe not found in ${MSYS2_MINGW64_PREFIX}/bin; cannot bundle working GSettings schemas")
endif()

execute_process(
  COMMAND "${_glib_compile_schemas}" "${_schemas_dst}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)
if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "glib-compile-schemas failed (${_rc}): ${_err}")
endif()

if (NOT EXISTS "${_schemas_dst}/gschemas.compiled")
  message(FATAL_ERROR "Expected gschemas.compiled to be created at ${_schemas_dst}/gschemas.compiled")
endif()

# Icons (theme).
# Copy only the themes we actually rely on (Adwaita + hicolor). Copying the
# entire share/icons tree can produce a very large NSIS installer.
set(_icons_root "${MSYS2_MINGW64_PREFIX}/share/icons")
set(_icons_adwaita "${_icons_root}/Adwaita")
set(_icons_hicolor "${_icons_root}/hicolor")

# MSYS2 packages sometimes contain symlinks inside icon themes. During CPack/NSIS
# packaging this may result in "failed opening file" errors if a symlink target
# is missing or cannot be materialized in the staging tree. To make packaging
# robust, install only files that actually exist on disk.
function(_balcom_install_tree_files src_root dst_root)
  file(GLOB_RECURSE _entries RELATIVE "${src_root}" "${src_root}/*")
  foreach(_rel IN LISTS _entries)
    set(_abs "${src_root}/${_rel}")
    if (IS_DIRECTORY "${_abs}")
      continue()
    endif()
    if (NOT EXISTS "${_abs}")
      continue()
    endif()
    get_filename_component(_rel_dir "${_rel}" DIRECTORY)
    if (_rel_dir STREQUAL "")
      file(INSTALL DESTINATION "${dst_root}" TYPE FILE FILES "${_abs}")
    else()
      file(INSTALL DESTINATION "${dst_root}/${_rel_dir}" TYPE FILE FILES "${_abs}")
    endif()
  endforeach()
endfunction()

if (NOT EXISTS "${_icons_adwaita}")
  message(FATAL_ERROR "Adwaita icon theme not found: ${_icons_adwaita} (install mingw-w64-x86_64-adwaita-icon-theme)")
endif()
if (NOT EXISTS "${_icons_hicolor}")
  message(FATAL_ERROR "hicolor icon theme not found: ${_icons_hicolor} (install mingw-w64-x86_64-hicolor-icon-theme)")
endif()

_balcom_install_tree_files("${_icons_adwaita}" "${_dest}/share/icons/Adwaita")
_balcom_install_tree_files("${_icons_hicolor}" "${_dest}/share/icons/hicolor")

# GTK data (optional but common).
set(_gtk_share_src "${MSYS2_MINGW64_PREFIX}/share/gtk-4.0")
if (EXISTS "${_gtk_share_src}")
  file(COPY "${_gtk_share_src}" DESTINATION "${_dest}/share")
endif()

# GTK modules (optional, but commonly required for full GTK functionality).
set(_gtk_lib_src "${MSYS2_MINGW64_PREFIX}/lib/gtk-4.0")
if (EXISTS "${_gtk_lib_src}")
  file(COPY "${_gtk_lib_src}" DESTINATION "${_dest}/lib")
endif()

# gdk-pixbuf loaders are required for icons/images. Copy full tree and regenerate loaders.cache.
set(_gdkpixbuf_src "${MSYS2_MINGW64_PREFIX}/lib/gdk-pixbuf-2.0")
if (NOT EXISTS "${_gdkpixbuf_src}")
  message(FATAL_ERROR "gdk-pixbuf dir not found: ${_gdkpixbuf_src} (is gdk-pixbuf installed in MSYS2?)")
endif()

file(COPY "${_gdkpixbuf_src}" DESTINATION "${_dest}/lib")

# Determine installed version dir (e.g. 2.10.0)
file(GLOB _installed_gdk_versions LIST_DIRECTORIES true "${_dest}/lib/gdk-pixbuf-2.0/*")
set(_installed_ver "")
foreach(_p IN LISTS _installed_gdk_versions)
  if (IS_DIRECTORY "${_p}/loaders")
    get_filename_component(_installed_ver "${_p}" NAME)
    break()
  endif()
endforeach()

if (NOT _installed_ver)
  message(FATAL_ERROR "Failed to locate installed gdk-pixbuf loaders dir under ${_dest}/lib/gdk-pixbuf-2.0")
endif()

set(_loaders_dst "${_dest}/lib/gdk-pixbuf-2.0/${_installed_ver}/loaders")
file(GLOB _loader_dlls "${_loaders_dst}/*.dll")
set(_query "${MSYS2_MINGW64_PREFIX}/bin/gdk-pixbuf-query-loaders.exe")
if (NOT EXISTS "${_query}")
  message(FATAL_ERROR "gdk-pixbuf-query-loaders.exe not found at ${_query}; cannot generate loaders.cache")
endif()
if (NOT _loader_dlls)
  message(FATAL_ERROR "No gdk-pixbuf loader DLLs found under ${_loaders_dst}")
endif()

execute_process(
  COMMAND "${_query}" ${_loader_dlls}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _cache
  ERROR_VARIABLE _err
)
if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "gdk-pixbuf-query-loaders failed (${_rc}): ${_err}")
endif()

file(WRITE "${_dest}/lib/gdk-pixbuf-2.0/${_installed_ver}/loaders.cache" "${_cache}")

message(STATUS "Bundled GTK runtime into: ${_dest}")
