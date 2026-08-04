#pragma once

#include <QNetworkAccessManager>

#include "library/rest/restlibraryclient.h"
#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class QWidget;

namespace Ui {
class DlgPrefRestLibraryDlg;
} // namespace Ui

class DlgPrefRestLibrary : public DlgPreferencePage {
    Q_OBJECT
  public:
    DlgPrefRestLibrary(QWidget* pParent, UserSettingsPointer pConfig);
    ~DlgPrefRestLibrary() override;

    bool okayToClose() const override;

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotCancel() override;
    void slotResetToDefaults() override;

  private slots:
    void slotBrowseCacheDirectory();
    void slotUpdateCacheControls(bool enabled);
    void slotUpdateMixManDefaultsControls(bool enabled);
    void slotUpdateValidationState();
    void slotTestConnection();
    void slotCancelConnectionTest();
    void slotConnectionDiagnosticUpdated(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);
    void slotConnectionTestFinished(bool success);
    void slotConnectionResultSelected();
    void slotMarkConnectionTestStale();

  private:
    bool isInputValid() const;
    QString validationMessage() const;
    bool hasValidBaseUrl() const;
    bool hasValidRemoteIdTemplate(const QString& pathTemplate) const;
    mixxx::library::rest::RestLibrarySettings settingsFromUi() const;
    void addConnectionDiagnostic(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);
    void addConnectionStatusRow(
            const QString& stage,
            const QString& result,
            const QString& summary);
    QString diagnosticDetails(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic) const;
    void clearValidationToolTips();
    void setConnectionTestRunning(bool running);
    void writeSettings();

    Ui::DlgPrefRestLibraryDlg* m_pUi;
    UserSettingsPointer m_pConfig;
    QNetworkAccessManager m_networkAccessManager;
    mixxx::library::rest::RestLibraryClient m_connectionTestClient;
    bool m_connectionTestRunning = false;
    bool m_connectionTestHasResults = false;
    bool m_connectionTestStale = false;
};
