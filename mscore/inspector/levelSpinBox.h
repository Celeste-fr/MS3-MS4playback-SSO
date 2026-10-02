//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#ifndef __LEVEL_SPIN_BOX_H__
#define __LEVEL_SPIN_BOX_H__

namespace Ms {

//---------------------------------------------------------
//   LevelSpinBox
//    a level offset in dB ("+1.5 dB", "-3 dB") whose 0 reads "Library default" (no change: Inspector ›
//    Articulation › Marcato level, libmscore/articulation.h MarcatoLevel). No suffix (Qt would put it after
//    "Library default" too): the unit is in the text
//---------------------------------------------------------

class LevelSpinBox : public QDoubleSpinBox {
      // the number in a text ("+1.5 dB" -> "1.5"); empty: none
      static QString number(QString t)
            {
            t = t.trimmed();
            if (t.endsWith("dB", Qt::CaseInsensitive))
                  t.chop(2);
            t = t.trimmed();
            if (t.startsWith('+'))
                  t.remove(0, 1);
            return t.trimmed();
            }
      static bool isDefault(const QString& t)
            {
            const QString s = t.trimmed();
            return s.isEmpty() || defaultText().startsWith(s, Qt::CaseInsensitive);
            }

   public:
      LevelSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

      static QString defaultText() { return QDoubleSpinBox::tr("Library default"); }

      QString textFromValue(double v) const override
            {
            if (qAbs(v) < 0.001)
                  return defaultText();
            return QString(v > 0 ? "+" : "") + QDoubleSpinBox::textFromValue(v) + " dB";
            }
      double valueFromText(const QString& text) const override
            {
            if (isDefault(text))
                  return 0.0;
            return QDoubleSpinBox::valueFromText(number(text));
            }
      QValidator::State validate(QString& input, int& pos) const override
            {
            Q_UNUSED(pos);
            if (isDefault(input))
                  return QValidator::Acceptable;
            const QString n = number(input);
            if (n.isEmpty() || n == "-")
                  return QValidator::Intermediate;
            bool ok = false;
            const double v = locale().toDouble(n, &ok);
            if (!ok)
                  return QValidator::Invalid;
            return (v >= minimum() && v <= maximum()) ? QValidator::Acceptable : QValidator::Intermediate;
            }
      };

} // namespace Ms

#endif
