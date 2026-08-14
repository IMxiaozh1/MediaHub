#pragma once

#include <WebView2.h>

#include <optional>

#include "browser_types.h"

namespace mediahub::browser_webview2 {

struct AcceleratorDispatch {
    HRESULT status{S_OK};
    std::optional<gui::BrowserAccelerator> accelerator;
};

// 调用线程：WebView2 AcceleratorKeyPressed 所在 GUI STA。
// 只有成功写入 Handled 的已知按键才返回稳定动作，读取或写入失败不投递。
template <typename Args>
AcceleratorDispatch handleAcceleratorKey(Args& args, const bool isControlDown,
                                         const bool isAltDown,
                                         const bool isShiftDown,
                                         const bool isWindowsDown,
                                         const bool isWebFullScreen) noexcept {
    COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP;
    HRESULT status = args.get_KeyEventKind(&kind);
    if (FAILED(status) ||
        (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
         kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)) {
        return {status, std::nullopt};
    }

    UINT virtualKey = 0;
    status = args.get_VirtualKey(&virtualKey);
    if (FAILED(status)) {
        return {status, std::nullopt};
    }
    COREWEBVIEW2_PHYSICAL_KEY_STATUS physicalStatus{};
    status = args.get_PhysicalKeyStatus(&physicalStatus);
    if (FAILED(status) || physicalStatus.WasKeyDown != FALSE) {
        return {status, std::nullopt};
    }

    std::optional<gui::BrowserAccelerator> accelerator;
    if (isWindowsDown) {
        return {S_OK, std::nullopt};
    }
    if (isControlDown && !isAltDown && isShiftDown && virtualKey == VK_TAB) {
        accelerator = gui::BrowserAccelerator::PreviousTab;
    } else if (isControlDown && !isAltDown && isShiftDown &&
               virtualKey == static_cast<UINT>('T')) {
        accelerator = gui::BrowserAccelerator::ReopenClosedTab;
    } else if (isControlDown && !isAltDown && isShiftDown &&
               virtualKey == VK_OEM_PLUS) {
        accelerator = gui::BrowserAccelerator::ZoomIn;
    } else if (isShiftDown) {
        return {S_OK, std::nullopt};
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('L')) {
        accelerator = gui::BrowserAccelerator::FocusAddress;
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('T')) {
        accelerator = gui::BrowserAccelerator::NewTab;
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('W')) {
        accelerator = gui::BrowserAccelerator::CloseTab;
    } else if (isControlDown && !isAltDown && virtualKey == VK_TAB) {
        accelerator = gui::BrowserAccelerator::NextTab;
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('F')) {
        accelerator = gui::BrowserAccelerator::FindInPage;
    } else if (!isControlDown && isAltDown && virtualKey == VK_LEFT) {
        accelerator = gui::BrowserAccelerator::Back;
    } else if (!isControlDown && isAltDown && virtualKey == VK_RIGHT) {
        accelerator = gui::BrowserAccelerator::Forward;
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('R')) {
        accelerator = gui::BrowserAccelerator::Reload;
    } else if (isControlDown && !isAltDown &&
               (virtualKey == VK_OEM_PLUS || virtualKey == VK_ADD)) {
        accelerator = gui::BrowserAccelerator::ZoomIn;
    } else if (isControlDown && !isAltDown &&
               (virtualKey == VK_OEM_MINUS || virtualKey == VK_SUBTRACT)) {
        accelerator = gui::BrowserAccelerator::ZoomOut;
    } else if (isControlDown && !isAltDown &&
               virtualKey == static_cast<UINT>('0')) {
        accelerator = gui::BrowserAccelerator::ResetZoom;
    } else if (!isControlDown && !isAltDown && virtualKey == VK_F5) {
        accelerator = gui::BrowserAccelerator::Reload;
    } else if (!isControlDown && !isAltDown && isWebFullScreen &&
               virtualKey == VK_ESCAPE) {
        accelerator = gui::BrowserAccelerator::ExitFullScreen;
    }
    if (!accelerator.has_value()) {
        return {S_OK, std::nullopt};
    }

    status = args.put_Handled(TRUE);
    if (FAILED(status)) {
        static_cast<void>(args.put_Handled(FALSE));
    }
    return {status, SUCCEEDED(status) ? accelerator : std::nullopt};
}

}  // namespace mediahub::browser_webview2
