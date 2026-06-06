#pragma once

#include <QUrl>

#include "preferences/usersettings.h"

namespace mixxx::library::rest {

class RestLibrarySettings final {
  public:
    static RestLibrarySettings fromConfig(const UserSettingsPointer& pConfig);

    bool isConfigured() const;
    bool enabled = false;
    bool cacheEnabled = true;
    QUrl baseUrl;
    QString bearerToken;
    QString trackListPath;
    QString trackDetailPathTemplate;
    QString trackLookupPathTemplate;
    QString recommendationPathTemplate;
    QString audioDownloadPathTemplate;
    QString cacheDirectoryPath;
    int pageSize = 50;
    int recommendationLimit = 5;
    int cacheMaxMegabytes = 1024;
    int cacheMaxAgeDays = 30;
    int maxConcurrentDownloads = 2;

    bool hasAudioDownloadConfigured() const;
    bool hasTrackLookupConfigured() const;
    bool hasRecommendationsConfigured() const;
};

} // namespace mixxx::library::rest
