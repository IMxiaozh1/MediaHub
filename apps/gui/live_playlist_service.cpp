#include "live_playlist_service.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <winhttp.h>
#endif

#include "mediahub/core/m3u.h"

namespace mediahub::gui {
namespace {

bool containsForbiddenUrlWhitespace(const QString &text) {
  return std::any_of(text.cbegin(), text.cend(), [](const QChar character) {
    return character.isSpace() || character.unicode() == 0x7FU;
  });
}

bool isHttpPlaylistUrl(const QUrl &url) {
  const QString scheme = url.scheme().toLower();
  return url.isValid() && !url.isRelative() && !url.host().isEmpty() &&
         (scheme == QStringLiteral("http") ||
          scheme == QStringLiteral("https"));
}

bool isSafeRedirect(const QUrl &source, const QUrl &target) {
  if (!isHttpPlaylistUrl(target)) {
    return false;
  }
  if (source.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) ==
          0 &&
      target.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) ==
          0) {
    return false;
  }
  const bool addsDifferentCredentials =
      (!target.userName().isEmpty() || !target.password().isEmpty()) &&
      (target.userName() != source.userName() ||
       target.password() != source.password());
  return !addsDifferentCredentials;
}

bool isValidUtf8(const QByteArray &text) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.constData());
  const auto size = static_cast<std::size_t>(text.size());
  for (std::size_t index = 0; index < size;) {
    const unsigned char first = bytes[index];
    if (first == 0U) {
      return false;
    }
    if (first <= 0x7FU) {
      ++index;
      continue;
    }

    std::size_t length = 0;
    unsigned char secondMinimum = 0x80U;
    unsigned char secondMaximum = 0xBFU;
    if (first >= 0xC2U && first <= 0xDFU) {
      length = 2;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      length = 3;
      if (first == 0xE0U) {
        secondMinimum = 0xA0U;
      } else if (first == 0xEDU) {
        secondMaximum = 0x9FU;
      }
    } else if (first >= 0xF0U && first <= 0xF4U) {
      length = 4;
      if (first == 0xF0U) {
        secondMinimum = 0x90U;
      } else if (first == 0xF4U) {
        secondMaximum = 0x8FU;
      }
    } else {
      return false;
    }

    if (index + length > size || bytes[index + 1] < secondMinimum ||
        bytes[index + 1] > secondMaximum) {
      return false;
    }
    for (std::size_t offset = 2; offset < length; ++offset) {
      if (bytes[index + offset] < 0x80U || bytes[index + offset] > 0xBFU) {
        return false;
      }
    }
    index += length;
  }
  return true;
}

bool isHlsMediaManifest(const std::string_view content) {
  constexpr std::string_view kMasterTag = "#EXT-X-STREAM-INF";
  constexpr std::string_view kTargetDurationTag = "#EXT-X-TARGETDURATION";
  constexpr std::string_view kMediaSequenceTag = "#EXT-X-MEDIA-SEQUENCE";
  constexpr std::string_view kMapTag = "#EXT-X-MAP:";
  constexpr std::string_view kPartTag = "#EXT-X-PART:";
  std::size_t lineStart = 0;
  while (lineStart < content.size()) {
    const std::size_t lineEnd = content.find('\n', lineStart);
    std::string_view line = content.substr(
        lineStart, lineEnd == std::string_view::npos ? std::string_view::npos
                                                     : lineEnd - lineStart);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
      line.remove_prefix(1);
    }
    if (line.starts_with(kMasterTag) || line.starts_with(kTargetDurationTag) ||
        line.starts_with(kMediaSequenceTag) || line.starts_with(kMapTag) ||
        line.starts_with(kPartTag)) {
      return true;
    }
    if (lineEnd == std::string_view::npos) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return false;
}

bool hasMissingHeaderIssue(const core::M3uParseResult &result) {
  return std::any_of(result.issues.cbegin(), result.issues.cend(),
                     [](const core::M3uParseIssue &issue) {
                       return issue.kind ==
                              core::M3uParseIssueKind::MissingHeader;
                     });
}

std::optional<std::string> resolveStreamUrl(const QUrl &baseUrl,
                                            const std::string_view source) {
  const QString text =
      QString::fromUtf8(source.data(), static_cast<int>(source.size()));
  if (containsForbiddenUrlWhitespace(text)) {
    return std::nullopt;
  }
  const QUrl relativeUrl(text, QUrl::StrictMode);
  if (!relativeUrl.isValid()) {
    return std::nullopt;
  }
  const QUrl resolvedUrl = baseUrl.resolved(relativeUrl);
  if (!resolvedUrl.isValid()) {
    return std::nullopt;
  }
  const QByteArray encoded = resolvedUrl.toEncoded(QUrl::FullyEncoded);
  return std::string(encoded.constData(),
                     static_cast<std::size_t>(encoded.size()));
}

#ifdef Q_OS_WIN

struct WinHttpHandleDeleter {
  void operator()(void *const handle) const noexcept {
    if (handle != nullptr) {
      WinHttpCloseHandle(handle);
    }
  }
};

using WinHttpHandle = std::unique_ptr<void, WinHttpHandleDeleter>;

struct NativeRequestResult {
  QByteArray responseBody;
  QUrl finalUrl;
  std::optional<LivePlaylistLoadError> error;
};

NativeRequestResult nativeFailure(const LivePlaylistLoadError error) {
  NativeRequestResult result;
  result.error = error;
  return result;
}

LivePlaylistLoadError currentWinHttpError() {
  return GetLastError() == ERROR_WINHTTP_TIMEOUT
             ? LivePlaylistLoadError::Timeout
             : LivePlaylistLoadError::NetworkFailure;
}

QString requestResource(const QUrl &url) {
  QString resource = url.path(QUrl::FullyEncoded);
  if (resource.isEmpty()) {
    resource = QStringLiteral("/");
  }
  const QString query = url.query(QUrl::FullyEncoded);
  if (!query.isEmpty()) {
    resource.append(QLatin1Char('?'));
    resource.append(query);
  }
  return resource;
}

std::optional<QString> responseHeader(const HINTERNET request,
                                      const DWORD query) {
  DWORD byteCount = 0;
  WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                      &byteCount, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || byteCount == 0) {
    return std::nullopt;
  }

  std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t));
  if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
                           buffer.data(), &byteCount,
                           WINHTTP_NO_HEADER_INDEX)) {
    return std::nullopt;
  }
  return QString::fromWCharArray(buffer.data()).trimmed();
}

// 调用线程：Qt 线程池工作线程，禁止在此操作 Qt 控件。
NativeRequestResult fetchWithWinHttp(const QUrl &initialUrl,
                                     const LivePlaylistLimits limits) {
  QElapsedTimer elapsed;
  elapsed.start();

  WinHttpHandle session(
      WinHttpOpen(L"MediaHub/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (session == nullptr) {
    return nativeFailure(currentWinHttpError());
  }

  QUrl currentUrl = initialUrl;
  int followedRedirects = 0;
  while (true) {
    const qint64 remainingMilliseconds =
        static_cast<qint64>(limits.timeoutMilliseconds) - elapsed.elapsed();
    if (remainingMilliseconds <= 0) {
      return nativeFailure(LivePlaylistLoadError::Timeout);
    }
    const int timeoutMilliseconds = static_cast<int>(std::min<qint64>(
        remainingMilliseconds, std::numeric_limits<int>::max()));
    if (!WinHttpSetTimeouts(session.get(), timeoutMilliseconds,
                            timeoutMilliseconds, timeoutMilliseconds,
                            timeoutMilliseconds)) {
      return nativeFailure(currentWinHttpError());
    }

    const QByteArray asciiHost = QUrl::toAce(currentUrl.host());
    const std::wstring host = QString::fromLatin1(asciiHost).toStdWString();
    const bool isHttps = currentUrl.scheme().compare(QStringLiteral("https"),
                                                     Qt::CaseInsensitive) == 0;
    const int defaultPort =
        isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    const int requestedPort = currentUrl.port(defaultPort);
    if (requestedPort <= 0 ||
        requestedPort > std::numeric_limits<INTERNET_PORT>::max()) {
      return nativeFailure(LivePlaylistLoadError::InvalidUrl);
    }

    WinHttpHandle connection(
        WinHttpConnect(session.get(), host.c_str(),
                       static_cast<INTERNET_PORT>(requestedPort), 0));
    if (connection == nullptr) {
      return nativeFailure(currentWinHttpError());
    }

    const std::wstring resource = requestResource(currentUrl).toStdWString();
    const wchar_t *acceptTypes[] = {L"audio/x-mpegurl",
                                    L"application/vnd.apple.mpegurl",
                                    L"text/plain", L"*/*", nullptr};
    const DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(
        WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
                           WINHTTP_NO_REFERER, acceptTypes, flags));
    if (request == nullptr) {
      return nativeFailure(currentWinHttpError());
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disabledFeatures =
        WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
    DWORD autoLogonPolicy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirectPolicy, sizeof(redirectPolicy)) ||
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE,
                          &disabledFeatures, sizeof(disabledFeatures)) ||
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY,
                          &autoLogonPolicy, sizeof(autoLogonPolicy))) {
      return nativeFailure(currentWinHttpError());
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
      return nativeFailure(currentWinHttpError());
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                             &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
      return nativeFailure(currentWinHttpError());
    }

    if (statusCode >= 300 && statusCode < 400) {
      if (followedRedirects >= limits.maximumRedirects) {
        return nativeFailure(LivePlaylistLoadError::TooManyRedirects);
      }
      const auto location =
          responseHeader(request.get(), WINHTTP_QUERY_LOCATION);
      if (!location.has_value()) {
        return nativeFailure(LivePlaylistLoadError::NetworkFailure);
      }
      const QUrl target =
          currentUrl.resolved(QUrl(*location, QUrl::StrictMode));
      if (!isSafeRedirect(currentUrl, target)) {
        return nativeFailure(LivePlaylistLoadError::UnsafeRedirect);
      }
      currentUrl = target;
      ++followedRedirects;
      continue;
    }
    if (statusCode < 200 || statusCode >= 300) {
      return nativeFailure(LivePlaylistLoadError::NetworkFailure);
    }

    if (const auto contentLength =
            responseHeader(request.get(), WINHTTP_QUERY_CONTENT_LENGTH);
        contentLength.has_value()) {
      bool isLengthValid = false;
      const qlonglong length = contentLength->toLongLong(&isLengthValid);
      if (isLengthValid && length > limits.maximumResponseBytes) {
        return nativeFailure(LivePlaylistLoadError::ResponseTooLarge);
      }
    }

    QByteArray responseBody;
    while (true) {
      DWORD availableBytes = 0;
      if (!WinHttpQueryDataAvailable(request.get(), &availableBytes)) {
        return nativeFailure(currentWinHttpError());
      }
      if (availableBytes == 0) {
        break;
      }
      if (static_cast<qint64>(responseBody.size()) + availableBytes >
          limits.maximumResponseBytes) {
        return nativeFailure(LivePlaylistLoadError::ResponseTooLarge);
      }

      const int chunkSize = static_cast<int>(std::min<DWORD>(
          availableBytes, static_cast<DWORD>(std::numeric_limits<int>::max())));
      QByteArray chunk(chunkSize, Qt::Uninitialized);
      DWORD readBytes = 0;
      if (!WinHttpReadData(request.get(), chunk.data(),
                           static_cast<DWORD>(chunk.size()), &readBytes)) {
        return nativeFailure(currentWinHttpError());
      }
      chunk.resize(static_cast<int>(readBytes));
      responseBody.append(chunk);
    }

    NativeRequestResult result;
    result.responseBody = std::move(responseBody);
    result.finalUrl = currentUrl;
    return result;
  }
}

#endif

} // namespace

LivePlaylistService::LivePlaylistService(
    QObject *const parent, LivePlaylistLimits limits,
    QNetworkAccessManager *const networkManager)
    : QObject(parent), limits_(limits), networkManager_(networkManager) {
  limits_.timeoutMilliseconds = std::max(limits_.timeoutMilliseconds, 1);
  limits_.maximumRedirects = std::max(limits_.maximumRedirects, 0);
  limits_.maximumResponseBytes =
      std::max<qint64>(limits_.maximumResponseBytes, 1);
  limits_.maximumEntries = std::max<std::size_t>(limits_.maximumEntries, 1U);
#ifdef Q_OS_WIN
  usesNativeNetworking_ = networkManager_ == nullptr;
#endif
  if (networkManager_ == nullptr) {
    networkManager_ = new QNetworkAccessManager(this);
  }
  timeoutTimer_ = new QTimer(this);
  timeoutTimer_->setObjectName(QStringLiteral("livePlaylistTimeoutTimer"));
  timeoutTimer_->setSingleShot(true);
  connect(timeoutTimer_, &QTimer::timeout, this, [this] {
    if (currentReply_ != nullptr || nativeRequestWatcher_ != nullptr) {
      finishFailure(LivePlaylistLoadError::Timeout, generation_);
    }
  });
}

LivePlaylistService::~LivePlaylistService() { cancel(); }

void LivePlaylistService::load(const QString &playlistUrl) {
  Q_ASSERT(thread() == QThread::currentThread());
  cancel();

  const QString normalizedUrl = playlistUrl.trimmed();
  const QUrl url(normalizedUrl, QUrl::StrictMode);
  if (normalizedUrl.isEmpty() ||
      containsForbiddenUrlWhitespace(normalizedUrl) ||
      !isHttpPlaylistUrl(url)) {
    emit loadFailed(LivePlaylistLoadError::InvalidUrl);
    return;
  }

  followedRedirects_ = 0;
  timeoutTimer_->start(limits_.timeoutMilliseconds);
  if (usesNativeNetworking_) {
    startNativeRequest(url, generation_);
  } else {
    startRequest(url, generation_);
  }
}

void LivePlaylistService::cancel() noexcept {
  ++generation_;
  timeoutTimer_->stop();
  responseBody_.clear();
  if (nativeRequestWatcher_ != nullptr) {
    QFutureWatcherBase *const watcher =
        std::exchange(nativeRequestWatcher_, nullptr);
    disconnect(watcher, nullptr, this, nullptr);
    watcher->deleteLater();
  }
  if (currentReply_ != nullptr) {
    QNetworkReply *const reply = std::exchange(currentReply_, nullptr);
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
}

void LivePlaylistService::startRequest(const QUrl &url,
                                       const std::uint64_t generation) {
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                       QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                       QNetworkRequest::Manual);
  request.setRawHeader("User-Agent", "MediaHub/0.2");

  responseBody_.clear();
  currentReply_ = networkManager_->get(request);
  currentReply_->setReadBufferSize(limits_.maximumResponseBytes + 1);
  QNetworkReply *const reply = currentReply_;
  connect(reply, &QNetworkReply::readyRead, this,
          [this, reply, generation] { consumeReplyData(reply, generation); });
  connect(
      reply, &QNetworkReply::metaDataChanged, this, [this, reply, generation] {
        if (!isCurrentReply(reply, generation)) {
          return;
        }
        bool isLengthValid = false;
        const qlonglong contentLength =
            reply->header(QNetworkRequest::ContentLengthHeader)
                .toLongLong(&isLengthValid);
        if (isLengthValid && contentLength > limits_.maximumResponseBytes) {
          finishFailure(LivePlaylistLoadError::ResponseTooLarge, generation);
        }
      });
  connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
    handleReplyFinished(reply, generation);
  });
}

void LivePlaylistService::startNativeRequest(const QUrl &url,
                                             const std::uint64_t generation) {
#ifdef Q_OS_WIN
  auto *const watcher = new QFutureWatcher<NativeRequestResult>(this);
  nativeRequestWatcher_ = watcher;
  connect(watcher, &QFutureWatcher<NativeRequestResult>::finished, this,
          [this, watcher, generation] {
            if (nativeRequestWatcher_ != watcher || generation != generation_) {
              watcher->deleteLater();
              return;
            }
            nativeRequestWatcher_ = nullptr;
            NativeRequestResult result = watcher->result();
            watcher->deleteLater();
            if (result.error.has_value()) {
              finishFailure(*result.error, generation);
              return;
            }
            finishResponse(std::move(result.responseBody), result.finalUrl,
                           generation);
          });
  watcher->setFuture(QtConcurrent::run(
      [url, limits = limits_] { return fetchWithWinHttp(url, limits); }));
#else
  startRequest(url, generation);
#endif
}

void LivePlaylistService::consumeReplyData(QNetworkReply *const reply,
                                           const std::uint64_t generation) {
  if (!isCurrentReply(reply, generation)) {
    return;
  }
  responseBody_.append(reply->readAll());
  if (responseBody_.size() > limits_.maximumResponseBytes) {
    finishFailure(LivePlaylistLoadError::ResponseTooLarge, generation);
  }
}

void LivePlaylistService::handleReplyFinished(QNetworkReply *const reply,
                                              const std::uint64_t generation) {
  if (!isCurrentReply(reply, generation)) {
    return;
  }
  consumeReplyData(reply, generation);
  if (!isCurrentReply(reply, generation)) {
    return;
  }

  const QVariant redirectTarget =
      reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
  if (redirectTarget.isValid() && !redirectTarget.toUrl().isEmpty()) {
    if (followedRedirects_ >= limits_.maximumRedirects) {
      finishFailure(LivePlaylistLoadError::TooManyRedirects, generation);
      return;
    }
    const QUrl nextUrl = reply->url().resolved(redirectTarget.toUrl());
    if (!isSafeRedirect(reply->url(), nextUrl)) {
      finishFailure(LivePlaylistLoadError::UnsafeRedirect, generation);
      return;
    }

    disconnect(reply, nullptr, this, nullptr);
    currentReply_ = nullptr;
    reply->deleteLater();
    ++followedRedirects_;
    startRequest(nextUrl, generation);
    return;
  }

  const int statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError || statusCode < 200 ||
      statusCode >= 300) {
    finishFailure(LivePlaylistLoadError::NetworkFailure, generation);
    return;
  }

  const QUrl finalUrl = reply->url();
  disconnect(reply, nullptr, this, nullptr);
  currentReply_ = nullptr;
  reply->deleteLater();
  finishResponse(std::exchange(responseBody_, {}), finalUrl, generation);
}

void LivePlaylistService::finishResponse(QByteArray responseBody,
                                         const QUrl &finalUrl,
                                         const std::uint64_t generation) {
  if (generation != generation_) {
    return;
  }
  timeoutTimer_->stop();

  if (!isValidUtf8(responseBody)) {
    emit loadFailed(LivePlaylistLoadError::InvalidUtf8);
    return;
  }

  const std::string content(responseBody.constData(),
                            static_cast<std::size_t>(responseBody.size()));
  if (isHlsMediaManifest(content)) {
    emit loadFailed(LivePlaylistLoadError::HlsMediaManifest);
    return;
  }

  core::M3uParseResult parsed =
      core::parseM3u(content, [finalUrl](const std::string_view source) {
        return resolveStreamUrl(finalUrl, source);
      });
  if (hasMissingHeaderIssue(parsed)) {
    emit loadFailed(LivePlaylistLoadError::InvalidFormat);
    return;
  }
  if (parsed.library.channels.size() > limits_.maximumEntries) {
    emit loadFailed(LivePlaylistLoadError::TooManyEntries);
    return;
  }
  if (parsed.library.channels.empty()) {
    emit loadFailed(LivePlaylistLoadError::NoPlayableEntries);
    return;
  }

  LivePlaylistLoadResult result;
  result.library = std::move(parsed.library);
  result.skippedChannelCount = parsed.skippedChannelCount;
  result.duplicateChannelCount = parsed.duplicateChannelCount;
  emit loadSucceeded(std::move(result));
}

void LivePlaylistService::finishFailure(const LivePlaylistLoadError error,
                                        const std::uint64_t generation) {
  if (generation != generation_) {
    return;
  }
  timeoutTimer_->stop();
  responseBody_.clear();
  if (nativeRequestWatcher_ != nullptr) {
    QFutureWatcherBase *const watcher =
        std::exchange(nativeRequestWatcher_, nullptr);
    disconnect(watcher, nullptr, this, nullptr);
    watcher->deleteLater();
  }
  if (currentReply_ != nullptr) {
    QNetworkReply *const reply = std::exchange(currentReply_, nullptr);
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
  emit loadFailed(error);
}

bool LivePlaylistService::isCurrentReply(
    const QNetworkReply *const reply,
    const std::uint64_t generation) const noexcept {
  return generation == generation_ && currentReply_ == reply;
}

} // namespace mediahub::gui
