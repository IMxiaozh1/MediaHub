# MediaHub third-party notices

This Release package dynamically links to the following third-party software:

- Qt 5.14.2 (`Qt5Core`, `Qt5Gui`, `Qt5Network`, `Qt5Svg`, and `Qt5Widgets`),
  distributed under the GNU Lesser General Public License version 3 or the
  alternative terms offered by The Qt Company.
- libVLC 3.0.21 and the plugins copied from the official VLC 3.0.21 win64
  archive. libVLC is available under LGPL 2.1 or later. Some VLC plugins are
  available under GPL 2 or later and require a separate review before public
  binary distribution.

The source tree also includes Microsoft WebView2 SDK 1.0.4129.50 as the fixed
build input for the v0.6 browser integration. MediaHub statically links the x64
WebView2 Loader from that SDK; no `WebView2Loader.dll` is bundled. The SDK
license and accompanying notices are included in this package.

The corresponding license texts are included in this directory:

- `Qt-LGPL-3.0.txt`
- `Qt-GPL-3.0.txt`
- `VLC-COPYING.txt`
- `WebView2-LICENSE.txt`
- `WebView2-NOTICE.txt`

The Microsoft Visual C++ 2015-2022 Redistributable (x64) is a runtime
prerequisite. Its installer is not bundled in this package.

Microsoft Edge WebView2 Evergreen Runtime is also an external runtime
prerequisite for the browser integration. It is installed and serviced
independently by Microsoft and is not stored in this repository or bundled by
this dependency stage.

MediaHub uses dynamic linking. Users may replace the Qt and VLC runtime files
with compatible builds bearing the same filenames. The MediaHub source code
license has not yet been selected, so this package is for local validation and
must not be treated as authorization for public redistribution.
