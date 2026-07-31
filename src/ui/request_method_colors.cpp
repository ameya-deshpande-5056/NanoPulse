#include "request_method_colors.h"

#include <QBrush>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace {
class RequestMethodItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto foreground = index.data(Qt::ForegroundRole);
        if (!foreground.canConvert<QBrush>())
            return;

        const auto brush = foreground.value<QBrush>();
        option->palette.setBrush(QPalette::Text, brush);
        option->palette.setBrush(QPalette::HighlightedText, brush);
    }
};

QColor methodColor(const QString &method, bool dark) {
    if (method == QStringLiteral("GET"))
        return QColor(dark ? QStringLiteral("#81c995")
                           : QStringLiteral("#137333"));
    if (method == QStringLiteral("POST"))
        return QColor(dark ? QStringLiteral("#8ab4f8")
                           : QStringLiteral("#1967d2"));
    if (method == QStringLiteral("PUT"))
        return QColor(dark ? QStringLiteral("#fdd663")
                           : QStringLiteral("#b06000"));
    if (method == QStringLiteral("PATCH"))
        return QColor(dark ? QStringLiteral("#d7aefb")
                           : QStringLiteral("#8430ce"));
    if (method == QStringLiteral("DELETE"))
        return QColor(dark ? QStringLiteral("#f28b82")
                           : QStringLiteral("#c5221f"));
    if (method == QStringLiteral("HEAD"))
        return QColor(dark ? QStringLiteral("#bdc1c6")
                           : QStringLiteral("#5f6368"));
    if (method == QStringLiteral("OPTIONS"))
        return QColor(dark ? QStringLiteral("#c58af9")
                           : QStringLiteral("#7627bb"));
    return {};
}
}

void RequestMethodColors::installDelegate(QComboBox *combo) {
    combo->setItemDelegate(new RequestMethodItemDelegate(combo));
}

void RequestMethodColors::apply(QComboBox *combo, bool dark, int methodRole) {
    for (int index = 0; index < combo->count(); ++index) {
        const auto method = methodRole == Qt::DisplayRole
            ? combo->itemText(index)
            : combo->itemData(index, methodRole).toString();
        const auto color = methodColor(method, dark);
        combo->setItemData(index, color.isValid() ? QVariant(QBrush(color))
                                                  : QVariant(),
                           Qt::ForegroundRole);
    }
}
