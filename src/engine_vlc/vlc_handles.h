#pragma once

#include <vlc/vlc.h>

#include <memory>

namespace mediahub::engine_vlc {

struct VlcInstanceDeleter {
    void operator()(libvlc_instance_t* instance) const noexcept {
        if (instance != nullptr) {
            libvlc_release(instance);
        }
    }
};

struct VlcMediaDeleter {
    void operator()(libvlc_media_t* media) const noexcept {
        if (media != nullptr) {
            libvlc_media_release(media);
        }
    }
};

struct VlcPlayerDeleter {
    void operator()(libvlc_media_player_t* player) const noexcept {
        if (player != nullptr) {
            libvlc_media_player_release(player);
        }
    }
};

using VlcInstancePtr = std::unique_ptr<libvlc_instance_t, VlcInstanceDeleter>;
using VlcMediaPtr = std::unique_ptr<libvlc_media_t, VlcMediaDeleter>;
using VlcPlayerPtr = std::unique_ptr<libvlc_media_player_t, VlcPlayerDeleter>;

}  // namespace mediahub::engine_vlc
