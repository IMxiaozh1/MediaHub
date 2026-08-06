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

file(GLOB_RECURSE mediahubForbiddenFiles LIST_DIRECTORIES FALSE
     "${mediahubPackageDir}/*.lib"
     "${mediahubPackageDir}/*.obj"
     "${mediahubPackageDir}/*.pdb"
     "${mediahubPackageDir}/*d.dll"
     "${mediahubPackageDir}/CMakeCache.txt"
     "${mediahubPackageDir}/build.ninja"
     "${mediahubPackageDir}/vc_redist*.exe"
     "${mediahubPackageDir}/*test*.exe"
     "${mediahubPackageDir}/*test*.txt")
if(mediahubForbiddenFiles)
    list(JOIN mediahubForbiddenFiles "\n  " mediahubForbiddenSummary)
    message(FATAL_ERROR
        "Release package contains build, test, or Debug files:\n  ${mediahubForbiddenSummary}")
endif()

message(STATUS "MediaHub Release package validated: "
               "${mediahubVlcPluginCount} VLC plugins, no build/test/Debug files.")
