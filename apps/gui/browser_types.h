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

// WebView2 原生子窗口只向 GUI 投递稳定快捷键语义，不暴露 Windows 消息或虚拟键值。
enum class BrowserAccelerator {
    FocusAddress,
    Back,
    Forward,
    Reload,
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
    Completed,
    Failed,
    Cancelled,
};

}  // namespace mediahub::gui
