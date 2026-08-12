if(NOT DEFINED MEDIAHUB_PACKAGE_DIR AND DEFINED PACKAGE_ROOT)
    set(MEDIAHUB_PACKAGE_DIR "${PACKAGE_ROOT}")
endif()

if(NOT DEFINED MEDIAHUB_PACKAGE_DIR)
    message(FATAL_ERROR "Set MEDIAHUB_PACKAGE_DIR to the Release package directory.")
endif()

cmake_path(ABSOLUTE_PATH MEDIAHUB_PACKAGE_DIR NORMALIZE
           OUTPUT_VARIABLE mediahubPackageDir)
if(NOT IS_DIRECTORY "${mediahubPackageDir}")
    message(FATAL_ERROR "Release package directory does not exist: ${mediahubPackageDir}")
endif()

set(mediahubRequiredFiles
    MediaHub.exe
    Qt5Core.dll
    Qt5Gui.dll
    Qt5Network.dll
    Qt5Svg.dll
    Qt5Widgets.dll
    libEGL.dll
    libGLESv2.dll
    libvlc.dll
    libvlccore.dll
    opengl32sw.dll
    icons/taskbar.png
    icons/window.jpg
    bearer/qgenericbearer.dll
    iconengines/qsvgicon.dll
    imageformats/qjpeg.dll
    platforms/qwindows.dll
    licenses/Qt-LGPL-3.0.txt
    licenses/Qt-GPL-3.0.txt
    licenses/VLC-COPYING.txt
    licenses/WebView2-LICENSE.txt
    licenses/WebView2-NOTICE.txt
    licenses/THIRD-PARTY-NOTICES.md
)
foreach(requiredFile IN LISTS mediahubRequiredFiles)
    if(NOT EXISTS "${mediahubPackageDir}/${requiredFile}")
        message(FATAL_ERROR "Release package is missing ${requiredFile}.")
    endif()
endforeach()

file(GLOB_RECURSE mediahubVlcPlugins LIST_DIRECTORIES FALSE
     "${mediahubPackageDir}/plugins/*.dll")
list(LENGTH mediahubVlcPlugins mediahubVlcPluginCount)
if(mediahubVlcPluginCount LESS 300)
    message(FATAL_ERROR
        "Release package contains only ${mediahubVlcPluginCount} VLC plugins; "
        "expected at least 300.")
endif()

file(GLOB_RECURSE mediahubPackageEntries
     LIST_DIRECTORIES TRUE
     RELATIVE "${mediahubPackageDir}"
     "${mediahubPackageDir}/*")
set(mediahubForbiddenFiles)
set(mediahubBrowserDataPattern
    "(^|/)(profile[^/]*|cache([._-][^/]*)?|code cache|gpucache|"
    "shadercache|cookies?([._-][^/]*)?)($|/)")
foreach(packageEntry IN LISTS mediahubPackageEntries)
    string(TOLOWER "${packageEntry}" packageEntryLower)
    get_filename_component(packageEntryName "${packageEntryLower}" NAME)

    if(packageEntryLower MATCHES "${mediahubBrowserDataPattern}"
       OR packageEntryLower MATCHES "(^|/)webview2loader\\.dll$"
       OR packageEntryLower MATCHES "(^|/)(x86|arm64)/.*webview2loader"
       OR packageEntryName MATCHES "\\.(lib|obj|pdb)$"
       OR packageEntryName MATCHES "d\\.dll$"
       OR packageEntryName STREQUAL "cmakecache.txt"
       OR packageEntryName STREQUAL "build.ninja"
       OR packageEntryName MATCHES "^vc_redist.*\\.exe$"
       OR (packageEntryName MATCHES "\\.exe$"
           AND NOT packageEntryName STREQUAL "mediahub.exe")
       OR packageEntryName MATCHES "test.*\\.(exe|txt)$"
       OR packageEntryName MATCHES ".*test.*\\.(exe|txt)$")
        list(APPEND mediahubForbiddenFiles "${packageEntry}")
    endif()
endforeach()
if(mediahubForbiddenFiles)
    list(JOIN mediahubForbiddenFiles "\n  " mediahubForbiddenSummary)
    message(FATAL_ERROR
        "Release package contains browser data, a dynamic WebView2 Loader, "
        "or build/test/Debug files:\n  ${mediahubForbiddenSummary}")
endif()

message(STATUS "MediaHub Release package validated: "
               "${mediahubVlcPluginCount} VLC plugins, no browser data or "
               "build/test/Debug files.")
