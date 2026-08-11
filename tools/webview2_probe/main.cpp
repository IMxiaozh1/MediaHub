#include <Windows.h>
#include <WebView2.h>

#include <iomanip>
#include <iostream>
#include <memory>

namespace {

struct CoTaskMemStringDeleter {
    void operator()(wchar_t* value) const noexcept {
        CoTaskMemFree(value);
    }
};

using CoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemStringDeleter>;

int reportProbeFailure(const char* category, HRESULT result) {
    std::cerr << "WebView2 Runtime probe failure: " << category << ", HRESULT 0x"
              << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
              << static_cast<unsigned long>(result) << '\n';
    return 1;
}

}

// 调用线程：程序主线程。
int main() {
    LPWSTR rawVersion = nullptr;
    const HRESULT result =
        GetAvailableCoreWebView2BrowserVersionString(nullptr, &rawVersion);
    CoTaskMemString version(rawVersion);

    if (FAILED(result)) {
        return reportProbeFailure("api_failure", result);
    }
    if (!version || version.get()[0] == L'\0') {
        return reportProbeFailure("empty_version", result);
    }

    std::wcout << L"WebView2 Runtime version: " << version.get() << L'\n';
    return 0;
}
