#include "preferences/dialog/dlgprefrestlibrary.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <utility>

#include "library/rest/restlibrarysettings.h"
#include "moc_dlgprefrestlibrary.cpp"
#include "preferences/dialog/ui_dlgprefrestlibrarydlg.h"

namespace {

using mixxx::library::rest::RestLibrarySettings;
namespace restConfig = mixxx::library::rest::config;

bool isEmptyOrRemoteIdTemplate(const QString& pathTemplate) {
    return pathTemplate.trimmed().isEmpty() || pathTemplate.contains(QStringLiteral("%1"));
}

} // namespace

DlgPrefRestLibrary::DlgPrefRestLibrary(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent),
          m_pUi(new Ui::DlgPrefRestLibraryDlg),
          m_pConfig(std::move(pConfig)) {
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
    };
    connect(m_pUi->checkBoxEnabled, &QCheckBox::toggled, this, updateValidation);
    connect(m_pUi->checkBoxUseMixManDefaults, &QCheckBox::toggled, this, updateValidation);
    connect(m_pUi->lineEditBaseUrl, &QLineEdit::textChanged, this, updateValidation);
    connect(m_pUi->lineEditTrackListPath, &QLineEdit::textChanged, this, updateValidation);
    connect(m_pUi->lineEditTrackDetailPathTemplate,
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
    m_pUi->labelValidationWarning->setVisible(!isInputValid());
}

bool DlgPrefRestLibrary::isInputValid() const {
    if (!m_pUi->checkBoxEnabled->isChecked()) {
        return true;
    }
    const bool useMixManDefaults = m_pUi->checkBoxUseMixManDefaults->isChecked();
    return hasValidBaseUrl() &&
            (useMixManDefaults ||
                    (!m_pUi->lineEditTrackListPath->text().trimmed().isEmpty() &&
                            hasValidRemoteIdTemplate(
                                    m_pUi->lineEditTrackDetailPathTemplate->text()) &&
                            hasValidRemoteIdTemplate(
                                    m_pUi->lineEditRecommendationPathTemplate->text()) &&
                            hasValidRemoteIdTemplate(
                                    m_pUi->lineEditAudioDownloadPathTemplate->text())));
}

bool DlgPrefRestLibrary::hasValidBaseUrl() const {
    const QUrl url(m_pUi->lineEditBaseUrl->text().trimmed());
    return url.isValid() && !url.isEmpty() && !url.isRelative();
}

bool DlgPrefRestLibrary::hasValidRemoteIdTemplate(const QString& pathTemplate) const {
    return isEmptyOrRemoteIdTemplate(pathTemplate);
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
