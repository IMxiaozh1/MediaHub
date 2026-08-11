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
    Completed,
    Failed,
    Cancelled,
};

}  // namespace mediahub::gui
