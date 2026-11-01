#include "FilterBar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

FilterBar::FilterBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("FilterBar"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#FilterBar { background: #eef2f7; border-bottom: 1px solid #d0d7e2; }"));

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 8);
    auto *title = new QLabel(tr("<b>Filter</b><br><span style='color:#5a6b80'>all conditions</span>"));
    title->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    title->setMinimumWidth(90);
    outer->addWidget(title);

    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setSpacing(4);
    m_rowsLayout->addStretch();               // rows are inserted above this, so they stay at the top
    outer->addLayout(m_rowsLayout, 1);

    auto *buttons = new QVBoxLayout;
    auto *apply = new QPushButton(tr("Apply filter"));
    apply->setDefault(true);
    auto *add = new QPushButton(tr("+ Condition"));
    auto *clearBtn = new QPushButton(tr("Clear"));
    for (auto *b : {apply, add, clearBtn}) b->setMinimumWidth(110);
    buttons->addWidget(apply);
    buttons->addWidget(add);
    buttons->addWidget(clearBtn);
    buttons->addStretch();
    outer->addLayout(buttons);

    connect(apply, &QPushButton::clicked, this, &FilterBar::applyRequested);
    connect(add, &QPushButton::clicked, this, [this] { addCondition(); });
    connect(clearBtn, &QPushButton::clicked, this, [this] { clear(); emit clearRequested(); });
    addCondition();
}

void FilterBar::setColumns(const QStringList &columns)
{
    m_columns = columns;
    clear();
}

void FilterBar::addCondition(int column, csv::Op op, const QString &value)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto *col = new QComboBox;
    col->addItem(tr("Any column"), -1);
    for (int i = 0; i < m_columns.size(); ++i) col->addItem(m_columns.at(i), i);
    col->setCurrentIndex(col->findData(column));
    col->setMinimumWidth(170);
    auto *opBox = new QComboBox;
    opBox->addItems(csv::operatorNames());
    opBox->setCurrentIndex(int(op));
    opBox->setMinimumWidth(150);
    auto *edit = new QLineEdit(value);
    edit->setPlaceholderText(tr("value"));
    edit->setClearButtonEnabled(true);
    auto *remove = new QToolButton;
    remove->setText(QStringLiteral("✕"));
    remove->setToolTip(tr("Remove condition"));
    h->addWidget(col);
    h->addWidget(opBox);
    h->addWidget(edit, 1);
    h->addWidget(remove);
    m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, w);
    m_rows.append({w, col, opBox, edit});

    connect(edit, &QLineEdit::returnPressed, this, &FilterBar::applyRequested);
    connect(opBox, &QComboBox::currentIndexChanged, edit, [edit](int i) {
        edit->setEnabled(csv::operatorNeedsValue(csv::Op(i)));
    });
    connect(remove, &QToolButton::clicked, this, [this, w] { removeRow(w); });
    edit->setEnabled(csv::operatorNeedsValue(op));
}

void FilterBar::removeRow(QWidget *widget)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].widget != widget) continue;
        if (m_rows.size() == 1) {               // keep one row, just empty it
            m_rows[i].value->clear();
            m_rows[i].column->setCurrentIndex(0);
            m_rows[i].op->setCurrentIndex(0);
            return;
        }
        m_rows.removeAt(i);
        widget->deleteLater();
        return;
    }
}

void FilterBar::editColumn(int column)
{
    if (m_rows.isEmpty() || !m_rows.last().value->text().isEmpty()) addCondition(column);
    Row &r = m_rows.last();
    r.column->setCurrentIndex(r.column->findData(column));
    r.value->setFocus();
}

void FilterBar::clear()
{
    for (const Row &r : m_rows) r.widget->deleteLater();
    m_rows.clear();
    addCondition();
}

std::vector<csv::Condition> FilterBar::conditions() const
{
    std::vector<csv::Condition> out;
    for (const Row &r : m_rows) {
        csv::Condition c;
        c.column = r.column->currentData().toInt();
        c.op = csv::Op(r.op->currentIndex());
        c.value = r.value->text();
        if (csv::operatorNeedsValue(c.op) && c.value.isEmpty()) continue;   // empty row: ignore
        out.push_back(c);
    }
    return out;
}
