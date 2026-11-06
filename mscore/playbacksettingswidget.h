//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYBACKSETTINGSWIDGET_H__
#define __PLAYBACKSETTINGSWIDGET_H__

#include <functional>
#include <memory>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QTreeWidget;

namespace Ms {

class MasterScore;
namespace SoundLib { class Library; }

//---------------------------------------------------------
//   PlaybackSettingsWidget
//    Mixer › Advanced Options… › Playback adjustments: every setting of libmscore/playbacksettings.h by its
//    Playback::display() (group, label, named values in a combo box, else a spin box; the ini name at the end of the
//    tooltip), its effective value for the score and where it comes from (Default, Sound library, Your settings
//    = playback.ini, This score); editing a value sets it for this score (metaTag, through setMetaTag: undoable),
//    Reset / Reset All take the score's own values away; Open / Reload Settings File (playback.ini); the Preset box
//    (Recommended / Library default) edits playback.ini and reloads
//---------------------------------------------------------

class PlaybackSettingsWidget : public QWidget {
      Q_OBJECT

      QPointer<MasterScore> _score;
      std::shared_ptr<const SoundLib::Library> _library;
      std::function<void(const char*, const QString&)> _setMetaTag;
      QTreeWidget* _tree { nullptr };
      QLabel* _info { nullptr };
      QComboBox* _preset { nullptr };
      QLabel* _presetInfo { nullptr };
      bool _filling { false };

      double mapValue(const char* id) const;
      void setScoreValue(const char* id, double value, bool remove);
      void fillPreset();

   signals:
      void changed();

   public:
      PlaybackSettingsWidget(MasterScore* score, std::shared_ptr<const SoundLib::Library> library,
                             std::function<void(const char*, const QString&)> setMetaTag, QWidget* parent = nullptr);
      void refresh();
      };

} // namespace Ms
#endif
