#pragma once

#include <QTextCursor>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class TextSearchBar : public QWidget {
    Q_OBJECT
public:
    explicit TextSearchBar(QPlainTextEdit *editor, bool canReplace,
                           QWidget *parent = nullptr);
    void showFind(bool showReplace = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void closeFind();
    void findNext(bool backward = false);
    void replaceSelection();
    void replaceAll();
    void refreshMatches(bool selectFirst = false);
    void activateMatch(int index);
    void updateHighlights();
    void updateMatchCount();
    void positionOverlay();

    QPlainTextEdit *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace = nullptr;
    QLabel *m_matchCount;
    QPushButton *m_case;
    QPushButton *m_wholeWord;
    QPushButton *m_regularExpression;
    QVector<QTextCursor> m_matches;
    int m_activeMatch = -1;
    bool m_canReplace;
};
