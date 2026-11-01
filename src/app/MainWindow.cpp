#include "MainWindow.h"

#include "ExportDialog.h"
#include "FilterBar.h"
#include "core/RowFilter.h"
#include "core/RowSorter.h"
#include "core/SampleGenerator.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <numeric>

namespace {

const QLocale kEn(QLocale::English);   // "5,000,000" style numbers everywhere

QString count(qint64 n) { return kEn.toString(n); }

QString fileSizeText(qint64 bytes)
{
    if (bytes >= 1000LL * 1000 * 1000) return QStringLiteral("%1 GB").arg(bytes / 1e9, 0, 'f', 2);
    if (bytes >= 1000LL * 1000) return QStringLiteral("%1 MB").arg(bytes / 1e6, 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(std::max<qint64>(1, bytes / 1000));
}

QString seconds(double s) { return QStringLiteral("%1 s").arg(s, 0, 'f', s < 10 ? 2 : 1); }

QString delimiterName(char d)
{
    switch (d) {
    case ',': return QStringLiteral("comma");
    case ';': return QStringLiteral("semicolon");
    case '\t': return QStringLiteral("tab");
    case '|': return QStringLiteral("pipe");
    default: return QString(QChar(d));
    }
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Large CSV Viewer"));
    setAcceptDrops(true);
    m_model = new CsvTableModel(this);
    connect(&m_doc, &csv::CsvDocument::rowsAdded, this, &MainWindow::onRowsAdded);
    connect(&m_doc, &csv::CsvDocument::indexFinished, this, &MainWindow::onIndexFinished);
    buildUi();
    buildMenus();
    updateRecentMenu();
    setBusy(false);
    resize(1280, 760);
}

MainWindow::~MainWindow()
{
    m_job.reset();          // stop a running task before the document goes away
    m_doc.close();
}

// ---------------------------------------------------------------------------
// User interface
// ---------------------------------------------------------------------------

void MainWindow::buildUi()
{
    // Welcome page (no file open)
    auto *welcome = new QWidget;
    auto *wl = new QVBoxLayout(welcome);
    auto *hello = new QLabel(tr("<div style='font-size:20px;font-weight:600;color:#1f3a5f'>Large CSV Viewer</div>"
                                "<p style='color:#4a5a6e'>Open CSV files with millions of rows in seconds.<br>"
                                "Search, filter, sort and export the result to Excel.</p>"
                                "<p style='color:#7a8696'>Drop a .csv file here or click Open.</p>"));
    hello->setAlignment(Qt::AlignCenter);
    auto *openBtn = new QPushButton(tr("Open CSV file…"));
    auto *sampleBtn = new QPushButton(tr("Create a sample file (5 million rows)…"));
    openBtn->setMinimumWidth(300);
    sampleBtn->setMinimumWidth(300);
    wl->addStretch();
    wl->addWidget(hello);
    wl->addSpacing(12);
    wl->addWidget(openBtn, 0, Qt::AlignHCenter);
    wl->addWidget(sampleBtn, 0, Qt::AlignHCenter);
    wl->addStretch();
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openDialog);
    connect(sampleBtn, &QPushButton::clicked, this, &MainWindow::createSample);

    // Table page
    auto *page = new QWidget;
    auto *pl = new QVBoxLayout(page);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    m_filterBar = new FilterBar;
    m_filterBar->hide();
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    QHeaderView *vh = m_table->verticalHeader();
    vh->setSectionResizeMode(QHeaderView::Fixed);         // fixed row height: smooth with millions of rows
    vh->setDefaultSectionSize(fontMetrics().height() + 8);
    vh->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("0,000,000")) + 12);
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setHighlightSections(false);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setToolTip(tr("Click a column header to sort"));
    pl->addWidget(m_filterBar);
    pl->addWidget(m_table, 1);

    m_stack = new QStackedWidget;
    m_stack->addWidget(welcome);
    m_stack->addWidget(page);
    setCentralWidget(m_stack);

    connect(hh, &QHeaderView::sectionClicked, this, &MainWindow::sortByColumn);
    hh->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hh, &QHeaderView::customContextMenuRequested, this, &MainWindow::headerMenu);
    connect(m_filterBar, &FilterBar::applyRequested, this, &MainWindow::applyFilter);
    connect(m_filterBar, &FilterBar::clearRequested, this, &MainWindow::clearFilterAndSort);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::updateRowDetails);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateRowDetails);

    // Row details (all columns of the current row, top to bottom)
    m_details = new QTableWidget(0, 2);
    m_details->setHorizontalHeaderLabels({tr("Column"), tr("Value")});
    m_details->horizontalHeader()->setStretchLastSection(true);
    m_details->verticalHeader()->hide();
    m_details->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailsDock = new QDockWidget(tr("Row details"), this);
    m_detailsDock->setObjectName(QStringLiteral("RowDetails"));
    m_detailsDock->setWidget(m_details);
    addDockWidget(Qt::RightDockWidgetArea, m_detailsDock);
    m_detailsDock->hide();

    // Status bar
    m_info = new QLabel(tr("No file open"));
    m_format = new QLabel;
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setMaximumWidth(220);
    m_progress->setTextVisible(false);
    m_cancel = new QToolButton;
    m_cancel->setText(tr("Cancel"));
    statusBar()->addWidget(m_info, 1);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(m_cancel);
    statusBar()->addPermanentWidget(m_format);
    m_progress->hide();
    m_cancel->hide();
    connect(m_cancel, &QToolButton::clicked, this, &MainWindow::cancelJob);
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(100);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::updateJobProgress);
}

void MainWindow::buildMenus()
{
    QStyle *st = style();
    auto *openAct = new QAction(st->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open…"), this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openDialog);
    m_exportAction = new QAction(st->standardIcon(QStyle::SP_DialogSaveButton), tr("&Export…"), this);
    m_exportAction->setShortcut(QKeySequence(tr("Ctrl+E")));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportRows);
    m_filterAction = new QAction(st->standardIcon(QStyle::SP_FileDialogContentsView), tr("&Filter"), this);
    m_filterAction->setCheckable(true);
    m_filterAction->setShortcut(QKeySequence(tr("Ctrl+Shift+F")));
    connect(m_filterAction, &QAction::toggled, m_filterBar, &QWidget::setVisible);
    m_clearAction = new QAction(st->standardIcon(QStyle::SP_DialogResetButton), tr("&Clear filter && sort"), this);
    connect(m_clearAction, &QAction::triggered, this, [this] {
        m_search->clear();
        m_filterBar->clear();
        clearFilterAndSort();
    });
    m_copyAction = new QAction(tr("&Copy"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelection);
    m_gotoAction = new QAction(tr("&Go to row…"), this);
    m_gotoAction->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(m_gotoAction, &QAction::triggered, this, &MainWindow::goToRow);

    // File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(openAct);
    m_recentMenu = file->addMenu(tr("Open &recent"));
    file->addAction(m_exportAction);
    file->addSeparator();
    file->addAction(tr("Create &sample CSV…"), this, &MainWindow::createSample);
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // Edit
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_copyAction);
    QAction *find = edit->addAction(tr("&Search…"), this, [this] { m_search->setFocus(); m_search->selectAll(); });
    find->setShortcut(QKeySequence::Find);
    edit->addAction(m_gotoAction);

    // View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_filterAction);
    view->addAction(m_clearAction);
    QAction *details = m_detailsDock->toggleViewAction();
    details->setShortcut(QKeySequence(Qt::Key_F4));
    view->addAction(details);

    // Format: override what was detected
    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    QMenu *delim = format->addMenu(tr("&Delimiter"));
    m_delimiterGroup = new QActionGroup(this);
    const QList<QPair<QString, char>> delimiters = {{tr("Detect automatically"), 0}, {tr("Comma  ,"), ','},
                                                    {tr("Semicolon  ;"), ';'}, {tr("Tab"), '\t'}, {tr("Pipe  |"), '|'}};
    for (const auto &[label, ch] : delimiters) {
        QAction *a = delim->addAction(label);
        a->setCheckable(true);
        a->setData(int(ch));
        a->setChecked(ch == 0);
        m_delimiterGroup->addAction(a);
    }
    QMenu *enc = format->addMenu(tr("&Encoding"));
    m_encodingGroup = new QActionGroup(this);
    const QList<QPair<QString, csv::Encoding>> encodings = {{tr("Detect automatically"), csv::Encoding::Auto},
                                                            {tr("UTF-8"), csv::Encoding::Utf8},
                                                            {tr("Windows-1252 (Western Europe)"), csv::Encoding::Windows1252}};
    for (const auto &[label, e] : encodings) {
        QAction *a = enc->addAction(label);
        a->setCheckable(true);
        a->setData(int(e));
        a->setChecked(e == csv::Encoding::Auto);
        m_encodingGroup->addAction(a);
    }
    m_headerAction = format->addAction(tr("First row is the &header"));
    m_headerAction->setCheckable(true);
    m_headerAction->setChecked(true);
    connect(m_delimiterGroup, &QActionGroup::triggered, this, &MainWindow::reloadWithOptions);
    connect(m_encodingGroup, &QActionGroup::triggered, this, &MainWindow::reloadWithOptions);
    connect(m_headerAction, &QAction::triggered, this, &MainWindow::reloadWithOptions);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::showAbout);

    // Toolbar
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("MainToolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(openAct);
    tb->addAction(m_exportAction);
    tb->addSeparator();
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search all columns…  (Enter)"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(340);
    m_search->setMaximumWidth(480);
    tb->addWidget(m_search);
    tb->addAction(m_filterAction);
    tb->addAction(m_clearAction);
    connect(m_search, &QLineEdit::returnPressed, this, &MainWindow::applyFilter);
    addAction(m_copyAction);
}

csv::OpenOptions MainWindow::currentOptions() const
{
    csv::OpenOptions o;
    o.delimiter = char(m_delimiterGroup->checkedAction()->data().toInt());
    o.encoding = csv::Encoding(m_encodingGroup->checkedAction()->data().toInt());
    o.hasHeader = m_headerAction->isChecked();
    return o;
}

// ---------------------------------------------------------------------------
// Opening files
// ---------------------------------------------------------------------------

void MainWindow::openDialog()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("lastDir"),
                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open CSV file"), dir,
                                                      tr("CSV and text files (*.csv *.tsv *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    // A new file: detect its format again.
    m_delimiterGroup->actions().first()->setChecked(true);
    m_encodingGroup->actions().first()->setChecked(true);
    openFile(path);
}

void MainWindow::reloadWithOptions()
{
    if (m_doc.isOpen()) openFile(m_doc.path());
}

void MainWindow::openFile(const QString &path)
{
    m_job.reset();
    m_jobDone = nullptr;
    setBusy(false);
    m_openTimer.start();
    m_model->setDocument(nullptr);
    QString error;
    if (!m_doc.open(path, currentOptions(), &error)) {
        QMessageBox::warning(this, tr("Open file"), error);
        m_stack->setCurrentIndex(0);
        updateStatus();
        return;
    }
    m_filtered.reset();
    m_sortColumn = -1;
    m_viewInfo.clear();
    m_model->setDocument(&m_doc);
    for (int c = 0; c < m_doc.columnCount(); ++c) m_table->showColumn(c);
    m_table->horizontalHeader()->setSortIndicatorShown(false);
    m_filterBar->setColumns(m_doc.headers());
    m_stack->setCurrentIndex(1);
    setWindowTitle(tr("%1 – Large CSV Viewer").arg(QFileInfo(path).fileName()));
    m_format->setText(QStringLiteral("%1 · %2 · %3")
                          .arg(m_doc.encoding() == csv::Encoding::Utf8 ? QStringLiteral("UTF-8")
                                                                       : QStringLiteral("Windows-1252"),
                               delimiterName(m_doc.delimiter()),
                               m_doc.hasHeader() ? tr("header row") : tr("no header")));
    addRecent(m_doc.path());
    m_progress->setValue(0);
    m_progress->show();
    m_widthsDone = false;
    setColumnWidths();
    updateStatus();
}

void MainWindow::onRowsAdded(qint64)
{
    m_progress->setValue(m_doc.indexProgress());
    if (!m_widthsDone && m_doc.rowCount() > 0) {   // first rows arrived: fit the columns to them
        setColumnWidths();
        m_widthsDone = true;
    }
    updateStatus();
}

void MainWindow::onIndexFinished(qint64, double)
{
    m_openSeconds = m_openTimer.nsecsElapsed() / 1e9;
    if (!m_job) m_progress->hide();
    setBusy(bool(m_job));
    updateStatus();
}

void MainWindow::setColumnWidths()
{
    // Width from the header and the first 200 rows (no need to look at millions).
    const QFontMetrics fm = m_table->fontMetrics();
    const int rows = int(std::min<qint64>(m_doc.rowCount(), 200));
    std::vector<int> widths;
    for (int c = 0; c < m_doc.columnCount(); ++c)
        widths.push_back(fm.horizontalAdvance(m_doc.headers().value(c)) + 34);
    for (int r = 0; r < rows; ++r) {
        const QStringList values = m_doc.rowValues(r);
        for (int c = 0; c < values.size() && c < int(widths.size()); ++c)
            widths[size_t(c)] = std::max(widths[size_t(c)], fm.horizontalAdvance(values.at(c).section('\n', 0, 0)) + 20);
    }
    for (int c = 0; c < int(widths.size()); ++c) m_table->setColumnWidth(c, std::clamp(widths[size_t(c)], 60, 320));
}

void MainWindow::addRecent(const QString &path)
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("recent")).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 6) list.removeLast();
    s.setValue(QStringLiteral("recent"), list);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList list = QSettings().value(QStringLiteral("recent")).toStringList();
    for (const QString &p : list) {
        m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] { openFile(p); })->setToolTip(p);
    }
    m_recentMenu->setEnabled(!list.isEmpty());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString path = event->mimeData()->urls().first().toLocalFile();
    m_delimiterGroup->actions().first()->setChecked(true);
    m_encodingGroup->actions().first()->setChecked(true);
    openFile(path);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_job.reset();
    event->accept();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void MainWindow::updateStatus()
{
    if (!m_doc.isOpen()) {
        m_info->setText(tr("No file open"));
        return;
    }
    QString text = QStringLiteral("<b>%1</b>  ·  ").arg(QFileInfo(m_doc.path()).fileName().toHtmlEscaped());
    if (!m_doc.isIndexComplete()) {
        text += tr("Loading…  %1 rows so far (%2%) · %3")
                   .arg(count(m_doc.rowCount()))
                   .arg(m_doc.indexProgress() / 10)
                   .arg(fileSizeText(m_doc.fileSize()));
    } else {
        text += tr("%1 rows · %2 columns · %3 · opened in %4")
                   .arg(count(m_doc.rowCount()))
                   .arg(m_doc.columnCount())
                   .arg(fileSizeText(m_doc.fileSize()), seconds(m_openSeconds));
    }
    if (!m_viewInfo.isEmpty()) text += QStringLiteral("&nbsp;&nbsp;&nbsp;|&nbsp;&nbsp;&nbsp;") + m_viewInfo.toHtmlEscaped();
    m_info->setText(text);
}

void MainWindow::setBusy(bool busy)
{
    const bool ready = m_doc.isOpen() && m_doc.isIndexComplete() && !busy;
    m_exportAction->setEnabled(ready);
    m_clearAction->setEnabled(ready);
    m_gotoAction->setEnabled(m_doc.isOpen());
    m_search->setEnabled(m_doc.isOpen());
    m_filterAction->setEnabled(m_doc.isOpen());
    m_filterBar->setEnabled(ready);
    m_cancel->setVisible(busy);
}

// ---------------------------------------------------------------------------
// Background tasks
// ---------------------------------------------------------------------------

bool MainWindow::canRunJob() const
{
    if (m_job) return false;
    if (!m_doc.isOpen() || !m_doc.isIndexComplete()) {
        if (m_doc.isOpen()) const_cast<MainWindow *>(this)->statusBar()->showMessage(tr("Please wait until the file is loaded."), 3000);
        return false;
    }
    return true;
}

void MainWindow::startJob(std::unique_ptr<BackgroundJob> job)
{
    m_job = std::move(job);
    connect(m_job.get(), &BackgroundJob::finished, this, &MainWindow::jobFinished);
    m_progress->setValue(0);
    m_progress->show();
    setBusy(true);
    m_info->setText(m_job->title() + QStringLiteral("…"));
    m_progressTimer->start();
    m_job->start();
}

void MainWindow::updateJobProgress()
{
    if (!m_job) return;
    m_progress->setValue(m_job->permille());
    m_info->setText(QStringLiteral("%1…  %2%").arg(m_job->title()).arg(m_job->permille() / 10));
}

void MainWindow::cancelJob()
{
    if (m_job) m_job->requestCancel();
}

void MainWindow::jobFinished()
{
    m_progressTimer->stop();
    BackgroundJob *job = m_job.release();
    auto done = std::move(m_jobDone);
    m_jobDone = nullptr;
    m_progress->hide();
    setBusy(false);
    updateStatus();
    if (done) done();
    updateStatus();
    job->deleteLater();          // still inside its finished() signal
}

// ---------------------------------------------------------------------------
// Filter, sort
// ---------------------------------------------------------------------------

void MainWindow::applyFilter()
{
    std::vector<csv::Condition> conditions = m_filterBar->conditions();
    const QString search = m_search->text().trimmed();
    if (!search.isEmpty()) conditions.push_back({-1, csv::Op::Contains, search});
    if (conditions.empty()) {
        clearFilterAndSort();
        return;
    }
    if (!canRunJob()) return;
    const QString problem = csv::validateConditions(conditions);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("Filter"), problem);
        return;
    }

    auto result = std::make_shared<std::vector<quint32>>();
    auto *doc = &m_doc;
    auto job = std::make_unique<BackgroundJob>(tr("Filtering %1 rows").arg(count(m_doc.rowCount())), m_doc.rowCount(),
        [doc, conditions, result](BackgroundJob &j) { *result = csv::filterRows(*doc, conditions, j.done, j.cancel); });
    BackgroundJob *raw = job.get();
    m_jobDone = [this, raw, result, conditions] {
        if (raw->isCancelled()) {
            statusBar()->showMessage(tr("Filter cancelled"), 4000);
            return;
        }
        m_filtered = result;
        m_sortColumn = -1;
        m_table->horizontalHeader()->setSortIndicatorShown(false);
        m_model->setView(m_filtered);
        m_table->scrollToTop();
        QList<CsvTableModel::Highlight> highlights;
        for (const auto &c : conditions)
            if (c.op == csv::Op::Contains || c.op == csv::Op::Equals || c.op == csv::Op::StartsWith ||
                c.op == csv::Op::EndsWith)
                highlights.append({c.column, c.value.trimmed()});
        m_model->setHighlights(highlights);
        m_viewInfo = tr("Filter: %1 of %2 rows match · %3")
                         .arg(count(qint64(result->size())), count(m_doc.rowCount()), seconds(raw->seconds()));
    };
    startJob(std::move(job));
}

void MainWindow::clearFilterAndSort()
{
    if (m_job || !m_doc.isOpen()) return;
    m_filtered.reset();
    m_sortColumn = -1;
    m_table->horizontalHeader()->setSortIndicatorShown(false);
    m_model->setView(nullptr);
    m_table->scrollToTop();
    m_model->setHighlights({});
    m_viewInfo.clear();
    updateStatus();
}

void MainWindow::sortByColumn(int column)
{
    // Header click: first ascending, click again for descending.
    sortRows(column, column == m_sortColumn ? !m_sortAscending : true);
}

void MainWindow::sortRows(int column, bool ascending)
{
    QHeaderView *hh = m_table->horizontalHeader();
    if (!canRunJob()) {
        if (m_sortColumn >= 0) hh->setSortIndicator(m_sortColumn, m_sortAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
        else hh->setSortIndicatorShown(false);
        return;
    }
    RowList input = m_filtered;
    const qint64 n = input ? qint64(input->size()) : m_doc.rowCount();
    auto result = std::make_shared<csv::SortResult>();
    auto *doc = &m_doc;
    const QString name = m_doc.headers().value(column);
    auto job = std::make_unique<BackgroundJob>(tr("Sorting %1 rows by %2").arg(count(n), name), n,
        [doc, input, n, column, ascending, result](BackgroundJob &j) {
            std::vector<quint32> rows;
            if (input) {
                rows = *input;
            } else {
                rows.resize(size_t(n));
                std::iota(rows.begin(), rows.end(), 0u);
            }
            *result = csv::sortRows(*doc, std::move(rows), column, ascending, j.done, j.cancel);
        });
    BackgroundJob *raw = job.get();
    m_jobDone = [this, raw, result, column, ascending, name] {
        QHeaderView *header = m_table->horizontalHeader();
        if (raw->isCancelled()) {
            if (m_sortColumn < 0) header->setSortIndicatorShown(false);
            else header->setSortIndicator(m_sortColumn, m_sortAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
            statusBar()->showMessage(tr("Sort cancelled"), 4000);
            return;
        }
        m_sortColumn = column;
        m_sortAscending = ascending;
        header->setSortIndicatorShown(true);
        header->setSortIndicator(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        const qint64 rows = qint64(result->rows.size());
        m_model->setView(std::make_shared<const std::vector<quint32>>(std::move(result->rows)));
        m_table->scrollToTop();
        QString base = m_filtered ? m_viewInfo.section(QStringLiteral("  ·  sorted"), 0, 0) : QString();
        const QString sortText = tr("sorted by %1 (%2, %3) in %4")
                                     .arg(name, ascending ? tr("ascending") : tr("descending"),
                                          result->numeric ? tr("as numbers") : tr("as text"), seconds(raw->seconds()));
        m_viewInfo = base.isEmpty() ? tr("%1 rows ").arg(count(rows)) + sortText : base + QStringLiteral("  ·  ") + sortText;
    };
    startJob(std::move(job));
}

void MainWindow::headerMenu(const QPoint &pos)
{
    QHeaderView *hh = m_table->horizontalHeader();
    const int column = hh->logicalIndexAt(pos);
    if (column < 0 || !m_doc.isOpen()) return;
    const QString name = m_doc.headers().value(column);
    QMenu menu(this);
    menu.addAction(tr("Sort ascending"), this, [this, column] { sortRows(column, true); });
    menu.addAction(tr("Sort descending"), this, [this, column] { sortRows(column, false); });
    menu.addAction(tr("Filter by \"%1\"…").arg(name), this, [this, column] {
        m_filterAction->setChecked(true);
        m_filterBar->editColumn(column);
    });
    menu.addSeparator();
    QAction *hide = menu.addAction(tr("Hide column \"%1\"").arg(name), this, [this, column] {
        m_table->hideColumn(column);
    });
    int visible = 0;
    for (int c = 0; c < m_doc.columnCount(); ++c) visible += !m_table->isColumnHidden(c);
    hide->setEnabled(visible > 1);
    QMenu *show = menu.addMenu(tr("Show column"));
    for (int c = 0; c < m_doc.columnCount(); ++c)
        if (m_table->isColumnHidden(c))
            show->addAction(m_doc.headers().value(c), this, [this, c] { m_table->showColumn(c); });
    show->setEnabled(!show->isEmpty());
    menu.addAction(tr("Show all columns"), this, [this] {
        for (int c = 0; c < m_doc.columnCount(); ++c) m_table->showColumn(c);
    });
    menu.exec(hh->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Export, sample file
// ---------------------------------------------------------------------------

void MainWindow::exportRows()
{
    if (!canRunJob()) return;
    RowList view = m_model->view();
    const qint64 viewRows = view ? qint64(view->size()) : m_doc.rowCount();
    QList<int> hidden;
    for (int c = 0; c < m_doc.columnCount(); ++c)
        if (m_table->isColumnHidden(c)) hidden << c;
    ExportDialog dlg(m_doc.headers(), viewRows, m_doc.rowCount(), bool(view), m_doc.path(), this, hidden);
    if (dlg.exec() != QDialog::Accepted) return;
    const csv::ExportOptions opt = dlg.options();
    if (QFileInfo::exists(opt.path) &&
        QMessageBox::question(this, tr("Export"), tr("%1 already exists. Replace it?").arg(QFileInfo(opt.path).fileName())) !=
            QMessageBox::Yes)
        return;
    if (QFileInfo(opt.path).absoluteFilePath() == m_doc.path()) {
        QMessageBox::warning(this, tr("Export"), tr("Please choose a different file than the one that is open."));
        return;
    }

    RowList rows = dlg.exportView() ? view : nullptr;
    const qint64 total = rows ? qint64(rows->size()) : m_doc.rowCount();
    auto result = std::make_shared<csv::ExportResult>();
    auto *doc = &m_doc;
    auto job = std::make_unique<BackgroundJob>(tr("Exporting %1 rows").arg(count(total)), total,
        [doc, rows, opt, result](BackgroundJob &j) {
            *result = csv::exportRows(*doc, rows ? rows.get() : nullptr, opt, j.done, j.cancel);
        });
    BackgroundJob *raw = job.get();
    m_jobDone = [this, raw, result, opt] {
        if (result->cancelled) {
            statusBar()->showMessage(tr("Export cancelled – no file was written"), 5000);
            return;
        }
        if (!result->ok) {
            QMessageBox::critical(this, tr("Export failed"), result->error);
            return;
        }
        QMessageBox box(this);
        box.setWindowTitle(tr("Export finished"));
        box.setIcon(QMessageBox::Information);
        box.setText(tr("<b>%1 rows</b> exported in %2.").arg(count(result->rows), seconds(raw->seconds())));
        box.setInformativeText(QStringLiteral("%1\n%2").arg(QDir::toNativeSeparators(opt.path), fileSizeText(result->bytes)));
        QPushButton *openBtn = box.addButton(tr("Open file"), QMessageBox::AcceptRole);
        QPushButton *folderBtn = box.addButton(tr("Show folder"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == openBtn) QDesktopServices::openUrl(QUrl::fromLocalFile(opt.path));
        else if (box.clickedButton() == folderBtn) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(opt.path).absolutePath()));
    };
    startJob(std::move(job));
}

void MainWindow::createSample()
{
    if (m_job) return;
    bool ok = false;
    const int rows = QInputDialog::getInt(this, tr("Create sample CSV"),
        tr("Number of rows (fictional orders, about 150 bytes per row):"), 5000000, 1000, 100000000, 1000000, &ok);
    if (!ok) return;
    const QString name = rows % 1000000 == 0 ? QStringLiteral("sample_orders_%1M.csv").arg(rows / 1000000)
                                             : QStringLiteral("sample_orders_%1.csv").arg(rows);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save sample CSV"), dir + QLatin1Char('/') + name,
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    auto error = std::make_shared<QString>();
    auto okFlag = std::make_shared<bool>(false);
    auto job = std::make_unique<BackgroundJob>(tr("Creating %1 sample rows").arg(count(rows)), rows,
        [path, rows, error, okFlag](BackgroundJob &j) {
            *okFlag = csv::generateSample(path, rows, j.done, j.cancel, error.get());
        });
    BackgroundJob *raw = job.get();
    m_jobDone = [this, raw, path, error, okFlag] {
        if (raw->isCancelled()) return;
        if (!*okFlag) {
            QMessageBox::critical(this, tr("Create sample CSV"), *error);
            return;
        }
        openFile(path);
    };
    startJob(std::move(job));
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void MainWindow::copySelection()
{
    QModelIndexList sel = m_table->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    if (sel.size() > 200000) {
        QMessageBox::information(this, tr("Copy"), tr("Please select fewer cells (at most 200,000), or use Export."));
        return;
    }
    std::sort(sel.begin(), sel.end(), [](const QModelIndex &a, const QModelIndex &b) {
        return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
    });
    QString out;
    int row = sel.first().row();
    bool first = true;
    for (const QModelIndex &i : sel) {
        if (i.row() != row) { out += QLatin1Char('\n'); row = i.row(); first = true; }
        if (!first) out += QLatin1Char('\t');
        out += m_doc.rowValues(m_model->sourceRow(i.row())).value(i.column());
        first = false;
    }
    QApplication::clipboard()->setText(out);
    statusBar()->showMessage(tr("%1 cells copied").arg(count(sel.size())), 3000);
}

void MainWindow::goToRow()
{
    const int rows = m_model->rowCount();
    if (rows == 0) return;
    bool ok = false;
    const int r = QInputDialog::getInt(this, tr("Go to row"), tr("Row in the current view (1 – %1):").arg(count(rows)),
                                       1, 1, rows, 1, &ok);
    if (!ok) return;
    const QModelIndex idx = m_model->index(r - 1, 0);
    m_table->scrollTo(idx, QAbstractItemView::PositionAtTop);
    m_table->setCurrentIndex(idx);
}

void MainWindow::updateRowDetails()
{
    const QModelIndex cur = m_table->currentIndex();
    if (!cur.isValid() || !m_doc.isOpen()) {
        m_details->setRowCount(0);
        m_detailsDock->setWindowTitle(tr("Row details"));
        return;
    }
    const qint64 source = m_model->sourceRow(cur.row());
    const QStringList values = m_doc.rowValues(source);
    m_detailsDock->setWindowTitle(tr("Row %1").arg(count(source + 1)));
    m_details->setRowCount(m_doc.columnCount());
    for (int c = 0; c < m_doc.columnCount(); ++c) {
        m_details->setItem(c, 0, new QTableWidgetItem(m_doc.headers().value(c)));
        m_details->setItem(c, 1, new QTableWidgetItem(values.value(c)));
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Large CSV Viewer"),
        tr("<h3>Large CSV Viewer</h3>"
           "<p>Demo project. Opens CSV files with millions of rows: the file is memory-mapped and "
           "indexed in the background, only visible rows are parsed. Filtering and sorting use all CPU cores.</p>"
           "<p>C++17 · Qt %1</p>").arg(QString::fromLatin1(qVersion())));
}
