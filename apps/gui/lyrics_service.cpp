#include "lyrics_service.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslSocket>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace mediahub::gui {
namespace {

constexpr int kNetworkTimeoutMilliseconds = 6000;
constexpr qint64 kMaximumId3TagBytes = 16 * 1024 * 1024;

struct TrackMetadata {
  QString title;
  QString artist;
  QString album;
  QString embeddedLyrics;
};

struct SearchCandidate {
  qint64 id{-1};
  QString identity;
  QString accessKey;
  QString versionSignature;
  QString title;
  QString artist;
  QString album;
  qint64 durationMilliseconds{-1};
  QString lyrics;
  QString synchronizedLyrics;
};

QString cleanedText(QString text) {
  text.remove(QChar::Null);
  return text.trimmed();
}

QString normalizedTerm(QString text) {
  text = text.toCaseFolded().normalized(QString::NormalizationForm_KC);
  static const QRegularExpression separators(
      QStringLiteral("[\\s\\p{P}\\p{S}]+"),
      QRegularExpression::UseUnicodePropertiesOption);
  text.remove(separators);
  return text;
}

QString titleWithoutTrailingQualifier(QString title) {
  title = title.trimmed();
  static const QRegularExpression trailingQualifier(
      QStringLiteral("\\s*[\\(（\\[【].*[\\)）\\]】]\\s*$"));
  const QString simplified =
      title.left(title.indexOf(trailingQualifier)).trimmed();
  return simplified.isEmpty() ? title : simplified;
}

QString versionQualifierSignature(const QString& title) {
  static const QRegularExpression versionQualifier(
      QStringLiteral(
          "(?:live|remix|cover|dj|instrumental|纯音乐|伴奏|翻唱|翻自|"
          "tv版|现场版|加速版|降速版|女声版|男声版|铃声|片段)"),
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::UseUnicodePropertiesOption);
  QStringList qualifiers;
  auto matches = versionQualifier.globalMatch(title);
  while (matches.hasNext()) {
    qualifiers.push_back(normalizedTerm(matches.next().captured()));
  }
  qualifiers.removeDuplicates();
  qualifiers.sort();
  return qualifiers.join(QLatin1Char('|'));
}

int syncSafeInteger(const char* const data) {
  return ((static_cast<unsigned char>(data[0]) & 0x7F) << 21) |
         ((static_cast<unsigned char>(data[1]) & 0x7F) << 14) |
         ((static_cast<unsigned char>(data[2]) & 0x7F) << 7) |
         (static_cast<unsigned char>(data[3]) & 0x7F);
}

int bigEndianInteger(const char* const data) {
  return (static_cast<unsigned char>(data[0]) << 24) |
         (static_cast<unsigned char>(data[1]) << 16) |
         (static_cast<unsigned char>(data[2]) << 8) |
         static_cast<unsigned char>(data[3]);
}

int threeByteInteger(const char* const data) {
  return (static_cast<unsigned char>(data[0]) << 16) |
         (static_cast<unsigned char>(data[1]) << 8) |
         static_cast<unsigned char>(data[2]);
}

QString decodeUtf16(const QByteArray& bytes, bool bigEndian) {
  if (bytes.size() < 2) {
    return {};
  }
  int offset = 0;
  const auto first = static_cast<unsigned char>(bytes[0]);
  const auto second = static_cast<unsigned char>(bytes[1]);
  if (first == 0xFE && second == 0xFF) {
    bigEndian = true;
    offset = 2;
  } else if (first == 0xFF && second == 0xFE) {
    bigEndian = false;
    offset = 2;
  }

  QVector<ushort> codeUnits;
  codeUnits.reserve((bytes.size() - offset) / 2);
  for (int index = offset; index + 1 < bytes.size(); index += 2) {
    const auto left = static_cast<unsigned char>(bytes[index]);
    const auto right = static_cast<unsigned char>(bytes[index + 1]);
    codeUnits.push_back(static_cast<ushort>(bigEndian ? (left << 8) | right
                                                      : (right << 8) | left));
  }
  return cleanedText(
      QString::fromUtf16(codeUnits.constData(), codeUnits.size()));
}

QString decodeId3Text(const QByteArray& bytes, const int encoding) {
  switch (encoding) {
    case 0:
      return cleanedText(QString::fromLatin1(bytes));
    case 1:
      return decodeUtf16(bytes, false);
    case 2:
      return decodeUtf16(bytes, true);
    case 3:
      return cleanedText(QString::fromUtf8(bytes));
    default:
      return {};
  }
}

int encodedTerminator(const QByteArray& bytes, const int start,
                      const int encoding) {
  if (encoding == 0 || encoding == 3) {
    return bytes.indexOf('\0', start);
  }
  for (int index = start; index + 1 < bytes.size(); index += 2) {
    if (bytes[index] == '\0' && bytes[index + 1] == '\0') {
      return index;
    }
  }
  return -1;
}

QString decodedTextFrame(const QByteArray& frame) {
  if (frame.isEmpty()) {
    return {};
  }
  return decodeId3Text(frame.mid(1), static_cast<unsigned char>(frame[0]));
}

QString decodedUnsynchronizedLyrics(const QByteArray& frame) {
  if (frame.size() < 5) {
    return {};
  }
  const int encoding = static_cast<unsigned char>(frame[0]);
  const int terminator = encodedTerminator(frame, 4, encoding);
  const int textStart =
      terminator < 0 ? 4
                     : terminator + ((encoding == 1 || encoding == 2) ? 2 : 1);
  return decodeId3Text(frame.mid(textStart), encoding);
}

void applyTextFrame(TrackMetadata& metadata, const QByteArray& frameId,
                    const QByteArray& frame) {
  const QString text = decodedTextFrame(frame);
  if (text.isEmpty()) {
    return;
  }
  if (frameId == "TIT2" || frameId == "TT2") {
    metadata.title = text;
  } else if (frameId == "TPE1" || frameId == "TP1") {
    metadata.artist = text;
  } else if (frameId == "TALB" || frameId == "TAL") {
    metadata.album = text;
  }
}

void readId3v2(QFile& file, TrackMetadata& metadata) {
  if (!file.seek(0)) {
    return;
  }
  const QByteArray header = file.read(10);
  if (header.size() != 10 || header.left(3) != "ID3") {
    return;
  }

  const int version = static_cast<unsigned char>(header[3]);
  const int tagSize = syncSafeInteger(header.constData() + 6);
  if (tagSize <= 0 || tagSize > kMaximumId3TagBytes) {
    return;
  }
  const QByteArray tag = file.read(tagSize);
  int offset = 0;

  while (version == 2 && offset + 6 <= tag.size()) {
    const QByteArray frameId = tag.mid(offset, 3);
    if (frameId == QByteArray(3, '\0')) {
      break;
    }
    const int frameSize = threeByteInteger(tag.constData() + offset + 3);
    offset += 6;
    if (frameSize <= 0 || offset + frameSize > tag.size()) {
      break;
    }
    const QByteArray frame = tag.mid(offset, frameSize);
    if (frameId.startsWith('T')) {
      applyTextFrame(metadata, frameId, frame);
    } else if (frameId == "ULT" && metadata.embeddedLyrics.isEmpty()) {
      metadata.embeddedLyrics = decodedUnsynchronizedLyrics(frame);
    }
    offset += frameSize;
  }

  while ((version == 3 || version == 4) && offset + 10 <= tag.size()) {
    const QByteArray frameId = tag.mid(offset, 4);
    if (frameId == QByteArray(4, '\0')) {
      break;
    }
    const int frameSize = version == 4
                              ? syncSafeInteger(tag.constData() + offset + 4)
                              : bigEndianInteger(tag.constData() + offset + 4);
    offset += 10;
    if (frameSize <= 0 || offset + frameSize > tag.size()) {
      break;
    }
    const QByteArray frame = tag.mid(offset, frameSize);
    if (frameId.startsWith('T')) {
      applyTextFrame(metadata, frameId, frame);
    } else if (frameId == "USLT" && metadata.embeddedLyrics.isEmpty()) {
      metadata.embeddedLyrics = decodedUnsynchronizedLyrics(frame);
    }
    offset += frameSize;
  }
}

QString decodedId3v1Field(const QByteArray& field) {
  const int terminator = field.indexOf('\0');
  const QByteArray trimmed = terminator < 0 ? field : field.left(terminator);
  return cleanedText(QString::fromLocal8Bit(trimmed));
}

void readId3v1(QFile& file, TrackMetadata& metadata) {
  if (file.size() < 128 || !file.seek(file.size() - 128)) {
    return;
  }
  const QByteArray tag = file.read(128);
  if (tag.size() != 128 || tag.left(3) != "TAG") {
    return;
  }
  if (metadata.title.isEmpty()) {
    metadata.title = decodedId3v1Field(tag.mid(3, 30));
  }
  if (metadata.artist.isEmpty()) {
    metadata.artist = decodedId3v1Field(tag.mid(33, 30));
  }
  if (metadata.album.isEmpty()) {
    metadata.album = decodedId3v1Field(tag.mid(63, 30));
  }
}

TrackMetadata readTrackMetadata(const LyricsQuery& query) {
  TrackMetadata metadata;
  QFile file(query.filePath);
  if (file.open(QIODevice::ReadOnly)) {
    readId3v2(file, metadata);
    readId3v1(file, metadata);
  }

  const QString baseName =
      QFileInfo(query.filePath).completeBaseName().trimmed();
  const QString fallbackName =
      baseName.isEmpty() ? QFileInfo(query.displayName).completeBaseName()
                         : baseName;
  if (metadata.title.isEmpty()) {
    const int separator = fallbackName.indexOf(QStringLiteral(" - "));
    if (separator > 0 && separator + 3 < fallbackName.size()) {
      if (metadata.artist.isEmpty()) {
        metadata.artist = fallbackName.left(separator).trimmed();
      }
      metadata.title = fallbackName.mid(separator + 3).trimmed();
    } else {
      metadata.title = fallbackName.trimmed();
    }
  }
  return metadata;
}

QString artistFromSong(const QJsonObject& song) {
  QJsonArray artists = song.value(QStringLiteral("artists")).toArray();
  if (artists.isEmpty()) {
    artists = song.value(QStringLiteral("ar")).toArray();
  }
  QStringList names;
  for (const auto& value : artists) {
    const QString name =
        value.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      names.push_back(name);
    }
  }
  return names.join(QStringLiteral(" / "));
}

QString albumFromSong(const QJsonObject& song) {
  QJsonObject album = song.value(QStringLiteral("album")).toObject();
  if (album.isEmpty()) {
    album = song.value(QStringLiteral("al")).toObject();
  }
  return album.value(QStringLiteral("name")).toString();
}

SearchCandidate kugouSongCandidate(const QJsonObject& song) {
  SearchCandidate candidate;
  candidate.identity = song.value(QStringLiteral("hash")).toString();
  const QString displayTitle =
      song.value(QStringLiteral("songname")).toString();
  candidate.versionSignature = versionQualifierSignature(displayTitle);
  candidate.title = song.value(QStringLiteral("songname_original")).toString();
  if (candidate.title.isEmpty()) {
    candidate.title = displayTitle;
  }
  candidate.artist = song.value(QStringLiteral("singername")).toString();
  candidate.album = song.value(QStringLiteral("album_name")).toString();
  candidate.durationMilliseconds =
      song.value(QStringLiteral("duration")).toVariant().toLongLong() * 1000;
  return candidate;
}

void appendKugouSongCandidates(const QJsonObject& song,
                               QVector<SearchCandidate>& candidates) {
  candidates.push_back(kugouSongCandidate(song));
  for (const auto& groupedValue :
       song.value(QStringLiteral("group")).toArray()) {
    candidates.push_back(kugouSongCandidate(groupedValue.toObject()));
  }
}

qint64 durationFromSong(const QJsonObject& song) {
  const QJsonValue duration = song.contains(QStringLiteral("duration"))
                                  ? song.value(QStringLiteral("duration"))
                                  : song.value(QStringLiteral("dt"));
  return duration.toVariant().toLongLong();
}

int candidateScore(const TrackMetadata& expected, const qint64 expectedDuration,
                   const SearchCandidate& candidate) {
  const QString expectedTitle = normalizedTerm(expected.title);
  const QString candidateTitle = normalizedTerm(candidate.title);
  if (expectedTitle.isEmpty() || candidateTitle.isEmpty()) {
    return std::numeric_limits<int>::min();
  }
  const QString expectedVersion = versionQualifierSignature(expected.title);
  const QString candidateVersion =
      candidate.versionSignature.isEmpty()
          ? versionQualifierSignature(candidate.title)
          : candidate.versionSignature;
  if (!candidateVersion.isEmpty() && candidateVersion != expectedVersion) {
    return std::numeric_limits<int>::min();
  }

  const QString expectedCoreTitle =
      normalizedTerm(titleWithoutTrailingQualifier(expected.title));
  const QString candidateCoreTitle =
      normalizedTerm(titleWithoutTrailingQualifier(candidate.title));
  const bool hasStrongTitleMatch =
      expectedTitle == candidateTitle ||
      (!expectedCoreTitle.isEmpty() && expectedCoreTitle == candidateCoreTitle);
  const qint64 durationDifference =
      expectedDuration > 0 && candidate.durationMilliseconds > 0
          ? std::abs(expectedDuration - candidate.durationMilliseconds)
          : std::numeric_limits<qint64>::max();

  int score = 0;
  if (hasStrongTitleMatch) {
    score += 120;
  } else if (candidateTitle.contains(expectedTitle) ||
             expectedTitle.contains(candidateTitle)) {
    score += 55;
  } else {
    return std::numeric_limits<int>::min();
  }

  const QString expectedArtist = normalizedTerm(expected.artist);
  const QString candidateArtist = normalizedTerm(candidate.artist);
  if (!expectedArtist.isEmpty()) {
    if (expectedArtist == candidateArtist) {
      score += 100;
    } else if (candidateArtist.contains(expectedArtist) ||
               expectedArtist.contains(candidateArtist)) {
      score += 45;
    } else {
      // 本地标签与在线曲库偶尔使用不同的演唱者署名。只有标题去除
      // “电视剧插曲”等尾缀后完全一致且时长接近时，才降低这项惩罚。
      score -= hasStrongTitleMatch && durationDifference <= 5000 ? 20 : 100;
    }
  }

  if (expectedDuration > 0 && candidate.durationMilliseconds > 0) {
    if (durationDifference <= 2000) {
      score += 70;
    } else if (durationDifference <= 5000) {
      score += 45;
    } else if (durationDifference <= 15000) {
      score += 15;
    } else {
      score -= 80;
    }
  }

  if (!expected.album.isEmpty() &&
      normalizedTerm(expected.album) == normalizedTerm(candidate.album)) {
    score += 25;
  }
  return score;
}

SearchCandidate bestKugouSongCandidate(const QByteArray& payload,
                                       const TrackMetadata& expected,
                                       const qint64 expectedDuration) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  const QJsonArray songs = document.object()
                               .value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("info"))
                               .toArray();
  QVector<SearchCandidate> candidates;
  for (const auto& value : songs) {
    appendKugouSongCandidates(value.toObject(), candidates);
  }

  SearchCandidate best;
  int bestScore = std::numeric_limits<int>::min();
  for (const auto& candidate : candidates) {
    const int score = candidateScore(expected, expectedDuration, candidate);
    if (!candidate.identity.isEmpty() && score > bestScore) {
      bestScore = score;
      best = candidate;
    }
  }
  const int minimumScore = expected.artist.isEmpty() ? 100 : 145;
  return bestScore >= minimumScore ? best : SearchCandidate{};
}

QString cacheFilePath(const TrackMetadata& metadata,
                      const qint64 durationMilliseconds) {
  const QString cacheRoot =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cacheRoot.isEmpty()) {
    return {};
  }
  const QByteArray identity =
      QStringLiteral("lyrics-v4|%1|%2|%3|%4")
          .arg(normalizedTerm(metadata.title), normalizedTerm(metadata.artist),
               normalizedTerm(metadata.album))
          .arg(durationMilliseconds)
          .toUtf8();
  const QString key = QString::fromLatin1(
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
  const QString directory = cacheRoot + QStringLiteral("/lyrics");
  if (!QDir().mkpath(directory)) {
    return {};
  }
  return directory + QLatin1Char('/') + key + QStringLiteral(".json");
}

QJsonObject resultToJson(const LyricsResult& result) {
  QJsonArray lines;
  for (const auto& line : result.synchronizedLines) {
    lines.push_back(QJsonObject{
        {QStringLiteral("time"), static_cast<double>(line.timeMilliseconds)},
        {QStringLiteral("text"), line.text},
    });
  }
  return QJsonObject{
      {QStringLiteral("title"), result.title},
      {QStringLiteral("artist"), result.artist},
      {QStringLiteral("album"), result.album},
      {QStringLiteral("source"), result.sourceName},
      {QStringLiteral("plain"), result.plainText},
      {QStringLiteral("lines"), lines},
  };
}

bool cachedResult(const QString& path, LyricsResult& result) {
  if (path.isEmpty()) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }

  const QJsonObject object = document.object();
  result.kind = LyricsResultKind::Ready;
  result.title = object.value(QStringLiteral("title")).toString();
  result.artist = object.value(QStringLiteral("artist")).toString();
  result.album = object.value(QStringLiteral("album")).toString();
  result.sourceName = object.value(QStringLiteral("source")).toString();
  result.plainText = object.value(QStringLiteral("plain")).toString();
  for (const auto& value : object.value(QStringLiteral("lines")).toArray()) {
    const QJsonObject line = value.toObject();
    const QString text = line.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
      result.synchronizedLines.push_back(
          {line.value(QStringLiteral("time")).toVariant().toLongLong(), text});
    }
  }
  return !result.plainText.isEmpty() || !result.synchronizedLines.isEmpty();
}

void saveCachedResult(const QString& path, const LyricsResult& result) {
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(
        QJsonDocument(resultToJson(result)).toJson(QJsonDocument::Compact));
  }
}

LyricsResult readyResult(const TrackMetadata& metadata,
                         const QString& sourceName, const QString& lyrics) {
  LyricsResult result;
  result.kind = LyricsResultKind::Ready;
  result.title = metadata.title;
  result.artist = metadata.artist;
  result.album = metadata.album;
  result.sourceName = sourceName;
  result.synchronizedLines = lyrics_internal::parseSynchronizedLyrics(lyrics);
  result.plainText = lyrics_internal::plainTextFromLyrics(lyrics);
  return result;
}

void mergeTranslation(QVector<LyricLine>& lines, const QString& translation) {
  const QVector<LyricLine> translated =
      lyrics_internal::parseSynchronizedLyrics(translation);
  if (translated.isEmpty()) {
    return;
  }
  for (auto& line : lines) {
    const auto match = std::find_if(
        translated.cbegin(), translated.cend(), [&line](const LyricLine& item) {
          return item.timeMilliseconds == line.timeMilliseconds;
        });
    if (match != translated.cend() && !match->text.isEmpty() &&
        match->text != line.text) {
      line.text += QLatin1Char('\n') + match->text;
    }
  }
}

}  // namespace

namespace lyrics_internal {

QVector<LyricLine> parseSynchronizedLyrics(const QString& lyrics) {
  static const QRegularExpression timestamp(
      QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[\\.:](\\d{1,3}))?\\]"));
  QVector<LyricLine> result;
  const QStringList rows =
      lyrics.split(QRegularExpression(QStringLiteral("\\r?\\n")));
  for (const QString& row : rows) {
    auto matches = timestamp.globalMatch(row);
    QVector<qint64> times;
    while (matches.hasNext()) {
      const QRegularExpressionMatch match = matches.next();
      const qint64 minutes = match.captured(1).toLongLong();
      const qint64 seconds = match.captured(2).toLongLong();
      const QString fractionText = match.captured(3);
      qint64 milliseconds = 0;
      if (fractionText.size() == 1) {
        milliseconds = fractionText.toLongLong() * 100;
      } else if (fractionText.size() == 2) {
        milliseconds = fractionText.toLongLong() * 10;
      } else if (fractionText.size() >= 3) {
        milliseconds = fractionText.left(3).toLongLong();
      }
      times.push_back((minutes * 60 + seconds) * 1000 + milliseconds);
    }
    QString text = row;
    text.remove(timestamp);
    text = text.trimmed();
    if (times.isEmpty() || text.isEmpty()) {
      continue;
    }
    for (const qint64 time : times) {
      result.push_back({time, text});
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const LyricLine& left, const LyricLine& right) {
                     return left.timeMilliseconds < right.timeMilliseconds;
                   });
  return result;
}

QString plainTextFromLyrics(const QString& lyrics) {
  static const QRegularExpression timestamp(
      QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[\\.:](\\d{1,3}))?\\]"));
  static const QRegularExpression metadata(
      QStringLiteral("^\\[(?:ar|al|ti|by|offset|re|ve):.*\\]$"),
      QRegularExpression::CaseInsensitiveOption);
  QStringList lines;
  for (QString row :
       lyrics.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
    row.remove(timestamp);
    row = row.trimmed();
    if (!row.isEmpty() && !metadata.match(row).hasMatch()) {
      lines.push_back(row);
    }
  }
  return lines.join(QLatin1Char('\n'));
}

bool isAcceptableTrackMatch(const QString& expectedTitle,
                            const QString& expectedArtist,
                            const qint64 expectedDurationMilliseconds,
                            const QString& candidateTitle,
                            const QString& candidateArtist,
                            const qint64 candidateDurationMilliseconds) {
  const TrackMetadata expected{expectedTitle, expectedArtist, {}, {}};
  SearchCandidate candidate;
  candidate.title = candidateTitle;
  candidate.artist = candidateArtist;
  candidate.durationMilliseconds = candidateDurationMilliseconds;
  const int minimumScore = expectedArtist.trimmed().isEmpty() ? 100 : 145;
  return candidateScore(expected, expectedDurationMilliseconds, candidate) >=
         minimumScore;
}

QString bestKugouTrackIdentity(const QByteArray& payload,
                               const QString& expectedTitle,
                               const QString& expectedArtist,
                               const qint64 expectedDurationMilliseconds) {
  const TrackMetadata expected{expectedTitle, expectedArtist, {}, {}};
  return bestKugouSongCandidate(payload, expected, expectedDurationMilliseconds)
      .identity;
}

QString decodeKugouLyricsPayload(const QByteArray& payload) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  const QByteArray encoded =
      document.object().value(QStringLiteral("content")).toString().toLatin1();
  QString lyrics = QString::fromUtf8(QByteArray::fromBase64(encoded));
  lyrics.remove(QChar::ByteOrderMark);
  return lyrics;
}

}  // namespace lyrics_internal

class OnlineLyricsService::Impl final {
 public:
  explicit Impl(OnlineLyricsService& owner) : owner_(owner) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(kNetworkTimeoutMilliseconds);
    QObject::connect(&timeout_, &QTimer::timeout, &owner_, [this] {
      timedOut_ = true;
      if (currentReply_ != nullptr) {
        currentReply_->abort();
      }
    });
  }

  void requestLyrics(const LyricsQuery& query) {
    cancel();
    query_ = query;
    metadata_ = readTrackMetadata(query_);
    cachePath_ = cacheFilePath(metadata_, query_.durationMilliseconds);
    hasNetworkFailure_ = false;
    isFallbackUnavailable_ = false;

    if (!metadata_.embeddedLyrics.trimmed().isEmpty()) {
      LyricsResult result = readyResult(metadata_, QStringLiteral("音频内嵌"),
                                        metadata_.embeddedLyrics);
      if (!result.plainText.isEmpty() || !result.synchronizedLines.isEmpty()) {
        publish(std::move(result), false);
        return;
      }
    }

    LyricsResult result;
    if (cachedResult(cachePath_, result)) {
      publish(std::move(result), false);
      return;
    }

    if (metadata_.title.trimmed().isEmpty()) {
      finishWithoutLyrics();
      return;
    }
    beginKugouSongSearch();
  }

  void cancel() noexcept {
    ++generation_;
    timeout_.stop();
    if (currentReply_ != nullptr) {
      QObject::disconnect(currentReply_, nullptr, &owner_, nullptr);
      currentReply_->abort();
      currentReply_->deleteLater();
      currentReply_ = nullptr;
    }
  }

 private:
  enum class Stage {
    KugouSongSearch,
    KugouLyricsSearch,
    KugouLyricsDownload,
    NeteaseSearch,
    NeteaseLyrics,
    LrclibSearch,
    AudioDbSearch,
  };

  void beginKugouSongSearch() {
    QUrl url(QStringLiteral("http://mobilecdn.kugou.com/api/v3/search/song"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("keyword"),
                       QStringLiteral("%1 %2")
                           .arg(metadata_.artist,
                                titleWithoutTrailingQualifier(metadata_.title))
                           .trimmed());
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("20"));
    query.addQueryItem(QStringLiteral("showtype"), QStringLiteral("1"));
    url.setQuery(query);
    startGet(url, Stage::KugouSongSearch, false);
  }

  void beginKugouLyricsSearch() {
    QUrl url(QStringLiteral("http://lyrics.kugou.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ver"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("man"), QStringLiteral("yes"));
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("pc"));
    query.addQueryItem(QStringLiteral("keyword"),
                       QStringLiteral("%1 - %2").arg(matchedCandidate_.artist,
                                                     matchedCandidate_.title));
    query.addQueryItem(QStringLiteral("duration"),
                       QString::number(matchedCandidate_.durationMilliseconds));
    query.addQueryItem(QStringLiteral("hash"), matchedCandidate_.identity);
    url.setQuery(query);
    startGet(url, Stage::KugouLyricsSearch, false);
  }

  void beginKugouLyricsDownload() {
    QUrl url(QStringLiteral("http://lyrics.kugou.com/download"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ver"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("pc"));
    query.addQueryItem(QStringLiteral("id"),
                       QString::number(matchedLyricsCandidate_.id));
    query.addQueryItem(QStringLiteral("accesskey"),
                       matchedLyricsCandidate_.accessKey);
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("lrc"));
    query.addQueryItem(QStringLiteral("charset"), QStringLiteral("utf8"));
    url.setQuery(query);
    startGet(url, Stage::KugouLyricsDownload, false);
  }

  void beginNeteaseSearch() {
    QUrl url(QStringLiteral("%1://music.163.com/api/search/get/web")
                 .arg(supportsHttps_ ? QStringLiteral("https")
                                     : QStringLiteral("http")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"),
                       QStringLiteral("%1 %2")
                           .arg(titleWithoutTrailingQualifier(metadata_.title),
                                metadata_.artist)
                           .trimmed());
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    url.setQuery(query);
    startGet(url, Stage::NeteaseSearch, true);
  }

  void beginNeteaseLyrics(const qint64 songId) {
    QUrl url(QStringLiteral("%1://music.163.com/api/song/lyric")
                 .arg(supportsHttps_ ? QStringLiteral("https")
                                     : QStringLiteral("http")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), QString::number(songId));
    query.addQueryItem(QStringLiteral("lv"), QStringLiteral("-1"));
    query.addQueryItem(QStringLiteral("kv"), QStringLiteral("-1"));
    query.addQueryItem(QStringLiteral("tv"), QStringLiteral("-1"));
    url.setQuery(query);
    startGet(url, Stage::NeteaseLyrics, true);
  }

  void beginLrclibSearch() {
    if (!supportsHttps_) {
      isFallbackUnavailable_ = true;
      finishWithoutLyrics();
      return;
    }
    QUrl url(QStringLiteral("https://lrclib.net/api/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), metadata_.title);
    if (!metadata_.artist.isEmpty()) {
      query.addQueryItem(QStringLiteral("artist_name"), metadata_.artist);
    }
    if (!metadata_.album.isEmpty()) {
      query.addQueryItem(QStringLiteral("album_name"), metadata_.album);
    }
    url.setQuery(query);
    startGet(url, Stage::LrclibSearch, false);
  }

  void beginAudioDbSearch() {
    if (metadata_.artist.isEmpty()) {
      finishWithoutLyrics();
      return;
    }
    QUrl url(QStringLiteral(
        "https://www.theaudiodb.com/api/v1/json/123/searchtrack.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), metadata_.artist);
    query.addQueryItem(QStringLiteral("t"), metadata_.title);
    url.setQuery(query);
    startGet(url, Stage::AudioDbSearch, false);
  }

  void startGet(const QUrl& url, const Stage stage,
                const bool isNeteaseRequest) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "MediaHub/0.2 (Windows desktop player)");
    request.setRawHeader("Accept", "application/json");
    if (isNeteaseRequest) {
      request.setRawHeader("Referer", "https://music.163.com/");
    }
    timedOut_ = false;
    const quint64 requestGeneration = generation_;
    QNetworkReply* const reply = network_.get(request);
    currentReply_ = reply;
    timeout_.start();
    QObject::connect(reply, &QNetworkReply::finished, &owner_,
                     [this, reply, requestGeneration, stage] {
                       handleReply(reply, requestGeneration, stage);
                     });
  }

  void handleReply(QNetworkReply* const reply, const quint64 requestGeneration,
                   const Stage stage) {
    if (requestGeneration != generation_ || reply != currentReply_) {
      reply->deleteLater();
      return;
    }
    timeout_.stop();
    currentReply_ = nullptr;
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool failed =
        timedOut_ || reply->error() != QNetworkReply::NoError || status >= 400;
    const QByteArray payload = failed ? QByteArray{} : reply->readAll();
    if (timedOut_ || reply->error() != QNetworkReply::NoError ||
        status == 429 || status >= 500) {
      hasNetworkFailure_ = true;
    }
    reply->deleteLater();
    if (failed) {
      advanceAfter(stage);
      return;
    }

    switch (stage) {
      case Stage::KugouSongSearch:
        handleKugouSongSearch(payload);
        break;
      case Stage::KugouLyricsSearch:
        handleKugouLyricsSearch(payload);
        break;
      case Stage::KugouLyricsDownload:
        handleKugouLyricsDownload(payload);
        break;
      case Stage::NeteaseSearch:
        handleNeteaseSearch(payload);
        break;
      case Stage::NeteaseLyrics:
        handleNeteaseLyrics(payload);
        break;
      case Stage::LrclibSearch:
        handleLrclibSearch(payload);
        break;
      case Stage::AudioDbSearch:
        handleAudioDbSearch(payload);
        break;
    }
  }

  void handleKugouSongSearch(const QByteArray& payload) {
    SearchCandidate best =
        bestKugouSongCandidate(payload, metadata_, query_.durationMilliseconds);
    if (best.identity.isEmpty()) {
      beginNeteaseSearch();
      return;
    }
    matchedCandidate_ = best;
    beginKugouLyricsSearch();
  }

  void handleKugouLyricsSearch(const QByteArray& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      beginNeteaseSearch();
      return;
    }
    const QJsonArray candidates =
        document.object().value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty()) {
      beginNeteaseSearch();
      return;
    }
    const QJsonObject best = candidates.first().toObject();
    matchedLyricsCandidate_ = {};
    matchedLyricsCandidate_.id =
        best.value(QStringLiteral("id")).toVariant().toLongLong();
    matchedLyricsCandidate_.accessKey =
        best.value(QStringLiteral("accesskey")).toString();
    if (matchedLyricsCandidate_.id < 0 ||
        matchedLyricsCandidate_.accessKey.isEmpty()) {
      beginNeteaseSearch();
      return;
    }
    beginKugouLyricsDownload();
  }

  void handleKugouLyricsDownload(const QByteArray& payload) {
    const QString lyrics = lyrics_internal::decodeKugouLyricsPayload(payload);
    LyricsResult result =
        readyResult(metadata_, QStringLiteral("酷狗音乐"), lyrics);
    if (result.synchronizedLines.isEmpty()) {
      beginNeteaseSearch();
      return;
    }
    result.title = matchedCandidate_.title;
    result.artist = matchedCandidate_.artist;
    result.album = matchedCandidate_.album;
    publish(std::move(result), true);
  }

  void handleNeteaseSearch(const QByteArray& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      beginLrclibSearch();
      return;
    }
    const QJsonArray songs = document.object()
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("songs"))
                                 .toArray();
    SearchCandidate best;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto& value : songs) {
      const QJsonObject song = value.toObject();
      SearchCandidate candidate;
      candidate.id = song.value(QStringLiteral("id")).toVariant().toLongLong();
      candidate.title = song.value(QStringLiteral("name")).toString();
      candidate.artist = artistFromSong(song);
      candidate.album = albumFromSong(song);
      candidate.durationMilliseconds = durationFromSong(song);
      const int score =
          candidateScore(metadata_, query_.durationMilliseconds, candidate);
      if (score > bestScore) {
        bestScore = score;
        best = std::move(candidate);
      }
    }
    const int minimumScore = metadata_.artist.isEmpty() ? 100 : 145;
    if (best.id < 0 || bestScore < minimumScore) {
      beginLrclibSearch();
      return;
    }
    matchedCandidate_ = best;
    beginNeteaseLyrics(best.id);
  }

  void handleNeteaseLyrics(const QByteArray& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      beginLrclibSearch();
      return;
    }
    const QJsonObject object = document.object();
    const QString lyrics = object.value(QStringLiteral("lrc"))
                               .toObject()
                               .value(QStringLiteral("lyric"))
                               .toString();
    LyricsResult result =
        readyResult(metadata_, QStringLiteral("网易云音乐"), lyrics);
    if (result.plainText.isEmpty() && result.synchronizedLines.isEmpty()) {
      beginLrclibSearch();
      return;
    }
    result.title = matchedCandidate_.title;
    result.artist = matchedCandidate_.artist;
    result.album = matchedCandidate_.album;
    const QString translation = object.value(QStringLiteral("tlyric"))
                                    .toObject()
                                    .value(QStringLiteral("lyric"))
                                    .toString();
    mergeTranslation(result.synchronizedLines, translation);
    publish(std::move(result), true);
  }

  void handleLrclibSearch(const QByteArray& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
      beginAudioDbSearch();
      return;
    }
    SearchCandidate best;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto& value : document.array()) {
      const QJsonObject item = value.toObject();
      SearchCandidate candidate;
      candidate.title = item.value(QStringLiteral("trackName")).toString();
      candidate.artist = item.value(QStringLiteral("artistName")).toString();
      candidate.album = item.value(QStringLiteral("albumName")).toString();
      candidate.durationMilliseconds = static_cast<qint64>(
          item.value(QStringLiteral("duration")).toDouble() * 1000.0);
      candidate.lyrics = item.value(QStringLiteral("plainLyrics")).toString();
      candidate.synchronizedLyrics =
          item.value(QStringLiteral("syncedLyrics")).toString();
      const int score =
          candidateScore(metadata_, query_.durationMilliseconds, candidate);
      if (score > bestScore && (!candidate.lyrics.isEmpty() ||
                                !candidate.synchronizedLyrics.isEmpty())) {
        bestScore = score;
        best = std::move(candidate);
      }
    }
    const int minimumScore = metadata_.artist.isEmpty() ? 100 : 145;
    if (bestScore < minimumScore) {
      beginAudioDbSearch();
      return;
    }

    LyricsResult result;
    result.kind = LyricsResultKind::Ready;
    result.title = best.title;
    result.artist = best.artist;
    result.album = best.album;
    result.sourceName = QStringLiteral("LRCLIB");
    result.synchronizedLines = lyrics_internal::parseSynchronizedLyrics(
        best.synchronizedLyrics.isEmpty() ? best.lyrics
                                          : best.synchronizedLyrics);
    result.plainText = lyrics_internal::plainTextFromLyrics(best.lyrics);
    if (result.plainText.isEmpty()) {
      result.plainText =
          lyrics_internal::plainTextFromLyrics(best.synchronizedLyrics);
    }
    publish(std::move(result), true);
  }

  void handleAudioDbSearch(const QByteArray& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      finishWithoutLyrics();
      return;
    }
    const QJsonArray tracks =
        document.object().value(QStringLiteral("track")).toArray();
    SearchCandidate best;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto& value : tracks) {
      const QJsonObject track = value.toObject();
      SearchCandidate candidate;
      candidate.title = track.value(QStringLiteral("strTrack")).toString();
      candidate.artist = track.value(QStringLiteral("strArtist")).toString();
      candidate.album = track.value(QStringLiteral("strAlbum")).toString();
      candidate.durationMilliseconds =
          track.value(QStringLiteral("intDuration")).toVariant().toLongLong();
      candidate.lyrics =
          track.value(QStringLiteral("strTrackLyrics")).toString();
      const int score =
          candidateScore(metadata_, query_.durationMilliseconds, candidate);
      if (score > bestScore && !candidate.lyrics.trimmed().isEmpty()) {
        bestScore = score;
        best = std::move(candidate);
      }
    }
    const int minimumScore = metadata_.artist.isEmpty() ? 100 : 145;
    if (bestScore < minimumScore) {
      finishWithoutLyrics();
      return;
    }
    TrackMetadata matchedMetadata{best.title, best.artist, best.album, {}};
    publish(
        readyResult(matchedMetadata, QStringLiteral("TheAudioDB"), best.lyrics),
        true);
  }

  void advanceAfter(const Stage stage) {
    switch (stage) {
      case Stage::KugouSongSearch:
      case Stage::KugouLyricsSearch:
      case Stage::KugouLyricsDownload:
        beginNeteaseSearch();
        break;
      case Stage::NeteaseSearch:
      case Stage::NeteaseLyrics:
        beginLrclibSearch();
        break;
      case Stage::LrclibSearch:
        beginAudioDbSearch();
        break;
      case Stage::AudioDbSearch:
        finishWithoutLyrics();
        break;
    }
  }

  void finishWithoutLyrics() {
    LyricsResult result;
    result.kind = hasNetworkFailure_ ? LyricsResultKind::Error
                                     : LyricsResultKind::NotFound;
    result.title = metadata_.title;
    result.artist = metadata_.artist;
    result.album = metadata_.album;
    result.message =
        hasNetworkFailure_ ? QStringLiteral("网络暂不可用，请稍后重试")
        : isFallbackUnavailable_
            ? QStringLiteral("网易云未匹配，当前环境暂时无法访问备用歌词源")
            : QStringLiteral("已查询所有歌词源，仍没有匹配结果");
    publish(std::move(result), false);
  }

  void publish(LyricsResult result, const bool shouldCache) {
    if (shouldCache && result.kind == LyricsResultKind::Ready) {
      saveCachedResult(cachePath_, result);
    }
    const quint64 requestGeneration = generation_;
    QTimer::singleShot(
        0, &owner_,
        [this, requestGeneration, result = std::move(result)]() mutable {
          if (requestGeneration == generation_) {
            emit owner_.resultReady(std::move(result));
          }
        });
  }

  OnlineLyricsService& owner_;
  QNetworkAccessManager network_;
  QTimer timeout_;
  QNetworkReply* currentReply_{nullptr};
  LyricsQuery query_;
  TrackMetadata metadata_;
  SearchCandidate matchedCandidate_;
  SearchCandidate matchedLyricsCandidate_;
  QString cachePath_;
  quint64 generation_{0};
  bool timedOut_{false};
  bool hasNetworkFailure_{false};
  bool supportsHttps_{QSslSocket::supportsSsl()};
  bool isFallbackUnavailable_{false};
};

OnlineLyricsService::OnlineLyricsService(QObject* const parent)
    : LyricsService(parent), impl_(std::make_unique<Impl>(*this)) {}

OnlineLyricsService::~OnlineLyricsService() { cancel(); }

void OnlineLyricsService::requestLyrics(const LyricsQuery& query) {
  impl_->requestLyrics(query);
}

void OnlineLyricsService::cancel() noexcept { impl_->cancel(); }

}  // namespace mediahub::gui
