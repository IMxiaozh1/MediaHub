#pragma once

#include <QRect>
#include <QString>

#include <cstdint>
#include <vector>

#include "browser_backend.h"
#include "browser_event_listener.h"

namespace mediahub::test {

enum class FakeBrowserCommandKind {
    SetEventListener,
    Initialize,
    Navigate,
    GoBack,
    GoForward,
    ReloadOrStop,
    SetBounds,
    SetVisible,
    SetAudioMuted,
    SetSuspended,
    ClearBrowsingData,
    AnswerPermission,
    ChooseDownloadPath,
    CancelDownload,
    AnswerExternalProtocol,
    AnswerCertificateError,
    ExitFullScreen,
    Shutdown,
};

struct FakeBrowserCommand {
    FakeBrowserCommandKind kind;
    QString text;
    QRect bounds;
    std::uint64_t requestId{0};
    std::uint64_t generation{0};
    bool flag{false};
    gui::BrowserPermissionDecision permissionDecision{
        gui::BrowserPermissionDecision::Deny};
    gui::BrowserCertificateDecision certificateDecision{
        gui::BrowserCertificateDecision::ReturnToSafety};
};

// 假后端只记录 GUI 主线程命令，并由测试显式触发事件。
class FakeBrowserBackend final : public gui::BrowserBackend {
 public:
    void setEventListener(gui::BrowserEventListener* listener) override {
        listener_ = listener;
        commands.push_back({FakeBrowserCommandKind::SetEventListener});
    }

    void initialize(void*, const QString& userDataDirectory,
                    std::uint64_t generation) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::Initialize};
        command.text = userDataDirectory;
        command.generation = generation;
        commands.push_back(command);
    }

    void navigate(const QString& normalizedUrl, std::uint64_t generation) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::Navigate};
        command.text = normalizedUrl;
        command.generation = generation;
        commands.push_back(command);
    }

    void goBack() override { commands.push_back({FakeBrowserCommandKind::GoBack}); }
    void goForward() override { commands.push_back({FakeBrowserCommandKind::GoForward}); }
    void reloadOrStop() override {
        commands.push_back({FakeBrowserCommandKind::ReloadOrStop});
    }

    void setBounds(const QRect& bounds) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::SetBounds};
        command.bounds = bounds;
        commands.push_back(command);
    }

    void setVisible(bool isVisible) override {
        recordFlag(FakeBrowserCommandKind::SetVisible, isVisible);
    }

    void setAudioMuted(bool isMuted) override {
        recordFlag(FakeBrowserCommandKind::SetAudioMuted, isMuted);
    }

    void setSuspended(bool isSuspended) override {
        recordFlag(FakeBrowserCommandKind::SetSuspended, isSuspended);
    }

    void clearBrowsingData(std::uint64_t generation) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::ClearBrowsingData};
        command.generation = generation;
        commands.push_back(command);
    }

    void answerPermission(std::uint64_t requestId,
                          gui::BrowserPermissionDecision decision) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::AnswerPermission};
        command.requestId = requestId;
        command.permissionDecision = decision;
        commands.push_back(command);
    }

    void chooseDownloadPath(std::uint64_t requestId,
                            const QString& destination) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::ChooseDownloadPath};
        command.requestId = requestId;
        command.text = destination;
        commands.push_back(command);
    }

    void cancelDownload(std::uint64_t requestId) override {
        recordRequest(FakeBrowserCommandKind::CancelDownload, requestId);
    }

    void answerExternalProtocol(std::uint64_t requestId, bool isAllowed) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::AnswerExternalProtocol};
        command.requestId = requestId;
        command.flag = isAllowed;
        commands.push_back(command);
    }

    void answerCertificateError(
        std::uint64_t requestId,
        gui::BrowserCertificateDecision decision) override {
        FakeBrowserCommand command{FakeBrowserCommandKind::AnswerCertificateError};
        command.requestId = requestId;
        command.certificateDecision = decision;
        commands.push_back(command);
    }

    void exitFullScreen() override {
        commands.push_back({FakeBrowserCommandKind::ExitFullScreen});
    }

    void shutdown() noexcept override {
        commands.push_back({FakeBrowserCommandKind::Shutdown});
    }

    void emitReady(std::uint64_t generation) {
        if (listener_ != nullptr) {
            listener_->onBrowserReady(generation);
        }
    }

    void emitNavigationCompleted(std::uint64_t generation,
                                 const QString& visibleUrl,
                                 const QString& title = {}, bool canGoBack = false,
                                 bool canGoForward = false) {
        if (listener_ != nullptr) {
            listener_->onNavigationCompleted(generation, visibleUrl, title, canGoBack,
                                             canGoForward);
        }
    }

    void emitError(std::uint64_t generation, gui::BrowserErrorKind kind, long errorCode) {
        if (listener_ != nullptr) {
            listener_->onBrowserError(generation, kind, errorCode);
        }
    }

    [[nodiscard]] int count(FakeBrowserCommandKind kind) const {
        int result = 0;
        for (const FakeBrowserCommand& command : commands) {
            if (command.kind == kind) {
                ++result;
            }
        }
        return result;
    }

    [[nodiscard]] bool hasCommand(FakeBrowserCommandKind kind) const {
        return count(kind) > 0;
    }

    [[nodiscard]] const FakeBrowserCommand& lastCommand() const {
        return commands.back();
    }

    std::vector<FakeBrowserCommand> commands;

 private:
    void recordFlag(FakeBrowserCommandKind kind, bool flag) {
        FakeBrowserCommand command{kind};
        command.flag = flag;
        commands.push_back(command);
    }

    void recordRequest(FakeBrowserCommandKind kind, std::uint64_t requestId) {
        FakeBrowserCommand command{kind};
        command.requestId = requestId;
        commands.push_back(command);
    }

    gui::BrowserEventListener* listener_{nullptr};
};

}  // namespace mediahub::test
