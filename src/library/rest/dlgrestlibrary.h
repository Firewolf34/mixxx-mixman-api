#pragma once

#include <memory>

#include <QWidget>

#include "library/autodj/autodjprocessor.h"
#include "library/libraryview.h"
#include "library/rest/restlibrarymixman.h"
#include "library/rest/restlibrarytablemodel.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"

class KeyboardEventFilter;
class Library;
class WLibrary;
class WTrackTableView;

namespace Ui {
class DlgRestLibrary;
} // namespace Ui

namespace mixxx::library::rest {

class DlgRestLibrary final : public QWidget, public LibraryView {
    Q_OBJECT

  public:
    DlgRestLibrary(
            WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            RestLibraryTableModel* pTableModel,
            KeyboardEventFilter* pKeyboard);
    ~DlgRestLibrary() override;

    void onSearch(const QString& text) override;
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;

    QString currentSearch() const;
    bool isFollowingCurrentTrack() const;

  public slots:
    void setStatusText(const QString& statusText);
    void setDiagnosticsText(const QString& diagnosticsText);
    void setPathSummaryText(const QString& pathSummaryText);
    void setPolicyPresets(
            const QList<mixxx::library::rest::RestLibraryPolicyPreset>& presets,
            const QString& currentPreset);
    void setMixManTargets(
            bool targetEnergyEnabled,
            int targetEnergy,
            bool targetColorEnabled,
            const QString& targetColor,
            bool targetBpmEnabled,
            int targetBpm);
    void setAutoDJState(AutoDJProcessor::AutoDJState state);
    void setAutoDJPreparing(bool preparing);

  signals:
    void refreshRequested();
    void followCurrentTrackChanged(bool follow);
    void policyPresetChanged(const QString& presetKey);
    void targetEnergyChanged(bool enabled, int energy);
    void targetColorChanged(bool enabled, const QString& color);
    void targetBpmChanged(bool enabled, int bpm);
    void rerollRequested();
    void autoDJToggleRequested(bool enable);
    void autoDJFadeNowRequested();
    void autoDJSkipNextRequested();
    void loadTrack(TrackPointer pTrack);
    void loadTrackToPlayer(TrackPointer pTrack, const QString& group, bool play);
    void trackSelected(TrackPointer pTrack);

  private slots:
    void slotChooseTargetColor();

  private:
    void updateTargetEnergyValue(int value);
    void updateTargetColorButton();

    std::unique_ptr<Ui::DlgRestLibrary> m_ui;
    WTrackTableView* m_pTrackTableView;
    RestLibraryTableModel* const m_pTableModel;
    QString m_targetColor;
    const bool m_showButtonText;
};

} // namespace mixxx::library::rest
