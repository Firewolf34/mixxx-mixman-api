#pragma once

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

  private:
    bool isInputValid() const;
    bool hasValidBaseUrl() const;
    bool hasValidRemoteIdTemplate(const QString& pathTemplate) const;
    void writeSettings();

    Ui::DlgPrefRestLibraryDlg* m_pUi;
    UserSettingsPointer m_pConfig;
};
