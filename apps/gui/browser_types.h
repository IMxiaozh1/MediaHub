#pragma once

#include <QString>

#include <cstdint>

namespace mediahub::gui {

enum class BrowserAddressKind {
    Web,
    LocalFile,
    ExternalProtocol,
    Blocked,
    Invalid,
};

struct BrowserAddress {
    BrowserAddressKind kind{BrowserAddressKind::Invalid};
    QString url;
};

enum class BrowserErrorKind {
    RuntimeUnavailable,
    InitializationFailed,
    ProfileUnavailable,
    InvalidAddress,
    NavigationFailed,
    BlockedScheme,
    ExternalProtocolFailed,
    CertificateRejected,
    PermissionDenied,
    DownloadFailed,
    ClearDataFailed,
};

enum class BrowserPageState {
    Unavailable,
    Initializing,
    Ready,
    Navigating,
    ClearingData,
    Failed,
    ShuttingDown,
};

// 网页进程失败只暴露宿主可稳定处理的类别，不包含进程描述、退出码或网页数据。
enum class BrowserProcessFailureKind {
    RenderProcessExited,
    RenderProcessUnresponsive,
    BrowserProcessExited,
    OtherProcessExited,
};

// WebView2 原生子窗口只向 GUI 投递稳定快捷键语义，不暴露 Windows 消息或虚拟键值。
enum class BrowserAccelerator {
    FocusAddress,
    FocusCycle,
    NewTab,
    CloseTab,
    NextTab,
    PreviousTab,
    FindInPage,
    ReopenClosedTab,
    Back,
    Forward,
    Reload,
    ZoomIn,
    ZoomOut,
    ResetZoom,
    ShowHistory,
    ShowFavorites,
    ShowDownloads,
    ExitFullScreen,
};

enum class BrowserPermissionKind {
    Camera,
    Microphone,
    Geolocation,
    Notifications,
    ScreenCapture,
    ClipboardRead,
    Other,
};

enum class BrowserPermissionDecision {
    AllowOnce,
    RememberForOrigin,
    Deny,
};

enum class BrowserCertificateDecision {
    ReturnToSafety,
    ContinueForSession,
};

enum class BrowserDownloadState {
    InProgress,
    CancelFailed,
    RetryableFailure,
    Completed,
    Failed,
    Cancelled,
};

}  // namespace mediahub::gui
