#include "text_search_bar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTextEdit>

TextSearchBar::TextSearchBar(QPlainTextEdit *editor, bool canReplace,
                             QWidget *parent)
    : QWidget(parent), m_editor(editor), m_canReplace(canReplace) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Find"));
    m_matchCount = new QLabel(this);
    auto *previous = new QPushButton(tr("Previous"), this);
    auto *next = new QPushButton(tr("Next"), this);
    m_case = new QPushButton(QStringLiteral("Aa"), this);
    m_wholeWord = new QPushButton(QStringLiteral("\\b"), this);
    m_regularExpression = new QPushButton(QStringLiteral(".*"), this);
    for (auto *option : {m_case, m_wholeWord, m_regularExpression})
        option->setCheckable(true);
    m_case->setToolTip(tr("Match Case"));
    m_wholeWord->setToolTip(tr("Match Whole Word"));
    m_regularExpression->setToolTip(tr("Use Regular Expression"));
    layout->addWidget(m_find, 1);
    layout->addWidget(m_matchCount);
    layout->addWidget(m_case);
    layout->addWidget(m_wholeWord);
    layout->addWidget(m_regularExpression);
    layout->addWidget(previous);
    layout->addWidget(next);
    if (m_canReplace) {
        m_replace = new QLineEdit(this);
        m_replace->setPlaceholderText(tr("Replace"));
        auto *replace = new QPushButton(tr("Replace"), this);
        auto *replaceAll = new QPushButton(tr("Replace All"), this);
        layout->addWidget(m_replace, 1);
        layout->addWidget(replace);
        layout->addWidget(replaceAll);
        connect(replace, &QPushButton::clicked,
                this, &TextSearchBar::replaceSelection);
        connect(replaceAll, &QPushButton::clicked,
                this, &TextSearchBar::replaceAll);
    }
    auto *close = new QPushButton(tr("Close"), this);
    layout->addWidget(close);

    connect(m_find, &QLineEdit::textChanged, this,
            [this] { refreshMatches(true); });
    connect(m_find, &QLineEdit::returnPressed, this,
            [this] { findNext(); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (isVisible())
            refreshMatches(true);
    });
    for (auto *option : {m_case, m_wholeWord, m_regularExpression})
        connect(option, &QPushButton::toggled, this,
                [this] { refreshMatches(true); });
    connect(previous, &QPushButton::clicked, this,
            [this] { findNext(true); });
    connect(next, &QPushButton::clicked, this,
            [this] { findNext(); });
    connect(close, &QPushButton::clicked, this, &TextSearchBar::closeFind);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &TextSearchBar::closeFind);
    auto *findShortcut = new QShortcut(QKeySequence::Find, m_editor);
    findShortcut->setContext(Qt::WidgetShortcut);
    connect(findShortcut, &QShortcut::activated, this,
            [this] { showFind(); });
    if (m_canReplace) {
        auto *replaceShortcut = new QShortcut(QKeySequence::Replace, m_editor);
        replaceShortcut->setContext(Qt::WidgetShortcut);
        connect(replaceShortcut, &QShortcut::activated, this,
                [this] { showFind(true); });
        auto *windowsReplace = new QShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_H), m_editor);
        windowsReplace->setContext(Qt::WidgetShortcut);
        connect(windowsReplace, &QShortcut::activated, this,
                [this] { showFind(true); });
    }
    m_editor->viewport()->installEventFilter(this);
    hide();
}

void TextSearchBar::showFind(bool showReplace) {
    Q_UNUSED(showReplace)
    show();
    positionOverlay();
    refreshMatches(true);
    m_find->setFocus();
    m_find->selectAll();
}

bool TextSearchBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_editor->viewport() && event->type() == QEvent::Resize
        && isVisible())
        positionOverlay();
    return QWidget::eventFilter(watched, event);
}

void TextSearchBar::closeFind() {
    hide();
    m_matches.clear();
    m_activeMatch = -1;
    m_editor->setExtraSelections({});
    m_editor->setFocus();
}

void TextSearchBar::findNext(bool backward) {
    refreshMatches();
    if (m_matches.isEmpty())
        return;
    const int count = m_matches.size();
    const int next = m_activeMatch < 0 ? 0
        : (m_activeMatch + (backward ? count - 1 : 1)) % count;
    activateMatch(next);
}

void TextSearchBar::replaceSelection() {
    if (m_activeMatch < 0 || m_activeMatch >= m_matches.size())
        return;
    auto cursor = m_matches.at(m_activeMatch);
    {
        QSignalBlocker blocker(m_editor);
        cursor.insertText(m_replace->text());
    }
    refreshMatches(true);
    findNext();
}

void TextSearchBar::replaceAll() {
    refreshMatches();
    if (m_matches.isEmpty())
        return;
    auto cursor = m_editor->textCursor();
    cursor.beginEditBlock();
    {
        QSignalBlocker blocker(m_editor);
        for (int index = m_matches.size() - 1; index >= 0; --index) {
            auto replacementCursor = m_matches.at(index);
            replacementCursor.insertText(m_replace->text());
        }
    }
    cursor.endEditBlock();
    refreshMatches(true);
}

void TextSearchBar::refreshMatches(bool selectFirst) {
    const int selectedStart = m_editor->textCursor().selectionStart();
    m_matches.clear();
    m_activeMatch = -1;
    const auto text = m_find->text();
    if (text.isEmpty()) {
        updateHighlights();
        updateMatchCount();
        return;
    }
    auto expressionText = m_regularExpression->isChecked()
        ? text : QRegularExpression::escape(text);
    if (m_wholeWord->isChecked())
        expressionText = QStringLiteral("\\b(?:%1)\\b").arg(expressionText);
    QRegularExpression expression(expressionText);
    if (!m_case->isChecked())
        expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid()) {
        updateHighlights();
        m_matchCount->setText(tr("Invalid regular expression"));
        return;
    }
    auto matches = expression.globalMatch(m_editor->toPlainText());
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedLength() == 0)
            continue;
        QTextCursor cursor(m_editor->document());
        cursor.setPosition(match.capturedStart());
        cursor.setPosition(match.capturedEnd(), QTextCursor::KeepAnchor);
        if (cursor.selectionStart() == selectedStart)
            m_activeMatch = m_matches.size();
        m_matches.append(cursor);
    }
    if (m_activeMatch < 0 && selectFirst && !m_matches.isEmpty())
        m_activeMatch = 0;
    if (m_activeMatch >= 0)
        m_editor->setTextCursor(m_matches.at(m_activeMatch));
    updateHighlights();
    updateMatchCount();
}

void TextSearchBar::activateMatch(int index) {
    m_activeMatch = index;
    m_editor->setTextCursor(m_matches.at(index));
    m_editor->centerCursor();
    updateHighlights();
    updateMatchCount();
}

void TextSearchBar::updateHighlights() {
    QList<QTextEdit::ExtraSelection> selections;
    auto inactiveColor = palette().color(QPalette::Highlight);
    inactiveColor.setAlpha(100);
    const auto activeColor = palette().color(QPalette::Highlight);
    for (int index = 0; index < m_matches.size(); ++index) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = m_matches.at(index);
        selection.format.setBackground(index == m_activeMatch ? activeColor
                                                              : inactiveColor);
        if (index == m_activeMatch)
            selection.format.setForeground(palette().color(QPalette::HighlightedText));
        selections.append(selection);
    }
    m_editor->setExtraSelections(selections);
}

void TextSearchBar::updateMatchCount() {
    if (m_find->text().isEmpty()) {
        m_matchCount->clear();
        return;
    }
    m_matchCount->setText(tr("%1 of %2").arg(m_activeMatch + 1)
                           .arg(m_matches.size()));
}

void TextSearchBar::positionOverlay() {
    adjustSize();
    const int x = qMax(8, m_editor->geometry().right() - width() - 8);
    const int y = m_editor->geometry().top() + 8;
    move(x, y);
    raise();
}
