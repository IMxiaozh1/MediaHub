#pragma once

#include <QImage>
#include <QString>
#include <memory>

class QWidget;

namespace mediahub::gui {

// 保存任务栏与标题栏各自使用的图片，避免两个图标槽位相互覆盖。
struct WindowIconImages {
  QImage taskbarImage;
  QImage windowImage;
};

// 从程序旁的固定目录读取图标，并管理 Windows 原生图标句柄的生命周期。
class WindowIconManager final {
 public:
  // 调用线程：GUI 主线程。window 的生命周期必须覆盖本对象。
  explicit WindowIconManager(QWidget* window);
  ~WindowIconManager();

  WindowIconManager(const WindowIconManager&) = delete;
  WindowIconManager& operator=(const WindowIconManager&) = delete;
  WindowIconManager(WindowIconManager&&) = delete;
  WindowIconManager& operator=(WindowIconManager&&) = delete;

  // 返回 MediaHub.exe 旁的 icons 目录。
  [[nodiscard]] static QString defaultIconDirectory();
  // 分别读取 taskbar.png 和 window.jpg；无效文件自动回退到内嵌资源。
  [[nodiscard]] static WindowIconImages loadImages(const QString& iconDirectory);

  // 调用线程：GUI 主线程。应用外部图标或对应的内嵌回退图标。
  void apply();

 private:
  struct NativeIcons;

  QWidget* window_{nullptr};
  std::unique_ptr<NativeIcons> nativeIcons_;
};

}  // namespace mediahub::gui
