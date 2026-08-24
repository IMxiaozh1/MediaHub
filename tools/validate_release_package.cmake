cmake_minimum_required(VERSION 3.20)

function(normalize_package_path inputVariable outputVariable)
    set(normalizedPath "")
    if(DEFINED ${inputVariable} AND NOT "${${inputVariable}}" STREQUAL "")
        cmake_path(ABSOLUTE_PATH ${inputVariable} NORMALIZE
                   OUTPUT_VARIABLE normalizedPath)
        cmake_path(GET normalizedPath ROOT_PATH normalizedRoot)
        if("${normalizedPath}/" STREQUAL "${normalizedRoot}")
            set(normalizedPath "${normalizedRoot}")
        elseif(NOT normalizedPath STREQUAL "${normalizedRoot}")
            string(REGEX REPLACE "/+$" "" normalizedPath "${normalizedPath}")
        endif()
    endif()
    set(${outputVariable} "${normalizedPath}" PARENT_SCOPE)
endfunction()

normalize_package_path(MEDIAHUB_PACKAGE_DIR mediahubPrimaryPackageDir)
normalize_package_path(PACKAGE_ROOT mediahubAliasPackageDir)

if(mediahubPrimaryPackageDir STREQUAL "" AND mediahubAliasPackageDir STREQUAL "")
    message(FATAL_ERROR
        "Set MEDIAHUB_PACKAGE_DIR or PACKAGE_ROOT to the Release package directory.")
endif()

if(NOT mediahubPrimaryPackageDir STREQUAL ""
   AND NOT mediahubAliasPackageDir STREQUAL "")
    set(mediahubPrimaryPackageDirForCompare "${mediahubPrimaryPackageDir}")
    set(mediahubAliasPackageDirForCompare "${mediahubAliasPackageDir}")
    if(NOT mediahubPrimaryPackageDirForCompare MATCHES "/$")
        string(APPEND mediahubPrimaryPackageDirForCompare "/")
    endif()
    if(NOT mediahubAliasPackageDirForCompare MATCHES "/$")
        string(APPEND mediahubAliasPackageDirForCompare "/")
    endif()
    if(WIN32)
        string(TOLOWER "${mediahubPrimaryPackageDirForCompare}"
               mediahubPrimaryPackageDirForCompare)
        string(TOLOWER "${mediahubAliasPackageDirForCompare}"
               mediahubAliasPackageDirForCompare)
    endif()
    if(NOT mediahubPrimaryPackageDirForCompare STREQUAL
       mediahubAliasPackageDirForCompare)
        message(FATAL_ERROR
            "MEDIAHUB_PACKAGE_DIR and PACKAGE_ROOT identify different directories.")
    endif()
endif()

if(NOT mediahubPrimaryPackageDir STREQUAL "")
    set(mediahubPackageDir "${mediahubPrimaryPackageDir}")
else()
    set(mediahubPackageDir "${mediahubAliasPackageDir}")
endif()

if(NOT IS_DIRECTORY "${mediahubPackageDir}")
    message(FATAL_ERROR "Release package directory does not exist: ${mediahubPackageDir}")
endif()

file(GLOB_RECURSE mediahubPackageEntries
     LIST_DIRECTORIES TRUE
     RELATIVE "${mediahubPackageDir}"
     "${mediahubPackageDir}/*")
set(mediahubLinkEntries)
foreach(packageEntry IN LISTS mediahubPackageEntries)
    if(IS_SYMLINK "${mediahubPackageDir}/${packageEntry}")
        list(APPEND mediahubLinkEntries "${packageEntry}")
    endif()
endforeach()
if(mediahubLinkEntries)
    list(JOIN mediahubLinkEntries "\n  " mediahubLinkSummary)
    message(FATAL_ERROR
        "Release package contains links:\n  ${mediahubLinkSummary}")
endif()

set(mediahubRequiredFiles
    MediaHub.exe
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Network.dll
    Qt6Svg.dll
    Qt6Widgets.dll
    d3dcompiler_47.dll
    dxcompiler.dll
    dxil.dll
    libvlc.dll
    libvlccore.dll
    opengl32sw.dll
    icons/taskbar.png
    icons/window.jpg
    iconengines/qsvgicon.dll
    imageformats/qjpeg.dll
    networkinformation/qnetworklistmanager.dll
    platforms/qwindows.dll
    styles/qmodernwindowsstyle.dll
    tls/qschannelbackend.dll
    licenses/Qt-LGPL-3.0.txt
    licenses/Qt-GPL-3.0.txt
    licenses/VLC-COPYING.txt
    licenses/WebView2-LICENSE.txt
    licenses/WebView2-NOTICE.txt
    licenses/THIRD-PARTY-NOTICES.md
)
foreach(requiredFile IN LISTS mediahubRequiredFiles)
    set(requiredPath "${mediahubPackageDir}/${requiredFile}")
    if(NOT EXISTS "${requiredPath}"
       OR IS_DIRECTORY "${requiredPath}"
       OR IS_SYMLINK "${requiredPath}")
        message(FATAL_ERROR
            "Release package requires a regular file at ${requiredFile}.")
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

set(mediahubForbiddenFiles)
string(CONCAT mediahubBrowserDataPattern
    "(^|/)(profile[^/]*|cache([._-][^/]*)?|code cache|gpucache|"
    "shadercache|dawnwebgpucache|dawngraphitecache|grshadercache|"
    "cookies?([._-][^/]*)?)($|/)")
string(CONCAT mediahubQtDebugDllPattern
    "^(qt6(core|gui|network|svg|test|widgets)|"
    "q(certonlybackend|gif|ico|jpeg|modernwindowsstyle|networklistmanager|"
    "offscreen|schannelbackend|svg|svgicon|tuiotouchplugin|windows))d\\.dll$")
foreach(packageEntry IN LISTS mediahubPackageEntries)
    string(TOLOWER "${packageEntry}" packageEntryLower)
    get_filename_component(packageEntryName "${packageEntryLower}" NAME)

    if(packageEntryLower MATCHES "${mediahubBrowserDataPattern}"
       OR packageEntryLower MATCHES "(^|/)qt5[^/]*\\.dll$"
       OR packageEntryLower MATCHES "(^|/)webview2loader\\.dll$"
       OR packageEntryLower MATCHES "(^|/)(x86|arm64)/.*webview2loader"
       OR packageEntryName MATCHES "\\.(lib|obj|pdb)$"
       OR packageEntryName MATCHES "${mediahubQtDebugDllPattern}"
       OR packageEntryName STREQUAL "cmakecache.txt"
       OR packageEntryName STREQUAL "build.ninja"
       OR packageEntryName MATCHES "^vc_redist.*\\.exe$"
       OR (packageEntryLower MATCHES "\\.exe$"
           AND NOT packageEntryLower STREQUAL "mediahub.exe")
       OR packageEntryName MATCHES "test.*\\.(exe|txt)$"
       OR packageEntryName MATCHES ".*test.*\\.(exe|txt)$")
        list(APPEND mediahubForbiddenFiles "${packageEntry}")
    endif()
endforeach()
if(mediahubForbiddenFiles)
    list(JOIN mediahubForbiddenFiles "\n  " mediahubForbiddenSummary)
    message(FATAL_ERROR
        "Release package contains links, browser data, a dynamic WebView2 Loader, "
        "or build/test/Debug files:\n  ${mediahubForbiddenSummary}")
endif()

message(STATUS "MediaHub Release package validated: "
               "${mediahubVlcPluginCount} VLC plugins, no links, browser data, or "
               "build/test/Debug files.")
