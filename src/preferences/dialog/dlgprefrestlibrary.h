#pragma once

#include <QNetworkAccessManager>
#include <QStringList>

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
    void slotConnectionDiagnosticUpdated(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);
    void slotConnectionTestFinished(bool success);

  private:
    bool isInputValid() const;
    bool hasValidBaseUrl() const;
    bool hasValidRemoteIdTemplate(const QString& pathTemplate) const;
    mixxx::library::rest::RestLibrarySettings settingsFromUi() const;
    QString formatDiagnostic(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic) const;
    void appendConnectionTestLine(const QString& line);
    void writeSettings();

    Ui::DlgPrefRestLibraryDlg* m_pUi;
    UserSettingsPointer m_pConfig;
    QNetworkAccessManager m_networkAccessManager;
    mixxx::library::rest::RestLibraryClient m_connectionTestClient;
    QStringList m_connectionTestLines;
};
