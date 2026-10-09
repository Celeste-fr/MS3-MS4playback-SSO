//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playbacksettingswidget.h"
#include "musescore.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"

#include <cmath>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

namespace Ms {

PlaybackSettingsWidget::PlaybackSettingsWidget(MasterScore* score, std::shared_ptr<const SoundLib::Library> library,
                                               std::function<void(const char*, const QString&)> setMetaTag, QWidget* parent)
   : QWidget(parent), _score(score), _library(library), _setMetaTag(setMetaTag)
      {
      QVBoxLayout* v = new QVBoxLayout(this);
      v->setContentsMargins(0, 0, 0, 0);
      QHBoxLayout* ph = new QHBoxLayout;
      ph->addWidget(new QLabel(tr("Preset:"), this));
      _preset = new QComboBox(this);
      _preset->setToolTip(tr("Sets the presets' keys in playback.ini (comments and other values are kept) and reads it again, "
                             "playback going on."));
      ph->addWidget(_preset);
      _presetInfo = new QLabel(this);
      ph->addWidget(_presetInfo, 1);
      v->addLayout(ph);
      _tree = new QTreeWidget(this);
      _tree->setColumnCount(3);
      _tree->setHeaderLabels({ tr("Setting"), tr("Value"), tr("From") });
      _tree->setRootIsDecorated(true);
      _tree->setMinimumHeight(220);
      _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
      v->addWidget(_tree);
      QHBoxLayout* h = new QHBoxLayout;
      QPushButton* reset = new QPushButton(tr("Reset to ini/default"), this);
      reset->setToolTip(tr("Takes this score's own value of the selected setting away (all of a selected group's)"));
      QPushButton* open = new QPushButton(tr("Open playback.ini"), this);
      open->setToolTip(Playback::iniPath());
      QPushButton* reload = new QPushButton(tr("Reload playback.ini"), this);
      reload->setToolTip(tr("Reads playback.ini again and renders the scores again (Edit › Reload Playback Settings)"));
      QPushButton* clear = new QPushButton(tr("Clear this score's values"), this);
      clear->setToolTip(tr("Takes all of this score's own values away: playback.ini (the preset) or the defaults apply"));
      h->addWidget(reset);
      h->addWidget(clear);
      h->addStretch();
      h->addWidget(open);
      h->addWidget(reload);
      v->addLayout(h);
      _info = new QLabel(this);
      _info->setWordWrap(true);
      v->addWidget(_info);

      connect(_preset, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            const QString id = _preset->itemData(index).toString();
            if (id.isEmpty())
                  return;
            if (mscore)
                  mscore->applyPlaybackPreset(id);
            else {
                  Playback::applyPreset(id);
                  Playback::reload();
                  }
            refresh();
            emit changed();
            });
      connect(reset, &QPushButton::clicked, this, [this]() {
            for (QTreeWidgetItem* it : _tree->selectedItems()) {
                  QList<QTreeWidgetItem*> items { it };
                  for (int c = 0; c < it->childCount(); ++c)
                        items << it->child(c);
                  for (QTreeWidgetItem* i : items) {
                        const QByteArray id = i->data(0, Qt::UserRole).toByteArray();
                        if (!id.isEmpty())
                              setScoreValue(id.constData(), 0, true);
                        }
                  }
            refresh();
            emit changed();
            });
      connect(clear, &QPushButton::clicked, this, [this]() {
            for (const Playback::Definition& d : Playback::definitions()) {
                  if (d.perScore && Playback::source(d.id, _score.data(), mapValue(d.id)) == Playback::Source::SCORE)
                        setScoreValue(d.id, 0, true);
                  }
            refresh();
            emit changed();
            });
      connect(open, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(Playback::iniPath()));
            });
      connect(reload, &QPushButton::clicked, this, [this]() {
            if (mscore)
                  mscore->reloadPlaybackSettings();
            else
                  Playback::reload();
            refresh();
            emit changed();
            });
      refresh();
      }

double PlaybackSettingsWidget::mapValue(const char* id) const
      {
      if (!_library)
            return -1;
      const QString s = id;
      if (s == "heldNotes/early")
            return _library->onsetEarly;
      if (s == "tuning/tolerance")
            return _library->laneTolerance >= 0 ? _library->laneTolerance : SoundLib::defaultLaneTolerance();
      if (s == "tuning/tail")
            return _library->laneTail;
      if (s == "tuning/maxLanes")
            return _library->maxLanes;
      return -1;
      }

// the score's own value: the older metaTags for theirs, else metaTag playbackSettings
void PlaybackSettingsWidget::setScoreValue(const char* id, double value, bool remove)
      {
      if (!_score || !_setMetaTag)
            return;
      const QString s = id;
      if (s == "heldNotes/early") {
            _setMetaTag(SoundLib::onsetEarlyMetaTag,
                        remove ? QString() : QString::number(int(std::lround(value))));
            return;
            }
      if (Playback::hasOwnMetaTag(id) && _library) {
            SoundLib::LaneSettings ls = SoundLib::laneSettings(_score, *_library);
            const SoundLib::LaneSettings lib = SoundLib::libraryLaneSettings(*_library);
            if (s == "tuning/tolerance")
                  ls.tolerance = remove ? lib.tolerance : value;
            else if (s == "tuning/tail")
                  ls.tail = remove ? lib.tail : value;
            else
                  ls.maxLanes = remove ? lib.maxLanes : int(std::lround(value));
            _setMetaTag(SoundLib::laneSettingsMetaTag, SoundLib::writeLaneSettings(ls, *_library));
            return;
            }
      std::map<QString, double> values = Playback::scoreValues(_score);
      if (remove)
            values.erase(s);
      else
            values[s] = value;
      _setMetaTag(Playback::metaTag, Playback::writeScoreValues(values));
      }

// the Preset box: both presets, and (Custom), not selectable, only when the ini matches neither
void PlaybackSettingsWidget::fillPreset()
      {
      const QString now = Playback::currentPreset();
      _preset->clear();
      for (const Playback::Preset& p : Playback::presets())
            _preset->addItem(tr(p.name), QString(p.id));
      if (now.isEmpty()) {
            _preset->addItem(tr("(Custom)"), QString());
            QStandardItemModel* m = qobject_cast<QStandardItemModel*>(_preset->model());
            if (m)
                  m->item(_preset->count() - 1)->setFlags(Qt::NoItemFlags);
            }
      _preset->setCurrentIndex(now.isEmpty() ? _preset->count() - 1 : _preset->findData(now));
      const QStringList over = Playback::presetKeysOverriddenBy(_score.data());
      _presetInfo->setText(over.isEmpty() ? QString() : tr("this score overrides: %1").arg(over.join(", ")));
      }

void PlaybackSettingsWidget::refresh()
      {
      _filling = true;
      fillPreset();
      QString selected;
      if (!_tree->selectedItems().isEmpty())
            selected = _tree->selectedItems().front()->data(0, Qt::UserRole).toString();
      // the tree is filled again: keep where it was scrolled to and which value had the focus
      const int scrolled = _tree->verticalScrollBar()->value();
      QString focused;
      for (QTreeWidgetItemIterator i(_tree); *i; ++i)
            if (QWidget* w = _tree->itemWidget(*i, 1))
                  if (w->hasFocus())
                        focused = (*i)->data(0, Qt::UserRole).toString();
      QWidget* focusBox = nullptr;
      _tree->clear();
      std::map<QString, QTreeWidgetItem*> groups;
      for (const Playback::Definition& d : Playback::definitions()) {
            const QString id = d.id;
            const QString group = id.section('/', 0, 0);
            QTreeWidgetItem*& g = groups[group];
            if (!g) {
                  g = new QTreeWidgetItem(_tree, { "[" + group + "]" });
                  g->setFirstColumnSpanned(false);
                  g->setExpanded(true);
                  }
            const double map = mapValue(d.id);
            const double v = Playback::value(d.id, d.perScore ? _score.data() : nullptr, map);
            const Playback::Source src = Playback::source(d.id, d.perScore ? _score.data() : nullptr, map);
            QTreeWidgetItem* it = new QTreeWidgetItem(g, { id.section('/', 1), QString(), Playback::sourceName(src) });
            it->setData(0, Qt::UserRole, id);
            for (int c = 0; c < 3; ++c)
                  it->setToolTip(c, QString("%1: %2").arg(id, d.comment));
            if (id == selected)
                  it->setSelected(true);
            QDoubleSpinBox* box = new QDoubleSpinBox(_tree);
            box->setRange(d.min, d.max);
            const bool integral = d.min == std::floor(d.min) && d.max == std::floor(d.max)
                                  && QString(d.unit) != "s" && QString(d.unit) != "cents" && id != "shorts/portato";
            box->setDecimals(id == "tuning/tolerance" ? 3 : integral ? 0 : 2);
            box->setValue(v);
            if (*d.unit && QString(d.unit) != "on/off")
                  box->setSuffix(QString(" ") + d.unit);
            // (computed when the map gives none: tuning tail per note, copies by the free memory)
            if (map < 0 && src != Playback::Source::SCORE && src != Playback::Source::INI
                && (id == "tuning/tail" || id == "tuning/maxLanes")) {
                  box->setMinimum(d.min - 1);
                  box->setSpecialValueText(id == "tuning/tail" ? tr("measured release") : tr("by free memory"));
                  box->setValue(d.min - 1);
                  }
            box->setKeyboardTracking(false);
            box->setEnabled(d.perScore && _score);
            box->setToolTip(it->toolTip(0));
            QFont f = box->font();
            f.setBold(src == Playback::Source::SCORE);
            box->setFont(f);
            const QByteArray key = id.toUtf8();
            connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key](double x) {
                  if (_filling)
                        return;
                  setScoreValue(key.constData(), x, false);
                  // (after the box's signal: refresh deletes the box)
                  QTimer::singleShot(0, this, [this]() {
                        refresh();
                        emit changed();
                        });
                  });
            _tree->setItemWidget(it, 1, box);
            if (id == focused)
                  focusBox = box;
            }
      _tree->doItemsLayout();
      _tree->verticalScrollBar()->setValue(scrolled);
      if (focusBox)
            focusBox->setFocus();
      QStringList warn = Playback::warnings();
      // (one line: the details on hover)
      _info->setText(warn.isEmpty() ? QString() : tr("playback.ini: %n line(s) ignored or corrected (hover for details)", "", warn.size()));
      _info->setToolTip(warn.join("\n"));
      _info->setVisible(!warn.isEmpty());
      _filling = false;
      }

} // namespace Ms
