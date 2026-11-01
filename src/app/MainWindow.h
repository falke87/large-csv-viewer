#pragma once

#include "BackgroundJob.h"
#include "CsvTableModel.h"
#include "core/CsvDocument.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <memory>

class FilterBar;
class QActionGroup;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QStackedWidget;
class QTableView;
class QTableWidget;
class QTimer;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void openDialog();
    void reloadWithOptions();
    void onRowsAdded(qint64 rows);
    void onIndexFinished(qint64 rows, double seconds);
    void applyFilter();
    void clearFilterAndSort();
    void sortByColumn(int column);
    void sortRows(int column, bool ascending);
    void headerMenu(const QPoint &pos);
    void exportRows();
    void createSample();
    void copySelection();
    void goToRow();
    void updateRowDetails();
    void updateRecentMenu();
    void addRecent(const QString &path);
    void showAbout();

    // Long tasks
    void startJob(std::unique_ptr<BackgroundJob> job);
    void jobFinished();
    void cancelJob();
    void updateJobProgress();
    bool canRunJob() const;

    void setBusy(bool busy);
    void updateStatus();
    void setColumnWidths();
    csv::OpenOptions currentOptions() const;

    csv::CsvDocument m_doc;
    CsvTableModel *m_model;
    RowList m_filtered;                 // result of the last filter (nullptr = no filter)
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    QString m_viewInfo;                 // "Filter: ... rows" text for the status bar

    std::unique_ptr<BackgroundJob> m_job;
    std::function<void()> m_jobDone;
    QElapsedTimer m_openTimer;
    double m_openSeconds = 0;
    bool m_widthsDone = false;

    QStackedWidget *m_stack;
    QTableView *m_table;
    FilterBar *m_filterBar;
    QLineEdit *m_search;
    QDockWidget *m_detailsDock;
    QTableWidget *m_details;
    QLabel *m_info, *m_format;
    QProgressBar *m_progress;
    QToolButton *m_cancel;
    QTimer *m_progressTimer;
    QMenu *m_recentMenu;
    QActionGroup *m_delimiterGroup, *m_encodingGroup;
    QAction *m_headerAction, *m_exportAction, *m_filterAction, *m_clearAction, *m_gotoAction, *m_copyAction;
};
