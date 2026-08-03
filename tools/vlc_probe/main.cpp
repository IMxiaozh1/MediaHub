#include <iostream>
#include <memory>

#include <vlc/vlc.h>

namespace {

struct VlcInstanceDeleter {
    void operator()(libvlc_instance_t* instance) const noexcept {
        if (instance != nullptr) {
            libvlc_release(instance);
        }
    }
};

using VlcInstance = std::unique_ptr<libvlc_instance_t, VlcInstanceDeleter>;

}

int main() {
    VlcInstance instance(libvlc_new(0, nullptr));
    if (!instance) {
        std::cerr << "Failed to initialize libVLC.\n";
        return 1;
    }

    std::cout << "libVLC version: " << libvlc_get_version() << '\n';
    return 0;
}
