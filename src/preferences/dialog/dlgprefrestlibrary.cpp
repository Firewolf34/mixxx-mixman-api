#include "preferences/dialog/dlgprefrestlibrary.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <utility>

#include "library/rest/restlibrarysettings.h"
#include "moc_dlgprefrestlibrary.cpp"
#include "preferences/dialog/ui_dlgprefrestlibrarydlg.h"

namespace {

using mixxx::library::rest::RestLibrarySettings;
using mixxx::library::rest::RestLibraryClient;
namespace restConfig = mixxx::library::rest::config;

constexpr int kConnectionDetailRole = Qt::UserRole;

bool isEmptyOrRemoteIdTemplate(const QString& pathTemplate) {
    return pathTemplate.trimmed().isEmpty() || pathTemplate.contains(QStringLiteral("%1"));
}

bool isValidPathOrAbsoluteUrl(const QString& value) {
    QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    trimmed.replace(QStringLiteral("%1"), QStringLiteral("1"));
    trimmed.replace(QStringLiteral("%artist"), QStringLiteral("artist"));
    trimmed.replace(QStringLiteral("%title"), QStringLiteral("title"));
    trimmed.replace(QStringLiteral("%duration"), QStringLiteral("duration"));
    trimmed.replace(QStringLiteral("%location"), QStringLiteral("location"));
    const QUrl url(trimmed);
    return url.isValid();
}

} // namespace

DlgPrefRestLibrary::DlgPrefRestLibrary(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent),
          m_pUi(new Ui::DlgPrefRestLibraryDlg),
          m_pConfig(std::move(pConfig)),
          m_connectionTestClient(&m_networkAccessManager, this) {
    m_pUi->setupUi(this);

    m_pUi->spinBoxPageSize->setRange(restConfig::kMinPageSize, restConfig::kMaxPageSize);
    m_pUi->spinBoxRecommendationLimit->setRange(
            restConfig::kMinRecommendationLimit,
            restConfig::kMaxRecommendationLimit);
    m_pUi->spinBoxMixManPathDepth->setRange(
            restConfig::kMinMixManPathDepth,
            restConfig::kMaxMixManPathDepth);
    m_pUi->spinBoxCacheMaxMegabytes->setRange(
            restConfig::kMinCacheMaxMegabytes,
            restConfig::kMaxCacheMaxMegabytes);
    m_pUi->spinBoxCacheMaxAgeDays->setRange(
            restConfig::kMinCacheMaxAgeDays,
            restConfig::kMaxCacheMaxAgeDays);
    m_pUi->spinBoxMaxConcurrentDownloads->setRange(
            restConfig::kMinMaxConcurrentDownloads,
            restConfig::kMaxMaxConcurrentDownloads);

    m_pUi->lineEditBearerToken->setEchoMode(QLineEdit::Password);

    connect(m_pUi->pushButtonBrowseCacheDirectory,
            &QPushButton::clicked,
            this,
            &DlgPrefRestLibrary::slotBrowseCacheDirectory);
    connect(m_pUi->pushButtonTestConnection,
            &QPushButton::clicked,
            this,
            &DlgPrefRestLibrary::slotTestConnection);
    connect(m_pUi->pushButtonCancelConnectionTest,
            &QPushButton::clicked,
            this,
            &DlgPrefRestLibrary::slotCancelConnectionTest);
    connect(m_pUi->checkBoxCacheEnabled,
            &QCheckBox::toggled,
            this,
            &DlgPrefRestLibrary::slotUpdateCacheControls);
    connect(m_pUi->checkBoxUseMixManDefaults,
            &QCheckBox::toggled,
            this,
            &DlgPrefRestLibrary::slotUpdateMixManDefaultsControls);

    const auto updateValidation = [this] {
        slotUpdateValidationState();
        slotMarkConnectionTestStale();
    };
    connect(m_pUi->checkBoxEnabled, &QCheckBox::toggled, this, updateValidation);
    connect(m_pUi->checkBoxUseMixManDefaults, &QCheckBox::toggled, this, updateValidation);
    connect(m_pUi->lineEditBaseUrl, &QLineEdit::textChanged, this, updateValidation);
    connect(m_pUi->lineEditBearerToken, &QLineEdit::textChanged, this, updateValidation);
    connect(m_pUi->lineEditTrackListPath, &QLineEdit::textChanged, this, updateValidation);
    connect(m_pUi->lineEditTrackDetailPathTemplate,
            &QLineEdit::textChanged,
            this,
            updateValidation);
    connect(m_pUi->lineEditTrackLookupPathTemplate,
            &QLineEdit::textChanged,
            this,
            updateValidation);
    connect(m_pUi->lineEditRecommendationPathTemplate,
            &QLineEdit::textChanged,
            this,
            updateValidation);
    connect(m_pUi->lineEditAudioDownloadPathTemplate,
            &QLineEdit::textChanged,
            this,
            updateValidation);
    connect(m_pUi->spinBoxPageSize,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            updateValidation);
    connect(m_pUi->spinBoxRecommendationLimit,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            updateValidation);
    connect(m_pUi->spinBoxMixManPathDepth,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            updateValidation);
    connect(m_pUi->checkBoxMixManAdminApprovedOnly,
            &QCheckBox::toggled,
            this,
            updateValidation);
    connect(m_pUi->checkBoxTestConnectionCreateSession,
            &QCheckBox::toggled,
            this,
            updateValidation);
    connect(&m_connectionTestClient,
            &RestLibraryClient::requestDiagnosticUpdated,
            this,
            &DlgPrefRestLibrary::slotConnectionDiagnosticUpdated);
    connect(&m_connectionTestClient,
            &RestLibraryClient::connectionTestFinished,
            this,
            &DlgPrefRestLibrary::slotConnectionTestFinished);
    connect(m_pUi->treeWidgetConnectionTestResults,
            &QTreeWidget::itemSelectionChanged,
            this,
            &DlgPrefRestLibrary::slotConnectionResultSelected);

    m_pUi->treeWidgetConnectionTestResults->header()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
    m_pUi->treeWidgetConnectionTestResults->header()->setStretchLastSection(true);
    setConnectionTestRunning(false);

    setScrollSafeGuardForAllInputWidgets(this);
    slotUpdate();
}

DlgPrefRestLibrary::~DlgPrefRestLibrary() {
    delete m_pUi;
}

bool DlgPrefRestLibrary::okayToClose() const {
    return isInputValid();
}

void DlgPrefRestLibrary::slotUpdate() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);

    m_pUi->checkBoxEnabled->setChecked(settings.enabled);
    m_pUi->lineEditBaseUrl->setText(m_pConfig->getValueString(restConfig::kBaseUrlKey));
    m_pUi->lineEditBearerToken->setText(
            m_pConfig->getValueString(restConfig::kLocalDevBearerTokenKey));
    m_pUi->spinBoxPageSize->setValue(settings.pageSize);
    m_pUi->checkBoxUseMixManDefaults->setChecked(settings.useMixManDefaults);
    m_pUi->spinBoxMixManPathDepth->setValue(settings.mixManPathDepth);
    m_pUi->checkBoxMixManAdminApprovedOnly->setChecked(settings.mixManAdminApprovedOnly);
    m_pUi->lineEditTrackListPath->setText(
            m_pConfig->getValueString(restConfig::kTrackListPathKey));
    m_pUi->lineEditTrackDetailPathTemplate->setText(
            m_pConfig->getValueString(restConfig::kTrackDetailPathTemplateKey));
    m_pUi->lineEditTrackLookupPathTemplate->setText(
            m_pConfig->getValueString(restConfig::kTrackLookupPathTemplateKey));
    m_pUi->lineEditRecommendationPathTemplate->setText(
            m_pConfig->getValueString(restConfig::kRecommendationPathTemplateKey));
    m_pUi->spinBoxRecommendationLimit->setValue(settings.recommendationLimit);
    m_pUi->lineEditAudioDownloadPathTemplate->setText(
            m_pConfig->getValueString(restConfig::kAudioDownloadPathTemplateKey));
    m_pUi->checkBoxCacheEnabled->setChecked(settings.cacheEnabled);
    m_pUi->lineEditCacheDirectory->setText(settings.cacheDirectoryPath);
    m_pUi->spinBoxCacheMaxMegabytes->setValue(settings.cacheMaxMegabytes);
    m_pUi->spinBoxCacheMaxAgeDays->setValue(settings.cacheMaxAgeDays);
    m_pUi->spinBoxMaxConcurrentDownloads->setValue(settings.maxConcurrentDownloads);

    slotUpdateCacheControls(settings.cacheEnabled);
    slotUpdateMixManDefaultsControls(settings.useMixManDefaults);
    slotUpdateValidationState();
}

void DlgPrefRestLibrary::slotApply() {
    slotUpdateValidationState();
    if (!isInputValid()) {
        return;
    }
    writeSettings();
}

void DlgPrefRestLibrary::slotCancel() {
    slotUpdate();
}

void DlgPrefRestLibrary::slotResetToDefaults() {
    m_pUi->checkBoxEnabled->setChecked(restConfig::kDefaultEnabled);
    m_pUi->lineEditBaseUrl->clear();
    m_pUi->lineEditBearerToken->clear();
    m_pUi->spinBoxPageSize->setValue(restConfig::kDefaultPageSize);
    m_pUi->checkBoxUseMixManDefaults->setChecked(restConfig::kDefaultUseMixManDefaults);
    m_pUi->spinBoxMixManPathDepth->setValue(restConfig::kDefaultMixManPathDepth);
    m_pUi->checkBoxMixManAdminApprovedOnly->setChecked(
            restConfig::kDefaultMixManAdminApprovedOnly);
    m_pUi->lineEditTrackListPath->clear();
    m_pUi->lineEditTrackDetailPathTemplate->clear();
    m_pUi->lineEditTrackLookupPathTemplate->clear();
    m_pUi->lineEditRecommendationPathTemplate->clear();
    m_pUi->spinBoxRecommendationLimit->setValue(restConfig::kDefaultRecommendationLimit);
    m_pUi->lineEditAudioDownloadPathTemplate->clear();
    m_pUi->checkBoxCacheEnabled->setChecked(restConfig::kDefaultCacheEnabled);
    m_pUi->lineEditCacheDirectory->setText(restConfig::defaultCacheDirectoryPath(m_pConfig));
    m_pUi->spinBoxCacheMaxMegabytes->setValue(restConfig::kDefaultCacheMaxMegabytes);
    m_pUi->spinBoxCacheMaxAgeDays->setValue(restConfig::kDefaultCacheMaxAgeDays);
    m_pUi->spinBoxMaxConcurrentDownloads->setValue(restConfig::kDefaultMaxConcurrentDownloads);

    slotUpdateCacheControls(restConfig::kDefaultCacheEnabled);
    slotUpdateMixManDefaultsControls(restConfig::kDefaultUseMixManDefaults);
    slotUpdateValidationState();
}

void DlgPrefRestLibrary::slotBrowseCacheDirectory() {
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
            this,
            tr("Select REST Library Cache Directory"),
            m_pUi->lineEditCacheDirectory->text());
    if (!selectedDirectory.isEmpty()) {
        m_pUi->lineEditCacheDirectory->setText(QDir::toNativeSeparators(selectedDirectory));
    }
}

void DlgPrefRestLibrary::slotUpdateCacheControls(bool enabled) {
    m_pUi->labelCacheDirectory->setEnabled(enabled);
    m_pUi->lineEditCacheDirectory->setEnabled(enabled);
    m_pUi->pushButtonBrowseCacheDirectory->setEnabled(enabled);
    m_pUi->labelCacheMaxMegabytes->setEnabled(enabled);
    m_pUi->spinBoxCacheMaxMegabytes->setEnabled(enabled);
    m_pUi->labelCacheMaxAgeDays->setEnabled(enabled);
    m_pUi->spinBoxCacheMaxAgeDays->setEnabled(enabled);
    m_pUi->labelMaxConcurrentDownloads->setEnabled(enabled);
    m_pUi->spinBoxMaxConcurrentDownloads->setEnabled(enabled);
}

void DlgPrefRestLibrary::slotUpdateMixManDefaultsControls(bool enabled) {
    m_pUi->labelTrackListPath->setEnabled(!enabled);
    m_pUi->lineEditTrackListPath->setEnabled(!enabled);
    m_pUi->labelTrackDetailPathTemplate->setEnabled(!enabled);
    m_pUi->lineEditTrackDetailPathTemplate->setEnabled(!enabled);
    m_pUi->labelTrackLookupPathTemplate->setEnabled(!enabled);
    m_pUi->lineEditTrackLookupPathTemplate->setEnabled(!enabled);
    m_pUi->labelRecommendationPathTemplate->setEnabled(!enabled);
    m_pUi->lineEditRecommendationPathTemplate->setEnabled(!enabled);
    m_pUi->labelAudioDownloadPathTemplate->setEnabled(!enabled);
    m_pUi->lineEditAudioDownloadPathTemplate->setEnabled(!enabled);
}

void DlgPrefRestLibrary::slotUpdateValidationState() {
    clearValidationToolTips();
    const QString message = validationMessage();
    m_pUi->labelValidationWarning->setText(message);
    m_pUi->labelValidationWarning->setVisible(!message.isEmpty());

    if (message.isEmpty() || !m_pUi->checkBoxEnabled->isChecked()) {
        return;
    }
    if (!hasValidBaseUrl()) {
        m_pUi->lineEditBaseUrl->setToolTip(message);
        return;
    }
    if (m_pUi->checkBoxUseMixManDefaults->isChecked()) {
        return;
    }
    if (m_pUi->lineEditTrackListPath->text().trimmed().isEmpty() ||
            !isValidPathOrAbsoluteUrl(m_pUi->lineEditTrackListPath->text())) {
        m_pUi->lineEditTrackListPath->setToolTip(message);
    } else if (!hasValidRemoteIdTemplate(m_pUi->lineEditTrackDetailPathTemplate->text()) ||
            !isValidPathOrAbsoluteUrl(m_pUi->lineEditTrackDetailPathTemplate->text())) {
        m_pUi->lineEditTrackDetailPathTemplate->setToolTip(message);
    } else if (!hasValidRemoteIdTemplate(m_pUi->lineEditRecommendationPathTemplate->text()) ||
            !isValidPathOrAbsoluteUrl(m_pUi->lineEditRecommendationPathTemplate->text())) {
        m_pUi->lineEditRecommendationPathTemplate->setToolTip(message);
    } else if (!hasValidRemoteIdTemplate(m_pUi->lineEditAudioDownloadPathTemplate->text()) ||
            !isValidPathOrAbsoluteUrl(m_pUi->lineEditAudioDownloadPathTemplate->text())) {
        m_pUi->lineEditAudioDownloadPathTemplate->setToolTip(message);
    }
}

void DlgPrefRestLibrary::slotTestConnection() {
    slotUpdateValidationState();
    m_pUi->treeWidgetConnectionTestResults->clear();
    m_pUi->plainTextEditConnectionTestDetails->clear();
    m_connectionTestHasResults = false;
    m_connectionTestStale = false;
    if (!isInputValid()) {
        mixxx::library::rest::RestLibraryRequestDiagnostic diagnostic;
        diagnostic.stage = tr("Configuration");
        diagnostic.method = QStringLiteral("-");
        diagnostic.url = m_pUi->lineEditBaseUrl->text().trimmed();
        diagnostic.success = false;
        diagnostic.summary = validationMessage();
        diagnostic.errorText = diagnostic.summary;
        addConnectionDiagnostic(diagnostic);
        setConnectionTestRunning(false);
        return;
    }

    setConnectionTestRunning(true);
    addConnectionStatusRow(
            tr("Connection test"),
            tr("RUNNING"),
            tr("Testing REST Library connection..."));
    m_connectionTestClient.testMixManConnection(
            settingsFromUi(),
            {},
            m_pUi->checkBoxTestConnectionCreateSession->isChecked());
}

void DlgPrefRestLibrary::slotCancelConnectionTest() {
    if (!m_connectionTestRunning) {
        return;
    }
    m_connectionTestClient.cancelMixManConnectionTest();
    setConnectionTestRunning(false);
    addConnectionStatusRow(
            tr("Connection test"),
            tr("CANCELED"),
            tr("Connection test canceled."));
}

void DlgPrefRestLibrary::slotConnectionDiagnosticUpdated(
        const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic) {
    addConnectionDiagnostic(diagnostic);
}

void DlgPrefRestLibrary::slotConnectionTestFinished(bool success) {
    if (!m_connectionTestRunning) {
        return;
    }
    setConnectionTestRunning(false);
    addConnectionStatusRow(
            tr("Connection test"),
            success ? tr("OK") : tr("FAIL"),
            success ? tr("Connection test passed.") : tr("Connection test failed."));
}

void DlgPrefRestLibrary::slotConnectionResultSelected() {
    const QList<QTreeWidgetItem*> selectedItems =
            m_pUi->treeWidgetConnectionTestResults->selectedItems();
    if (selectedItems.isEmpty()) {
        m_pUi->plainTextEditConnectionTestDetails->clear();
        return;
    }
    m_pUi->plainTextEditConnectionTestDetails->setPlainText(
            selectedItems.constFirst()->data(0, kConnectionDetailRole).toString());
}

void DlgPrefRestLibrary::slotMarkConnectionTestStale() {
    if (!m_connectionTestHasResults ||
            m_connectionTestRunning ||
            m_connectionTestStale) {
        return;
    }
    m_connectionTestStale = true;
    addConnectionStatusRow(
            tr("Settings"),
            tr("STALE"),
            tr("Settings changed since this test ran."));
}

bool DlgPrefRestLibrary::isInputValid() const {
    return validationMessage().isEmpty();
}

QString DlgPrefRestLibrary::validationMessage() const {
    if (!m_pUi->checkBoxEnabled->isChecked()) {
        return {};
    }
    const bool useMixManDefaults = m_pUi->checkBoxUseMixManDefaults->isChecked();
    if (!hasValidBaseUrl()) {
        return tr("Enter an absolute REST Library base URL, such as https://example.com.");
    }
    if (useMixManDefaults) {
        return {};
    }
    if (m_pUi->lineEditTrackListPath->text().trimmed().isEmpty()) {
        return tr("Enter a custom track list path.");
    }
    if (!isValidPathOrAbsoluteUrl(m_pUi->lineEditTrackListPath->text())) {
        return tr("Track list path is not a valid path or URL.");
    }
    if (!hasValidRemoteIdTemplate(m_pUi->lineEditTrackDetailPathTemplate->text())) {
        return tr("Track detail path must include %1 when configured.");
    }
    if (!isValidPathOrAbsoluteUrl(m_pUi->lineEditTrackDetailPathTemplate->text())) {
        return tr("Track detail path is not a valid path or URL.");
    }
    if (!hasValidRemoteIdTemplate(m_pUi->lineEditRecommendationPathTemplate->text())) {
        return tr("Recommendation path must include %1 when configured.");
    }
    if (!isValidPathOrAbsoluteUrl(m_pUi->lineEditRecommendationPathTemplate->text())) {
        return tr("Recommendation path is not a valid path or URL.");
    }
    if (!hasValidRemoteIdTemplate(m_pUi->lineEditAudioDownloadPathTemplate->text())) {
        return tr("Audio download path must include %1 when configured.");
    }
    if (!isValidPathOrAbsoluteUrl(m_pUi->lineEditAudioDownloadPathTemplate->text())) {
        return tr("Audio download path is not a valid path or URL.");
    }
    return {};
}

bool DlgPrefRestLibrary::hasValidBaseUrl() const {
    const QUrl url(m_pUi->lineEditBaseUrl->text().trimmed());
    return url.isValid() && !url.isEmpty() && !url.isRelative();
}

bool DlgPrefRestLibrary::hasValidRemoteIdTemplate(const QString& pathTemplate) const {
    return isEmptyOrRemoteIdTemplate(pathTemplate);
}

RestLibrarySettings DlgPrefRestLibrary::settingsFromUi() const {
    RestLibrarySettings settings;
    settings.enabled = m_pUi->checkBoxEnabled->isChecked();
    settings.baseUrl = QUrl(m_pUi->lineEditBaseUrl->text().trimmed());
    settings.bearerToken = m_pUi->lineEditBearerToken->text();
    settings.pageSize = m_pUi->spinBoxPageSize->value();
    settings.useMixManDefaults = m_pUi->checkBoxUseMixManDefaults->isChecked();
    settings.mixManPathDepth = m_pUi->spinBoxMixManPathDepth->value();
    settings.mixManAdminApprovedOnly = m_pUi->checkBoxMixManAdminApprovedOnly->isChecked();
    settings.recommendationLimit = m_pUi->spinBoxRecommendationLimit->value();
    if (settings.useMixManDefaults) {
        settings.trackListPath = restConfig::mixManTrackListPath();
        settings.trackDetailPathTemplate = restConfig::mixManTrackDetailPathTemplate();
        settings.trackLookupPathTemplate = restConfig::mixManTrackLookupPathTemplate();
        settings.recommendationPathTemplate = restConfig::mixManRecommendationPathTemplate();
        settings.audioDownloadPathTemplate = restConfig::mixManAudioDownloadPathTemplate();
    } else {
        settings.trackListPath = m_pUi->lineEditTrackListPath->text().trimmed();
        settings.trackDetailPathTemplate =
                m_pUi->lineEditTrackDetailPathTemplate->text().trimmed();
        settings.trackLookupPathTemplate =
                m_pUi->lineEditTrackLookupPathTemplate->text().trimmed();
        settings.recommendationPathTemplate =
                m_pUi->lineEditRecommendationPathTemplate->text().trimmed();
        settings.audioDownloadPathTemplate =
                m_pUi->lineEditAudioDownloadPathTemplate->text().trimmed();
    }
    return settings;
}

void DlgPrefRestLibrary::addConnectionDiagnostic(
        const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic) {
    auto* pItem = new QTreeWidgetItem(m_pUi->treeWidgetConnectionTestResults);
    pItem->setText(0, diagnostic.stage);
    pItem->setText(1, diagnostic.success ? tr("OK") : tr("FAIL"));
    pItem->setText(2, diagnostic.statusCode > 0
                    ? QString::number(diagnostic.statusCode)
                    : QStringLiteral("-"));
    pItem->setText(3, diagnostic.elapsedMillis > 0
                    ? tr("%1 ms").arg(diagnostic.elapsedMillis)
                    : QStringLiteral("-"));
    pItem->setText(4, diagnostic.summary);
    pItem->setData(0, kConnectionDetailRole, diagnosticDetails(diagnostic));
    m_connectionTestHasResults = true;
    m_pUi->treeWidgetConnectionTestResults->setCurrentItem(pItem);
}

void DlgPrefRestLibrary::addConnectionStatusRow(
        const QString& stage,
        const QString& result,
        const QString& summary) {
    auto* pItem = new QTreeWidgetItem(m_pUi->treeWidgetConnectionTestResults);
    pItem->setText(0, stage);
    pItem->setText(1, result);
    pItem->setText(2, QStringLiteral("-"));
    pItem->setText(3, QStringLiteral("-"));
    pItem->setText(4, summary);
    pItem->setData(0, kConnectionDetailRole, summary);
    m_connectionTestHasResults = true;
    m_pUi->treeWidgetConnectionTestResults->setCurrentItem(pItem);
}

QString DlgPrefRestLibrary::diagnosticDetails(
        const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic) const {
    QStringList parts;
    parts.append(diagnostic.summary);
    if (!diagnostic.method.isEmpty() && !diagnostic.url.isEmpty()) {
        parts.append(QStringLiteral("%1 %2").arg(diagnostic.method, diagnostic.url));
    }
    if (diagnostic.statusCode > 0) {
        parts.append(tr("HTTP %1").arg(diagnostic.statusCode));
    }
    if (diagnostic.networkError != static_cast<int>(QNetworkReply::NoError) &&
            !diagnostic.errorText.isEmpty()) {
        parts.append(tr("Network error: %1").arg(diagnostic.errorText));
    }
    if (diagnostic.elapsedMillis > 0) {
        parts.append(tr("%1 ms").arg(diagnostic.elapsedMillis));
    }
    if (!diagnostic.responseSnippet.isEmpty()) {
        parts.append(tr("Response: %1").arg(diagnostic.responseSnippet));
    }
    return parts.join(QLatin1Char('\n'));
}

void DlgPrefRestLibrary::clearValidationToolTips() {
    m_pUi->lineEditBaseUrl->setToolTip({});
    m_pUi->lineEditTrackListPath->setToolTip({});
    m_pUi->lineEditTrackDetailPathTemplate->setToolTip({});
    m_pUi->lineEditRecommendationPathTemplate->setToolTip({});
    m_pUi->lineEditAudioDownloadPathTemplate->setToolTip({});
}

void DlgPrefRestLibrary::setConnectionTestRunning(bool running) {
    m_connectionTestRunning = running;
    m_pUi->pushButtonTestConnection->setEnabled(!running);
    m_pUi->pushButtonCancelConnectionTest->setEnabled(running);
}

void DlgPrefRestLibrary::writeSettings() {
    m_pConfig->setValue(restConfig::kEnabledKey, m_pUi->checkBoxEnabled->isChecked());
    m_pConfig->setValue(restConfig::kBaseUrlKey, m_pUi->lineEditBaseUrl->text().trimmed());
    m_pConfig->setValue(restConfig::kLocalDevBearerTokenKey, m_pUi->lineEditBearerToken->text());
    m_pConfig->setValue(restConfig::kPageSizeKey, m_pUi->spinBoxPageSize->value());
    m_pConfig->setValue(
            restConfig::kUseMixManDefaultsKey,
            m_pUi->checkBoxUseMixManDefaults->isChecked());
    m_pConfig->setValue(restConfig::kMixManPathDepthKey, m_pUi->spinBoxMixManPathDepth->value());
    m_pConfig->setValue(
            restConfig::kMixManAdminApprovedOnlyKey,
            m_pUi->checkBoxMixManAdminApprovedOnly->isChecked());
    m_pConfig->setValue(
            restConfig::kTrackListPathKey,
            m_pUi->lineEditTrackListPath->text().trimmed());
    m_pConfig->setValue(
            restConfig::kTrackDetailPathTemplateKey,
            m_pUi->lineEditTrackDetailPathTemplate->text().trimmed());
    m_pConfig->setValue(
            restConfig::kTrackLookupPathTemplateKey,
            m_pUi->lineEditTrackLookupPathTemplate->text().trimmed());
    m_pConfig->setValue(
            restConfig::kRecommendationPathTemplateKey,
            m_pUi->lineEditRecommendationPathTemplate->text().trimmed());
    m_pConfig->setValue(
            restConfig::kRecommendationLimitKey,
            m_pUi->spinBoxRecommendationLimit->value());
    m_pConfig->setValue(
            restConfig::kAudioDownloadPathTemplateKey,
            m_pUi->lineEditAudioDownloadPathTemplate->text().trimmed());
    m_pConfig->setValue(restConfig::kCacheEnabledKey, m_pUi->checkBoxCacheEnabled->isChecked());
    m_pConfig->setValue(
            restConfig::kCacheDirectoryKey,
            QDir::fromNativeSeparators(m_pUi->lineEditCacheDirectory->text().trimmed()));
    m_pConfig->setValue(
            restConfig::kCacheMaxMegabytesKey,
            m_pUi->spinBoxCacheMaxMegabytes->value());
    m_pConfig->setValue(restConfig::kCacheMaxAgeDaysKey, m_pUi->spinBoxCacheMaxAgeDays->value());
    m_pConfig->setValue(
            restConfig::kMaxConcurrentDownloadsKey,
            m_pUi->spinBoxMaxConcurrentDownloads->value());
}
