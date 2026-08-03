#pragma once

#include "mediahub/core/player_engine.h"

#include <chrono>
#include <memory>
#include <optional>

namespace mediahub::engine_vlc {

// 哑输出仅供自动化测试使用；正式程序保持两个选项为 false。
struct VlcPlayerEngineOptions {
    bool useDummyAudioOutput{false};
    bool useDummyVideoOutput{false};
};

// libVLC 播放实现。线程、异步请求和监听器生命周期约定继承 PlayerEngine。
class VlcPlayerEngine final : public core::PlayerEngine {
public:
    explicit VlcPlayerEngine(VlcPlayerEngineOptions options = {});
    ~VlcPlayerEngine() override;

    VlcPlayerEngine(const VlcPlayerEngine&) = delete;
    VlcPlayerEngine& operator=(const VlcPlayerEngine&) = delete;
    VlcPlayerEngine(VlcPlayerEngine&&) = delete;
    VlcPlayerEngine& operator=(VlcPlayerEngine&&) = delete;

    void open(core::MediaItem item) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(std::chrono::milliseconds position) override;
    void setVolume(int volume) override;
    void setMuted(bool isMuted) override;
    void setVideoSurface(void* nativeHandle) override;

    [[nodiscard]] core::PlaybackState state() const override;
    [[nodiscard]] core::PlaybackPosition position() const override;
    [[nodiscard]] std::optional<std::chrono::milliseconds> duration() const override;
    [[nodiscard]] bool isSeekable() const override;
    void setEventListener(core::PlayerEventListener* listener) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mediahub::engine_vlc
