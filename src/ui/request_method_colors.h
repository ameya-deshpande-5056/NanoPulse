#pragma once

#include <Qt>

class QComboBox;

namespace RequestMethodColors {
void installDelegate(QComboBox *combo);
void apply(QComboBox *combo, bool dark, int methodRole = Qt::DisplayRole);
}
