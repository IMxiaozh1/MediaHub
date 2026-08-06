#include "window_icon_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QImageReader>
#include <QPixmap>
#include <QResource>
#include <QWidget>
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static void initializeMediaHubGuiResources() {
  static const bool isInitialized = [] {
    Q_INIT_RESOURCE(mediahub_gui);
    return true;
  }();
  static_cast<void>(isInitialized);
}

namespace mediahub::gui {
namespace {

constexpr auto kTaskbarIconFileName = "taskbar.png";
constexpr auto kWindowIconFileName = "window.jpg";
constexpr auto kEmbeddedTaskbarIcon = ":/mediahub/app_icon.png";
constexpr auto kEmbeddedWindowIcon = ":/mediahub/window_icon.jpg";

QImage readImage(const QString& path) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  return reader.read();
}

QImage readExternalOrEmbedded(const QString& externalPath,
                              const QString& embeddedPath) {
  QImage image = readImage(externalPath);
  if (!image.isNull()) {
    return image;
  }
  return readImage(embeddedPath);
}

#ifdef _WIN32
QImage squareIconImage(const QImage& source, const int width,
                       const int height) {
  if (source.isNull() || width <= 0 || height <= 0) {
    return {};
  }
  const QImage scaled = source.scaled(width, height, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
  const int left = std::max(0, (scaled.width() - width) / 2);
  const int top = std::max(0, (scaled.height() - height) / 2);
  return scaled.copy(left, top, width, height).convertToFormat(QImage::Format_ARGB32);
}

HICON createNativeIcon(const QImage& source, const int width, const int height) {
  const QImage image = squareIconImage(source, width, height);
  if (image.isNull()) {
    return nullptr;
  }

  BITMAPINFO bitmapInfo{};
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = width;
  bitmapInfo.bmiHeader.biHeight = -height;
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;

  void* colorBits = nullptr;
  const HDC screen = GetDC(nullptr);
  const HBITMAP colorBitmap = CreateDIBSection(
      screen, &bitmapInfo, DIB_RGB_COLORS, &colorBits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (colorBitmap == nullptr || colorBits == nullptr) {
    if (colorBitmap != nullptr) {
      DeleteObject(colorBitmap);
    }
    return nullptr;
  }

  for (int row = 0; row < height; ++row) {
    std::memcpy(static_cast<unsigned char*>(colorBits) + row * width * 4,
                image.constScanLine(row), static_cast<std::size_t>(width) * 4);
  }
  const int maskStride = ((width + 15) / 16) * 2;
  const std::vector<unsigned char> maskBits(
      static_cast<std::size_t>(maskStride * height), 0);
  const HBITMAP maskBitmap =
      CreateBitmap(width, height, 1, 1, maskBits.data());
  if (maskBitmap == nullptr) {
    DeleteObject(colorBitmap);
    return nullptr;
  }

  ICONINFO iconInfo{};
  iconInfo.fIcon = TRUE;
  iconInfo.hbmMask = maskBitmap;
  iconInfo.hbmColor = colorBitmap;
  const HICON icon = CreateIconIndirect(&iconInfo);
  DeleteObject(maskBitmap);
  DeleteObject(colorBitmap);
  return icon;
}
#endif

}  // namespace

struct WindowIconManager::NativeIcons {
#ifdef _WIN32
  HWND windowHandle{nullptr};
  HICON taskbarIcon{nullptr};
  HICON windowIcon{nullptr};
  HICON previousTaskbarIcon{nullptr};
  HICON previousWindowIcon{nullptr};

  ~NativeIcons() {
    if (windowHandle != nullptr && IsWindow(windowHandle)) {
      const auto currentTaskbarIcon = reinterpret_cast<HICON>(
          SendMessageW(windowHandle, WM_GETICON, ICON_BIG, 0));
      const auto currentWindowIcon = reinterpret_cast<HICON>(
          SendMessageW(windowHandle, WM_GETICON, ICON_SMALL, 0));
      if (taskbarIcon != nullptr && currentTaskbarIcon == taskbarIcon) {
        SendMessageW(windowHandle, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(previousTaskbarIcon));
      }
      if (windowIcon != nullptr && currentWindowIcon == windowIcon) {
        SendMessageW(windowHandle, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(previousWindowIcon));
      }
    }
    if (taskbarIcon != nullptr) {
      DestroyIcon(taskbarIcon);
    }
    if (windowIcon != nullptr) {
      DestroyIcon(windowIcon);
    }
  }
#endif
};

WindowIconManager::WindowIconManager(QWidget* const window) : window_(window) {}

WindowIconManager::~WindowIconManager() = default;

QString WindowIconManager::defaultIconDirectory() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("icons"));
}

WindowIconImages WindowIconManager::loadImages(const QString& iconDirectory) {
  initializeMediaHubGuiResources();
  const QDir directory(iconDirectory);
  return {
      readExternalOrEmbedded(
          directory.filePath(QString::fromLatin1(kTaskbarIconFileName)),
          QString::fromLatin1(kEmbeddedTaskbarIcon)),
      readExternalOrEmbedded(
          directory.filePath(QString::fromLatin1(kWindowIconFileName)),
          QString::fromLatin1(kEmbeddedWindowIcon)),
  };
}

void WindowIconManager::apply() {
  if (window_ == nullptr) {
    return;
  }
  const WindowIconImages images = loadImages(defaultIconDirectory());
  if (!images.taskbarImage.isNull()) {
    window_->setWindowIcon(QIcon(QPixmap::fromImage(images.taskbarImage)));
  }

  nativeIcons_.reset();
#ifdef _WIN32
  if (QApplication::platformName() != QStringLiteral("windows")) {
    return;
  }

  auto nativeIcons = std::make_unique<NativeIcons>();
  nativeIcons->windowHandle = reinterpret_cast<HWND>(window_->winId());
  nativeIcons->taskbarIcon =
      createNativeIcon(images.taskbarImage, GetSystemMetrics(SM_CXICON),
                       GetSystemMetrics(SM_CYICON));
  nativeIcons->windowIcon =
      createNativeIcon(images.windowImage, GetSystemMetrics(SM_CXSMICON),
                       GetSystemMetrics(SM_CYSMICON));
  if (nativeIcons->taskbarIcon != nullptr) {
    nativeIcons->previousTaskbarIcon = reinterpret_cast<HICON>(SendMessageW(
        nativeIcons->windowHandle, WM_SETICON, ICON_BIG,
        reinterpret_cast<LPARAM>(nativeIcons->taskbarIcon)));
  }
  if (nativeIcons->windowIcon != nullptr) {
    nativeIcons->previousWindowIcon = reinterpret_cast<HICON>(SendMessageW(
        nativeIcons->windowHandle, WM_SETICON, ICON_SMALL,
        reinterpret_cast<LPARAM>(nativeIcons->windowIcon)));
  }
  nativeIcons_ = std::move(nativeIcons);
#endif
}

}  // namespace mediahub::gui
