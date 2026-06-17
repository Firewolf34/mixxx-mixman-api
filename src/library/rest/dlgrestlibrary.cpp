#include "library/rest/dlgrestlibrary.h"

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

    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) {
    } else {
        box->removeWidget(m_ui->m_pTrackTablePlaceholder);
        m_ui->m_pTrackTablePlaceholder->hide();
        box->insertWidget(1, m_pTrackTableView);
    }

    m_pTrackTableView->loadTrackModel(m_pTableModel);
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

} // namespace mixxx::library::rest
