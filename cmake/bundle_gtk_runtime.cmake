# Bundle GTK4/GLib runtime for a MinGW-built GTK application on Windows.
# This script is executed at install time (via install(CODE ... include())).
# It copies dependent DLLs and required GTK runtime data into the install prefix.

if (NOT WIN32)
  return()
endif()

if (NOT DEFINED MSYS2_MINGW64_PREFIX)
  set(MSYS2_MINGW64_PREFIX "C:/msys64/mingw64")
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
if (EXISTS "${_schemas_src}")
  file(INSTALL DESTINATION "${_dest}/share/glib-2.0" TYPE DIRECTORY FILES "${_schemas_src}")

  set(_glib_compile_schemas "${MSYS2_MINGW64_PREFIX}/bin/glib-compile-schemas.exe")
  if (EXISTS "${_glib_compile_schemas}")
    execute_process(
      COMMAND "${_glib_compile_schemas}" "${_schemas_dst}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _out
      ERROR_VARIABLE _err
    )
    if (NOT _rc EQUAL 0)
      message(WARNING "glib-compile-schemas failed (${_rc}): ${_err}")
    endif()
  else()
    message(WARNING "glib-compile-schemas.exe not found in ${MSYS2_MINGW64_PREFIX}/bin; schemas may not work without gschemas.compiled")
  endif()
else()
  message(WARNING "Schemas dir not found: ${_schemas_src}")
endif()

# Icons (theme). This can be large, but is the most reliable way to avoid missing icons.
set(_icons_src "${MSYS2_MINGW64_PREFIX}/share/icons")
if (EXISTS "${_icons_src}")
  file(INSTALL DESTINATION "${_dest}/share" TYPE DIRECTORY FILES "${_icons_src}")
endif()

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
if (EXISTS "${_gdkpixbuf_src}")
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

  if (_installed_ver)
    set(_loaders_dst "${_dest}/lib/gdk-pixbuf-2.0/${_installed_ver}/loaders")
    file(GLOB _loader_dlls "${_loaders_dst}/*.dll")
    set(_query "${MSYS2_MINGW64_PREFIX}/bin/gdk-pixbuf-query-loaders.exe")
    if (EXISTS "${_query}" AND _loader_dlls)
      execute_process(
        COMMAND "${_query}" ${_loader_dlls}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _cache
        ERROR_VARIABLE _err
      )
      if (_rc EQUAL 0)
        file(WRITE "${_dest}/lib/gdk-pixbuf-2.0/${_installed_ver}/loaders.cache" "${_cache}")
      else()
        message(WARNING "gdk-pixbuf-query-loaders failed (${_rc}): ${_err}")
      endif()
    else()
      message(WARNING "gdk-pixbuf-query-loaders.exe not found or no loader DLLs; images may not load")
    endif()
  endif()
endif()

message(STATUS "Bundled GTK runtime into: ${_dest}")
