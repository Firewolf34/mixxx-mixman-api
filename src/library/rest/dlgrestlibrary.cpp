#include "library/rest/dlgrestlibrary.h"

#include <QColor>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QSpinBox>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/ui_dlgrestlibrary.h"
#include "moc_dlgrestlibrary.cpp"
#include "util/assert.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace mixxx::library::rest {

DlgRestLibrary::DlgRestLibrary(
        WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        RestLibraryTableModel* pTableModel,
        KeyboardEventFilter* pKeyboard)
        : QWidget(parent),
          m_ui(std::make_unique<Ui::DlgRestLibrary>()),
          m_pTrackTableView(new WTrackTableView(
                  this,
                  std::move(pConfig),
                  pLibrary,
                  parent->getTrackTableBackgroundColorOpacity())),
          m_pTableModel(pTableModel) {
    m_ui->setupUi(this);

    m_pTrackTableView->installEventFilter(pKeyboard);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgRestLibrary::loadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgRestLibrary::loadTrackToPlayer);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgRestLibrary::trackSelected);

    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);

    connect(m_ui->pushButtonRefresh,
            &QPushButton::clicked,
            this,
            &DlgRestLibrary::refreshRequested);
    connect(m_ui->checkBoxFollowCurrentTrack,
            &QCheckBox::toggled,
            this,
            &DlgRestLibrary::followCurrentTrackChanged);
    connect(m_ui->comboBoxPolicyPreset,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                if (index >= 0) {
                    emit policyPresetChanged(
                            m_ui->comboBoxPolicyPreset->itemData(index).toString());
                }
            });
    connect(m_ui->checkBoxTargetEnergy,
            &QCheckBox::toggled,
            this,
            [this](bool checked) {
                m_ui->horizontalSliderTargetEnergy->setEnabled(checked);
                emit targetEnergyChanged(checked, m_ui->horizontalSliderTargetEnergy->value());
            });
    connect(m_ui->horizontalSliderTargetEnergy,
            &QSlider::valueChanged,
            this,
            [this](int value) {
                emit targetEnergyChanged(m_ui->checkBoxTargetEnergy->isChecked(), value);
            });
    connect(m_ui->checkBoxTargetColor,
            &QCheckBox::toggled,
            this,
            [this](bool checked) {
                m_ui->pushButtonTargetColor->setEnabled(checked);
                emit targetColorChanged(checked, m_targetColor);
            });
    connect(m_ui->pushButtonTargetColor,
            &QPushButton::clicked,
            this,
            &DlgRestLibrary::slotChooseTargetColor);
    connect(m_ui->checkBoxTargetBpm,
            &QCheckBox::toggled,
            this,
            [this](bool checked) {
                m_ui->spinBoxTargetBpm->setEnabled(checked);
                emit targetBpmChanged(checked, m_ui->spinBoxTargetBpm->value());
            });
    connect(m_ui->spinBoxTargetBpm,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            [this](int value) {
                emit targetBpmChanged(m_ui->checkBoxTargetBpm->isChecked(), value);
            });
    connect(m_ui->pushButtonReroll,
            &QPushButton::clicked,
            this,
            &DlgRestLibrary::rerollRequested);

    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) {
    } else {
        const int placeholderIndex = box->indexOf(m_ui->m_pTrackTablePlaceholder);
        box->removeWidget(m_ui->m_pTrackTablePlaceholder);
        m_ui->m_pTrackTablePlaceholder->hide();
        box->insertWidget(placeholderIndex, m_pTrackTableView);
    }

    m_pTrackTableView->loadTrackModel(m_pTableModel);
    m_ui->horizontalSliderTargetEnergy->setEnabled(false);
    m_ui->pushButtonTargetColor->setEnabled(false);
    m_ui->spinBoxTargetBpm->setEnabled(false);
    m_targetColor = QStringLiteral("#ffffff");
    updateTargetColorButton();
    setStatusText(tr("Select or play a track to load REST recommendations."));
}

DlgRestLibrary::~DlgRestLibrary() = default;

void DlgRestLibrary::onSearch(const QString& text) {
    m_pTableModel->search(text);
}

void DlgRestLibrary::onShow() {
}

bool DlgRestLibrary::hasFocus() const {
    return m_pTrackTableView->hasFocus();
}

void DlgRestLibrary::setFocus() {
    m_pTrackTableView->setFocus();
}

void DlgRestLibrary::saveCurrentViewState() {
    m_pTrackTableView->saveCurrentViewState();
}

bool DlgRestLibrary::restoreCurrentViewState() {
    return m_pTrackTableView->restoreCurrentViewState();
}

QString DlgRestLibrary::currentSearch() const {
    return m_pTableModel->currentSearch();
}

bool DlgRestLibrary::isFollowingCurrentTrack() const {
    return m_ui->checkBoxFollowCurrentTrack->isChecked();
}

void DlgRestLibrary::setStatusText(const QString& statusText) {
    m_ui->labelStatus->setText(statusText);
}

void DlgRestLibrary::setDiagnosticsText(const QString& diagnosticsText) {
    m_ui->labelDiagnostics->setText(diagnosticsText);
}

void DlgRestLibrary::setPathSummaryText(const QString& pathSummaryText) {
    m_ui->labelPathSummary->setText(pathSummaryText);
}

void DlgRestLibrary::setPolicyPresets(
        const QList<RestLibraryPolicyPreset>& presets,
        const QString& currentPreset) {
    const QSignalBlocker blocker(m_ui->comboBoxPolicyPreset);
    m_ui->comboBoxPolicyPreset->clear();
    if (presets.isEmpty()) {
        m_ui->comboBoxPolicyPreset->addItem(tr("DJ Assist"), QStringLiteral("dj_assist"));
    } else {
        for (const auto& preset : presets) {
            m_ui->comboBoxPolicyPreset->addItem(
                    preset.label.isEmpty() ? preset.key : preset.label,
                    preset.key);
        }
    }

    const int presetIndex = m_ui->comboBoxPolicyPreset->findData(currentPreset);
    if (presetIndex >= 0) {
        m_ui->comboBoxPolicyPreset->setCurrentIndex(presetIndex);
    }
}

void DlgRestLibrary::setMixManTargets(
        bool targetEnergyEnabled,
        int targetEnergy,
        bool targetColorEnabled,
        const QString& targetColor,
        bool targetBpmEnabled,
        int targetBpm) {
    {
        const QSignalBlocker blocker(m_ui->checkBoxTargetEnergy);
        m_ui->checkBoxTargetEnergy->setChecked(targetEnergyEnabled);
    }
    {
        const QSignalBlocker blocker(m_ui->horizontalSliderTargetEnergy);
        m_ui->horizontalSliderTargetEnergy->setValue(targetEnergy);
    }
    m_ui->horizontalSliderTargetEnergy->setEnabled(targetEnergyEnabled);

    {
        const QSignalBlocker blocker(m_ui->checkBoxTargetColor);
        m_ui->checkBoxTargetColor->setChecked(targetColorEnabled);
    }
    m_targetColor = targetColor.trimmed().isEmpty() ? QStringLiteral("#ffffff") : targetColor;
    m_ui->pushButtonTargetColor->setEnabled(targetColorEnabled);
    updateTargetColorButton();

    {
        const QSignalBlocker blocker(m_ui->checkBoxTargetBpm);
        m_ui->checkBoxTargetBpm->setChecked(targetBpmEnabled);
    }
    {
        const QSignalBlocker blocker(m_ui->spinBoxTargetBpm);
        m_ui->spinBoxTargetBpm->setValue(targetBpm);
    }
    m_ui->spinBoxTargetBpm->setEnabled(targetBpmEnabled);
}

void DlgRestLibrary::slotChooseTargetColor() {
    const QColor initialColor(m_targetColor);
    const QColor color = QColorDialog::getColor(
            initialColor.isValid() ? initialColor : QColor(Qt::white),
            this,
            tr("Select Target Color"));
    if (!color.isValid()) {
        return;
    }
    m_targetColor = color.name(QColor::HexRgb);
    updateTargetColorButton();
    emit targetColorChanged(m_ui->checkBoxTargetColor->isChecked(), m_targetColor);
}

void DlgRestLibrary::updateTargetColorButton() {
    const QColor color(m_targetColor);
    m_ui->pushButtonTargetColor->setStyleSheet(
            color.isValid()
                    ? QStringLiteral("background-color: %1;").arg(color.name(QColor::HexRgb))
                    : QString());
    m_ui->pushButtonTargetColor->setToolTip(m_targetColor);
}

} // namespace mixxx::library::rest
