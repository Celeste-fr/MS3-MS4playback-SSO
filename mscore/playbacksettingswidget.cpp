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

#include <algorithm>
#include <cmath>
#include <QComboBox>
#include <QCoreApplication>
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

// the panel's texts: Playback::display()'s, translated (a setting without them shows its ini name)
static QString tx(const char* s)
      {
      return QCoreApplication::translate("playbackSettings", s);
      }

static QString labelOf(const QString& id)
      {
      const Playback::Display* disp = Playback::display(id.toUtf8().constData());
      return disp ? tx(disp->label) : id;
      }

// names of the values d.min, d.min + 1 … d.max; empty: the setting is a number
static QStringList valueNames(const Playback::Definition& d)
      {
      QStringList names;
      const Playback::Display* disp = Playback::display(d.id);
      if (disp && !disp->values.empty()) {
            for (const char* n : disp->values)
                  names << tx(n);
            }
      else if (QString(d.unit) == "on/off")
            names << QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "Off"))
                  << QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "On"));
      if (names.size() != int(std::lround(d.max - d.min)) + 1)
            return QStringList();
      return names;
      }

PlaybackSettingsWidget::PlaybackSettingsWidget(MasterScore* score, std::shared_ptr<const SoundLib::Library> library,
                                               std::function<void(const char*, const QString&)> setMetaTag, QWidget* parent)
   : QWidget(parent), _score(score), _library(library), _setMetaTag(setMetaTag)
      {
      QVBoxLayout* v = new QVBoxLayout(this);
      v->setContentsMargins(0, 0, 0, 0);
      QHBoxLayout* ph = new QHBoxLayout;
      ph->addWidget(new QLabel(tr("Preset:"), this));
      _preset = new QComboBox(this);
      _preset->setToolTip(tr("Replaces your settings with the preset's values and keeps the old file as playback.ini.bak"));
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
      QPushButton* reset = new QPushButton(tr("Reset"), this);
      reset->setToolTip(tr("Use your settings or the defaults for the selected settings in this score"));
      QPushButton* open = new QPushButton(tr("Open Settings File"), this);
      open->setToolTip(Playback::iniPath());
      QPushButton* reload = new QPushButton(tr("Reload Settings File"), this);
      reload->setToolTip(tr("Reads your settings file again"));
      QPushButton* clear = new QPushButton(tr("Reset All"), this);
      clear->setToolTip(tr("Use your settings or the defaults for every setting in this score"));
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

// the Preset box: both presets, and Custom, not selectable, only when the ini matches neither
void PlaybackSettingsWidget::fillPreset()
      {
      const QString now = Playback::currentPreset();
      _preset->clear();
      for (const Playback::Preset& p : Playback::presets())
            _preset->addItem(QCoreApplication::translate("playbackSettings", p.name), QString(p.id));
      if (now.isEmpty()) {
            _preset->addItem(tr("Custom"), QString());
            QStandardItemModel* m = qobject_cast<QStandardItemModel*>(_preset->model());
            if (m)
                  m->item(_preset->count() - 1)->setFlags(Qt::NoItemFlags);
            }
      _preset->setCurrentIndex(now.isEmpty() ? _preset->count() - 1 : _preset->findData(now));
      QStringList over;
      for (const QString& id : Playback::presetKeysOverriddenBy(_score.data()))
            over << labelOf(id);
      _presetInfo->setText(over.isEmpty() ? QString() : tr("Set in this score: %1").arg(over.join(", ")));
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
      // the groups in displayGroups()' order (a setting without display data last, under its ini section), each
      // group's settings in the definitions' order
      std::vector<std::pair<QString, std::vector<const Playback::Definition*>>> groups;
      for (const char* g : Playback::displayGroups())
            groups.push_back({ QString(g), {} });
      for (const Playback::Definition& d : Playback::definitions()) {
            const Playback::Display* disp = Playback::display(d.id);
            const QString group = disp ? QString(disp->group) : QString(d.id).section('/', 0, 0);
            auto g = std::find_if(groups.begin(), groups.end(), [&](const auto& x) { return x.first == group; });
            if (g == groups.end()) {
                  groups.push_back({ group, {} });
                  g = groups.end() - 1;
                  }
            g->second.push_back(&d);
            }
      for (const auto& group : groups) {
            if (group.second.empty())
                  continue;
            QTreeWidgetItem* g = new QTreeWidgetItem(_tree, { tx(group.first.toUtf8().constData()) });
            g->setFirstColumnSpanned(false);
            g->setExpanded(true);
            for (const Playback::Definition* dp : group.second) {
                  const Playback::Definition& d = *dp;
                  const QString id = d.id;
                  const Playback::Display* disp = Playback::display(d.id);
                  const double map = mapValue(d.id);
                  const double v = Playback::value(d.id, d.perScore ? _score.data() : nullptr, map);
                  const Playback::Source src = Playback::source(d.id, d.perScore ? _score.data() : nullptr, map);
                  QTreeWidgetItem* it = new QTreeWidgetItem(g, { labelOf(id), QString(), Playback::sourceName(src) });
                  it->setData(0, Qt::UserRole, id);
                  // (the ini name ends the tooltip, to find the setting in playback.ini)
                  const QString tip = disp ? QString("%1 (%2)").arg(tx(disp->tooltip), id) : QString("%1: %2").arg(id, d.comment);
                  for (int c = 0; c < 3; ++c)
                        it->setToolTip(c, tip);
                  if (id == selected)
                        it->setSelected(true);
                  QFont f = _tree->font();
                  f.setBold(src == Playback::Source::SCORE);
                  const QByteArray key = id.toUtf8();
                  QWidget* editor = nullptr;
                  const QStringList names = valueNames(d);
                  if (!names.isEmpty()) {
                        // a mode or on/off: its named values
                        QComboBox* combo = new QComboBox(_tree);
                        for (int i = 0; i < names.size(); ++i)
                              combo->addItem(names[i], d.min + i);
                        const int index = int(std::lround(v - d.min));
                        if (index >= 0 && index < names.size())
                              combo->setCurrentIndex(index);
                        else {
                              combo->addItem(QString::number(v), v);    // (a value between the named ones: shown as is)
                              combo->setCurrentIndex(combo->count() - 1);
                              }
                        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, key, combo](int i) {
                              if (_filling)
                                    return;
                              setScoreValue(key.constData(), combo->itemData(i).toDouble(), false);
                              // (after the box's signal: refresh deletes the box)
                              QTimer::singleShot(0, this, [this]() {
                                    refresh();
                                    emit changed();
                                    });
                              });
                        editor = combo;
                        }
                  else {
                        QDoubleSpinBox* box = new QDoubleSpinBox(_tree);
                        box->setRange(d.min, d.max);
                        const bool integral = d.min == std::floor(d.min) && d.max == std::floor(d.max)
                                              && QString(d.unit) != "s" && QString(d.unit) != "cents" && id != "shorts/portato";
                        box->setDecimals(id == "tuning/tolerance" ? 3 : integral ? 0 : 2);
                        box->setValue(v);
                        if (*d.unit && QString(d.unit) != "on/off")
                              box->setSuffix(QString(" ") + d.unit);
                        if (disp && disp->special)
                              box->setSpecialValueText(tx(disp->special));
                        // (computed when the map gives none: tuning tail per note; copies: no maximum, as the music needs; the map's
                        // maxLanes is 0 when it gives none)
                        if ((id == "tuning/tail" ? map < 0 : map <= 0) && src != Playback::Source::SCORE && src != Playback::Source::INI
                            && (id == "tuning/tail" || id == "tuning/maxLanes")) {
                              box->setMinimum(d.min - 1);
                              box->setSpecialValueText(id == "tuning/tail" ? tr("Measured release") : tr("No maximum"));
                              box->setValue(d.min - 1);
                              }
                        box->setKeyboardTracking(false);
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
                        editor = box;
                        }
                  editor->setEnabled(d.perScore && _score);
                  editor->setToolTip(tip);
                  editor->setFont(f);
                  _tree->setItemWidget(it, 1, editor);
                  if (id == focused)
                        focusBox = editor;
                  }
            }
      _tree->doItemsLayout();
      _tree->verticalScrollBar()->setValue(scrolled);
      if (focusBox)
            focusBox->setFocus();
      QStringList warn = Playback::warnings();
      // (one line: the details on hover)
      _info->setText(warn.isEmpty() ? QString() : tr("%n line(s) of playback.ini ignored or corrected", "", warn.size()));
      _info->setToolTip(warn.join("\n"));
      _info->setVisible(!warn.isEmpty());
      _filling = false;
      }

} // namespace Ms
