//=============================================================================
//  MuseScore
//  Linux Music Score Editor
//
//  Copyright (C) 2002-2016 Werner Schweer and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//=============================================================================

#include "mixerdetails.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QVBoxLayout>

#include "musescore.h"

#include "libmscore/score.h"
#include "libmscore/part.h"
#include "mixer.h"
#include "mixertrack.h"
#include "mixertrackitem.h"
#include "seq.h"
#include "libmscore/undo.h"
#include "synthcontrol.h"
#include "audio/midi/msynthesizer.h"
#include "preferences.h"
#include "playbackmode.h"
#include "libmscore/partplayback.h"
#include "libmscore/plainliveset.h"
#include "libmscore/trackdelays.h"
#include "libmscore/soundlibrary.h"
#include "soundlibraryhost.h"

namespace Ms {

//---------------------------------------------------------
//   MixerDetails
//---------------------------------------------------------

MixerDetails::MixerDetails(QWidget *parent) :
      QWidget(parent),
      _mti(nullptr),
      mutePerVoiceHolder(nullptr)
      {
      setupUi(this);

      // the part's playback mode: the global one, or its own (kept in the score)
      labelPlayback = new QLabel(tr("Part playback:"), this);
      playbackCombo = new QComboBox(this);
      playbackCombo->setToolTip(tr("How this part plays: \"Global default\" plays it as \"Global playback\" at the top says, or MuseScore 3, MuseScore 4 or the sound library for this part only (saved in the score)"));
      labelPlayback->setBuddy(playbackCombo);
      const int row = gridLayout_2->rowCount();
      gridLayout_2->addWidget(labelPlayback, row, 0);
      gridLayout_2->addWidget(playbackCombo, row, 1, 1, gridLayout_2->columnCount() - 1);
      connect(playbackCombo, SIGNAL(activated(int)), SLOT(playbackChanged(int)));

      // a sound library part's track delay (as Live's Track Delay; kept in the score), and its tracks' own
      labelDelay = new QLabel(tr("Track delay:"), this);
      delaySpinBox = new QDoubleSpinBox(this);
      delaySpinBox->setRange(TrackDelays::MIN_MS, TrackDelays::MAX_MS);
      delaySpinBox->setDecimals(1);
      delaySpinBox->setSingleStep(1.0);
      delaySpinBox->setSuffix(tr(" ms"));
      delaySpinBox->setKeyboardTracking(false);
      delaySpinBox->setToolTip(tr("The part plays this much later (negative: earlier), as Live's Track Delay: on its group "
                                  "track in the Live set Create Live Set writes (saved in the score)"));
      labelDelay->setBuddy(delaySpinBox);
      delayTracksButton = new QPushButton(tr("Tracks…"), this);
      delayTracksButton->setToolTip(tr("Each patch's and each technique's own delay, added to the part's, and own level "
                                       "(the Live set's Kontakt and technique tracks)"));
      const int delayRow = gridLayout_2->rowCount();
      gridLayout_2->addWidget(labelDelay, delayRow, 0);
      QHBoxLayout* delayBox = new QHBoxLayout();
      delayBox->addWidget(delaySpinBox, 1);
      delayBox->addWidget(delayTracksButton);
      gridLayout_2->addLayout(delayBox, delayRow, 1, 1, gridLayout_2->columnCount() - 1);
      connect(delaySpinBox, SIGNAL(editingFinished()), SLOT(trackDelayChanged()));
      connect(delayTracksButton, SIGNAL(clicked()), SLOT(editTrackDelays()));

      // a narrow Mixer (the owner, 2026-10-05: "the mixer menu is too wide"): long sound names (a library's
      // patch with the library's name) are cut instead of widening it, and below the width of both columns
      // MIDI and Mute Voice go under the part's settings (placeMidiColumn)
      for (QComboBox* c : { patchCombo, playbackCombo }) {
            c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            c->setMinimumContentsLength(12);
            }

      connect(partNameLineEdit,    SIGNAL(editingFinished()),              SLOT(partNameChanged()));
      connect(trackColorLabel,     SIGNAL(colorChanged(QColor)),           SLOT(trackColorChanged(QColor)));
      connect(patchCombo,          SIGNAL(activated(int)),                 SLOT(patchChanged(int)));
      connect(volumeSlider,        &QSlider::valueChanged,       this,     &MixerDetails::volumeChanged);
      connect(volumeSpinBox,       SIGNAL(valueChanged(double)),           SLOT(volumeChanged(double)));
      connect(panSlider,           &QSlider::valueChanged,       this,     &MixerDetails::panChanged);
      connect(panSpinBox,          SIGNAL(valueChanged(double)),           SLOT(panChanged(double)));
      connect(chorusSlider,        &QSlider::valueChanged,       this,     &MixerDetails::chorusChanged);
      connect(chorusSpinBox,       SIGNAL(valueChanged(double)),           SLOT(chorusChanged(double)));
      connect(reverbSlider,        &QSlider::valueChanged,       this,     &MixerDetails::reverbChanged);
      connect(reverbSpinBox,       SIGNAL(valueChanged(double)),           SLOT(reverbChanged(double)));
      connect(portSpinBox,         SIGNAL(valueChanged(int)),              SLOT(midiChannelChanged(int)));
      connect(channelSpinBox,      SIGNAL(valueChanged(int)),              SLOT(midiChannelChanged(int)));
      connect(drumkitCheck,        SIGNAL(toggled(bool)),                  SLOT(drumkitToggled(bool)));

      for (QWidget* w : std::initializer_list<QWidget*> { patchCombo, volumeSlider, volumeSpinBox, panSlider, panSpinBox, reverbSlider,
                                                         reverbSpinBox, chorusSlider, chorusSpinBox, portSpinBox, channelSpinBox })
            _tips[w] = w->toolTip();

      updateFromTrack();
      }

//---------------------------------------------------------
//   setTrack
//---------------------------------------------------------

void MixerDetails::setTrack(MixerTrackItemPtr track)
      {
      _mti = track;
      setNotifier(_mti ? _mti->focusedChan() : nullptr);
      updateFromTrack();
      }

//---------------------------------------------------------
//   placeMidiColumn
//    MIDI and Mute Voice beside the part's settings when both fit, else under them; the panel's
//    minimum is the wider of the two, so the Mixer can be made as narrow as one column
//---------------------------------------------------------

void MixerDetails::placeMidiColumn()
      {
      const int left = gridLayout_2->minimumSize().width();
      const int right = gridLayout->minimumSize().width();
      if (minimumWidth() != qMax(left, right))
            setMinimumWidth(qMax(left, right));
      const bool stacked = width() < left + gridLayout_4->horizontalSpacing() + right;
      if (stacked == _stacked)
            return;
      _stacked = stacked;
      gridLayout_4->removeItem(gridLayout);
      gridLayout_4->addLayout(gridLayout, stacked ? 1 : 0, stacked ? 2 : 3);
      gridLayout_4->setVerticalSpacing(stacked ? 11 : 0);
      }

void MixerDetails::resizeEvent(QResizeEvent* e)
      {
      QWidget::resizeEvent(e);
      placeMidiColumn();
      }


//---------------------------------------------------------
//   updateFromTrack
//---------------------------------------------------------

void MixerDetails::updateFromTrack()
      {
      if (mutePerVoiceHolder) {
            mutePerVoiceHolder->deleteLater();
            mutePerVoiceHolder = nullptr;
            }

      updatePlayback();

      if (!_mti) {
            drumkitCheck->setChecked(false);
            patchCombo->clear();
            partNameLineEdit->setText("");
            channelLabel->setText("");
            volumeSlider->setValue(0);
            volumeSpinBox->setValue(0);
            panSlider->setValue(0);
            panSpinBox->setValue(0);
            reverbSlider->setValue(0);
            reverbSpinBox->setValue(0);
            chorusSlider->setValue(0);
            chorusSpinBox->setValue(0);
            portSpinBox->setValue(0);
            channelSpinBox->setValue(0);
            trackColorLabel->blockSignals(true);
            trackColorLabel->setColor(QColor());
            trackColorLabel->blockSignals(false);

            drumkitCheck->setEnabled(false);
            patchCombo->setEnabled(false);
            partNameLineEdit->setEnabled(false);
            volumeSlider->setEnabled(false);
            volumeSpinBox->setEnabled(false);
            panSlider->setEnabled(false);
            panSpinBox->setEnabled(false);
            reverbSlider->setEnabled(false);
            reverbSpinBox->setEnabled(false);
            chorusSlider->setEnabled(false);
            chorusSpinBox->setEnabled(false);
            portSpinBox->setEnabled(false);
            channelSpinBox->setEnabled(false);
            trackColorLabel->setEnabled(false);

            labelName->setEnabled(false);
            labelChannel->setEnabled(false);
            labelChannel_2->setEnabled(false);
            labelChorus->setEnabled(false);
            labelPan->setEnabled(false);
            labelPatch->setEnabled(false);
            labelPort->setEnabled(false);
            labelReverb->setEnabled(false);
            labelVolume->setEnabled(false);
            return;
            }

      drumkitCheck->setEnabled(true);
      patchCombo->setEnabled(true);
      partNameLineEdit->setEnabled(true);
      volumeSlider->setEnabled(true);
      volumeSpinBox->setEnabled(true);
      panSlider->setEnabled(true);
      panSpinBox->setEnabled(true);
      reverbSlider->setEnabled(true);
      reverbSpinBox->setEnabled(true);
      chorusSlider->setEnabled(true);
      chorusSpinBox->setEnabled(true);
      portSpinBox->setEnabled(true);
      channelSpinBox->setEnabled(true);
      trackColorLabel->setEnabled(true);

      labelName->setEnabled(true);
      labelChannel->setEnabled(true);
      labelChannel_2->setEnabled(true);
      labelChorus->setEnabled(true);
      labelPan->setEnabled(true);
      labelPatch->setEnabled(true);
      labelPort->setEnabled(true);
      labelReverb->setEnabled(true);
      labelVolume->setEnabled(true);


      MidiMapping* midiMap = _mti->midiMap();
      Part* part = _mti->part();
      Channel* chan = _mti->focusedChan();

      //Check if drumkit
      const bool isHarmonyChannel = chan->isHarmonyChannel();
      const bool drum = midiMap->part()->instrument()->useDrumset() && !isHarmonyChannel;
      drumkitCheck->blockSignals(true);
      drumkitCheck->setChecked(drum);
      drumkitCheck->setEnabled(!isHarmonyChannel);
      drumkitCheck->blockSignals(false);

      //Populate patch combo
      patchCombo->blockSignals(true);
      patchCombo->clear();
      const auto& pl = synti->getPatchInfo();
      int patchIndex = 0;

      // Order by program number instead of bank, so similar instruments
      // appear next to each other, but ordered primarily by soundfont
      std::map<int, std::map<int, std::vector<const MidiPatch*>>> orderedPl;

      for (const MidiPatch* p : pl)
            orderedPl[p->sfid][p->prog].push_back(p);

      std::vector<QString> usedNames;
      for (auto const& sf : orderedPl) {
            for (auto const& pn : sf.second) {
                  for (const MidiPatch* p : pn.second) {
                        if (p->drum == drum || p->synti != "Fluid") {
                              QString pName = p->name;
                              if (std::find(usedNames.begin(), usedNames.end(), p->name) != usedNames.end()) {
                                    QString addNum = QString(" (%1)").arg(p->sfid);
                                    pName.append(addNum);
                                    }
                              else
                                    usedNames.push_back(p->name);

                              patchCombo->addItem(pName, QVariant::fromValue<void*>((void*)p));
                              if (p->synti == chan->synti() &&
                                  p->bank == chan->bank() &&
                                  p->prog == chan->program())
                                    patchIndex = patchCombo->count() - 1;
                              }
                        }
                  }
            }
      patchCombo->setCurrentIndex(patchIndex);

      patchCombo->blockSignals(false);

      QString partName = part->partName();
      if (!chan->name().isEmpty())
            channelLabel->setText(qApp->translate("InstrumentsXML", chan->name().toUtf8().data()));
      else
            channelLabel->setText("");
      partNameLineEdit->setText(partName);
      partNameLineEdit->setToolTip(partName);


      trackColorLabel->blockSignals(true);
      volumeSlider->blockSignals(true);
      volumeSpinBox->blockSignals(true);
      panSlider->blockSignals(true);
      panSpinBox->blockSignals(true);
      reverbSlider->blockSignals(true);
      reverbSpinBox->blockSignals(true);
      chorusSlider->blockSignals(true);
      chorusSpinBox->blockSignals(true);

      portSpinBox->blockSignals(true);
      channelSpinBox->blockSignals(true);

      trackColorLabel->setColor(QColor(_mti->color() | 0xff000000));

      volumeSlider->setValue((int)chan->volume());
      volumeSpinBox->setValue(chan->volume());
      panSlider->setValue((int)chan->pan());
      panSpinBox->setValue(chan->pan());
      reverbSlider->setValue((int)chan->reverb());
      reverbSpinBox->setValue(chan->reverb());
      chorusSlider->setValue((int)chan->chorus());
      chorusSpinBox->setValue(chan->chorus());

      portSpinBox->setValue(part->masterScore()->midiMapping(chan->channel())->port() + 1);
      channelSpinBox->setValue(part->masterScore()->midiMapping(chan->channel())->channel() + 1);

      trackColorLabel->blockSignals(false);
      volumeSlider->blockSignals(false);
      volumeSpinBox->blockSignals(false);
      panSlider->blockSignals(false);
      panSpinBox->blockSignals(false);
      reverbSlider->blockSignals(false);
      reverbSpinBox->blockSignals(false);
      chorusSlider->blockSignals(false);
      chorusSpinBox->blockSignals(false);

      portSpinBox->blockSignals(false);
      channelSpinBox->blockSignals(false);

      //Set up mute per voice buttons
      mutePerVoiceHolder = new QWidget();
      mutePerVoiceArea->addWidget(mutePerVoiceHolder);

      mutePerVoiceGrid = new QGridLayout();
      mutePerVoiceHolder->setLayout(mutePerVoiceGrid);
      mutePerVoiceGrid->setContentsMargins(0, 0, 0, 0);
      mutePerVoiceGrid->setSpacing(7);

      for (int staffIdx = 0; staffIdx < (*part->staves()).length(); ++staffIdx) {
            Staff* staff = (*part->staves())[staffIdx];
            for (int voice = 0; voice < VOICES; ++voice) {
                  QPushButton* tb = new QPushButton;
                  tb->setStyleSheet(
                        QString("QPushButton{padding: 4px 8px 4px 8px;}QPushButton:checked{background-color:%1}")
                        .arg(MScore::selectColor[voice].name()));
                  tb->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
                  tb->setText(QString("%1").arg(voice + 1));
                  tb->setCheckable(true);
                  tb->setChecked(!staff->playbackVoice(voice));
                  tb->setToolTip(QString(tr("Staff %1:")).arg(staffIdx + 1));

                  mutePerVoiceGrid->addWidget(tb, staffIdx, voice);
                  MixerDetailsVoiceButtonHandler* handler =
                              new MixerDetailsVoiceButtonHandler(this, staffIdx, voice, tb);
                  connect(tb, SIGNAL(toggled(bool)), handler, SLOT(setVoiceMute(bool)));
                  }
            }

      updateLibrary();
      }

//---------------------------------------------------------
//   updateLibrary
//    a part the sound library plays: volume and pan act on its sound (hosted: in MuseScore, on its
//    plug-ins' output; over MIDI out: CC7 / CC10 on its routes), from its first channel (the part's
//    row sets all its channels); reverb and chorus: hosted, the library has its own room (SSO's mics:
//    View › Sound Library… › Controllers…), MuseScore's reverb is not on it; over MIDI out, CC91 / CC93.
//    The patch and MIDI port / channel are the library's (a kit's sounds the library lacks keep the
//    General MIDI patch and channel)
//---------------------------------------------------------

void MixerDetails::updateLibrary()
      {
      for (auto i = _tips.begin(); i != _tips.end(); ++i)
            i.key()->setToolTip(i.value());
      updateTrackDelay(false);
      if (!_mti)
            return;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Part* part = _mti->part();
      if (!library || !SoundLib::active() || !part)
            return;
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!master || master->instruments()->empty()
          || !PartPlaybackModes::playsLibrary(master, PartPlaybackModes::read(part->masterScore())))
            return;
      const SoundLib::LibInstrument* li = library->match(master->instruments()->begin()->second, master);
      if (!li)
            return;
      updateTrackDelay(true);
      const bool hosted = SoundLib::output() == SoundLib::Output::PLUGIN;
      const QString where = hosted ? tr("its instance of the library's plug-in, in MuseScore")
                                   : tr("its MIDI route (port and channel set by the library)");
      Channel* chan = _mti->focusedChan();
      const Instrument* first = master->instruments()->begin()->second;
      const bool firstChannel = !first->channel().empty()
                                && part->masterScore()->playbackChannel(first->channel(0)) == chan;
      const QString note = firstChannel ? QString()
            : QString("\n") + tr("A sound library part plays all its notes with its first channel's volume and pan (the part's row sets all its channels).");
      volumeSlider->setToolTip(tr("Volume of %1 on %2").arg(li->name, where) + note);
      volumeSpinBox->setToolTip(volumeSlider->toolTip());
      panSlider->setToolTip(tr("Pan of %1 on %2").arg(li->name, where) + note);
      panSpinBox->setToolTip(panSlider->toolTip());
      if (hosted) {
            const QString room = tr("%1 plays in its own room (its microphones: View › Sound Library… › Controllers…); "
                                    "MuseScore's reverb and chorus don't apply to it.").arg(library->name);
            for (QWidget* w : std::initializer_list<QWidget*> { reverbSlider, reverbSpinBox, chorusSlider, chorusSpinBox }) {
                  w->setEnabled(false);
                  w->setToolTip(room);
                  }
            labelReverb->setEnabled(false);
            labelChorus->setEnabled(false);
            }
      else {
            reverbSlider->setToolTip(tr("Sent as CC91 on %1").arg(where));
            reverbSpinBox->setToolTip(reverbSlider->toolTip());
            chorusSlider->setToolTip(tr("Sent as CC93 on %1").arg(where));
            chorusSpinBox->setToolTip(chorusSlider->toolTip());
            }
      if (li->kit) {
            const QString kit = tr("A drum sound %1 has no key for plays this General MIDI patch.").arg(library->name);
            patchCombo->setToolTip(kit);
            return;
            }
      // the library's patch (and its extras), not the General MIDI sounds
      patchCombo->blockSignals(true);
      patchCombo->clear();
      patchCombo->addItem(tr("%1 (%2)").arg(li->name, library->name));
      patchCombo->setCurrentIndex(0);
      patchCombo->blockSignals(false);
      patchCombo->setEnabled(false);
      patchCombo->setToolTip(tr("The sound library plays this part: its patch is chosen from the instrument (View › Sound Library…). "
                                "The General MIDI patch comes back with \"Part playback:\" set to MuseScore 3 or 4."));
      // the route: MuseScore's port and channel don't apply
      const std::vector<std::pair<int, int>> outs = seq ? seq->libraryOuts(master) : std::vector<std::pair<int, int>>();
      portSpinBox->setEnabled(false);
      channelSpinBox->setEnabled(false);
      labelPort->setEnabled(false);
      labelChannel_2->setEnabled(false);
      QString route = hosted ? tr("Plays on its own instance of the library's plug-in: MIDI port and channel don't apply.")
                             : tr("The sound library sends this part on its own port and channel (in score order).");
      if (!hosted && !outs.empty()) {
            const QSignalBlocker b1(portSpinBox);
            const QSignalBlocker b2(channelSpinBox);
            portSpinBox->setValue(outs.front().first + 1);
            channelSpinBox->setValue(outs.front().second + 1);
            }
      portSpinBox->setToolTip(route);
      channelSpinBox->setToolTip(route);
      }

//---------------------------------------------------------
//   setVoiceMute
//---------------------------------------------------------

void MixerDetails::setVoiceMute(int staffIdx, int voice, bool shouldMute)
      {
      Part* part = _mti->part();
      Staff* staff = part->staff(staffIdx);
      switch (voice) {
            case 0:
                  staff->undoChangeProperty(Pid::PLAYBACK_VOICE1, !shouldMute);
                  break;
            case 1:
                  staff->undoChangeProperty(Pid::PLAYBACK_VOICE2, !shouldMute);
                  break;
            case 2:
                  staff->undoChangeProperty(Pid::PLAYBACK_VOICE3, !shouldMute);
                  break;
            case 3:
                  staff->undoChangeProperty(Pid::PLAYBACK_VOICE4, !shouldMute);
                  break;
            }
      }


//---------------------------------------------------------
//   updatePlayback
//    the choices: the global mode (the Playback box), then MuseScore 3, MuseScore 4, the library
//---------------------------------------------------------

void MixerDetails::updatePlayback()
      {
      const QSignalBlocker block(playbackCombo);
      playbackCombo->clear();
      playbackCombo->addItem(tr("Global default"), int(PartPlayback::DEFAULT));   // (names: the owner, 2026-10-07)
      playbackCombo->addItem(playbackModeName(PlaybackMode::MS3), int(PartPlayback::MS3));
      playbackCombo->addItem(playbackModeName(PlaybackMode::MS4), int(PartPlayback::MS4));
      playbackCombo->addItem(playbackModeName(PlaybackMode::LIBRARY), int(PartPlayback::LIBRARY));
      const Part* part = _mti ? _mti->part() : nullptr;
      playbackCombo->setCurrentIndex(playbackCombo->findData(int(part ? PartPlaybackModes::of(part) : PartPlayback::DEFAULT)));
      playbackCombo->setEnabled(part);
      labelPlayback->setEnabled(part);
      }

//---------------------------------------------------------
//   playbackChanged
//    the part's own mode, in the master score's metaTag (undoable)
//---------------------------------------------------------

void MixerDetails::playbackChanged(int index)
      {
      if (!_mti)
            return;
      Part* part = _mti->part();
      Score* score = part->score();
      MasterScore* ms = score->masterScore();
      std::map<const Part*, PartPlayback> modes = PartPlaybackModes::read(ms);
      modes[PartPlaybackModes::masterPart(part)] = PartPlayback(playbackCombo->itemData(index).toInt());
      QMap<QString, QString> tags = ms->metaTags();
      const QString value = PartPlaybackModes::write(ms, modes);
      if (value.isEmpty())
            tags.remove(PartPlaybackModes::metaTag);
      else
            tags.insert(PartPlaybackModes::metaTag, value);
      if (tags == ms->metaTags())
            return;
      {
      const GoOnPlaying goOn;             // (the part may play other patches or the other synthesizer)
      score->startCmd();
      score->undo(new ChangeMetaTags(ms, tags));
      score->endCmd();
      ms->setPlaylistDirty();
      }
      updateFromTrack();                  // (what applies to a sound library part)
      }

//---------------------------------------------------------
//   updateTrackDelay
//    the part's track delay: only for a part the sound library plays
//---------------------------------------------------------

void MixerDetails::updateTrackDelay(bool library)
      {
      const Part* part = _mti ? _mti->part() : nullptr;
      const TrackDelays::Delays d = part && library ? TrackDelays::of(part, TrackDelays::read(part->masterScore()))
                                                    : TrackDelays::Delays();
      const QSignalBlocker block(delaySpinBox);
      delaySpinBox->setValue(d.ms);
      for (QWidget* w : std::initializer_list<QWidget*> { labelDelay, delaySpinBox, delayTracksButton })
            w->setEnabled(library);
      const size_t set = d.tracks.size() + d.levels.size();
      delayTracksButton->setText(set == 0 ? tr("Tracks…") : tr("Tracks (%1)…").arg(set));
      }

//---------------------------------------------------------
//   writeTrackDelays
//    a part's delays, in the master score's metaTag (one undoable step)
//---------------------------------------------------------

void MixerDetails::writeTrackDelays(const Part* part, const TrackDelays::Delays& delays)
      {
      MasterScore* ms = const_cast<Part*>(part)->masterScore();
      std::map<const Part*, TrackDelays::Delays> all = TrackDelays::read(ms);
      if (delays.empty())
            all.erase(PartPlaybackModes::masterPart(part));
      else
            all[PartPlaybackModes::masterPart(part)] = delays;
      QMap<QString, QString> tags = ms->metaTags();
      const QString value = TrackDelays::write(ms, all);
      if (value.isEmpty())
            tags.remove(TrackDelays::metaTag);
      else
            tags.insert(TrackDelays::metaTag, value);
      if (tags == ms->metaTags())
            return;
      Score* score = const_cast<Part*>(part)->score();
      score->startCmd();
      score->undo(new ChangeMetaTags(ms, tags));
      score->endCmd();
      ms->setPlaylistDirty();
      if (seq)
            seq->renderAgainPlaying();    // (same patches, other times and levels: heard without stopping)
      }

//---------------------------------------------------------
//   trackDelayChanged
//---------------------------------------------------------

void MixerDetails::trackDelayChanged()
      {
      if (!_mti)
            return;
      const Part* part = _mti->part();
      TrackDelays::Delays d = TrackDelays::of(part, TrackDelays::read(part->masterScore()));
      d.ms = delaySpinBox->value();
      writeTrackDelays(part, d);
      }

//---------------------------------------------------------
//   editTrackDelays
//    the part's tracks as the plain Live set has them: per patch (its Kontakt track) and per technique the
//    patch can play (its MIDI tracks), each one's own delay over the part's and its own level (dB, added up)
//---------------------------------------------------------

void MixerDetails::editTrackDelays()
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!_mti || !library)
            return;
      Part* part = _mti->part();
      const Part* master = PartPlaybackModes::masterPart(part);
      TrackDelays::Delays d = TrackDelays::of(part, TrackDelays::read(part->masterScore()));
      std::vector<const SoundLib::LibInstrument*> patches;
      for (const SoundLib::Route& r : SoundLib::routes(part->masterScore(), *library))
            if (r.part == master && r.lane == 0 && r.instrument
                && std::find(patches.begin(), patches.end(), r.instrument) == patches.end())
                  patches.push_back(r.instrument);

      QDialog dialog(this);
      dialog.setWindowTitle(tr("Track delays and levels: %1").arg(part->partName()));
      QVBoxLayout* top = new QVBoxLayout(&dialog);
      QLabel* intro = new QLabel(tr("Delay: added to the part's %1 ms, as Live adds a Kontakt track's and a technique "
                                    "track's delays to their group's (-1000 to 1000 ms). Level: a patch's and a "
                                    "technique's, added up, played by CC11 (expression) on the technique's notes, in "
                                    "Live's clips too; softer only (CC11 rests at its top), down to %2 dB.")
                                 .arg(d.ms).arg(TrackDelays::MIN_DB), &dialog);
      intro->setWordWrap(true);
      top->addWidget(intro);
      QScrollArea* scroll = new QScrollArea(&dialog);
      scroll->setWidgetResizable(true);
      QWidget* inner = new QWidget(scroll);
      QGridLayout* grid = new QGridLayout(inner);
      grid->addWidget(new QLabel(tr("Delay"), inner), 0, 1);
      grid->addWidget(new QLabel(tr("Level"), inner), 0, 2);
      std::map<QString, std::pair<QDoubleSpinBox*, QDoubleSpinBox*>> boxes;     // key -> delay, level
      auto row = [&](const QString& key, const QString& label) {
            if (boxes.count(key))
                  return;
            QDoubleSpinBox* ms = new QDoubleSpinBox(inner);
            ms->setRange(TrackDelays::MIN_MS, TrackDelays::MAX_MS);
            ms->setDecimals(1);
            ms->setSuffix(tr(" ms"));
            ms->setValue(TrackDelays::own(d, key));
            QDoubleSpinBox* db = new QDoubleSpinBox(inner);
            db->setRange(TrackDelays::MIN_DB, TrackDelays::MAX_DB);
            db->setDecimals(1);
            db->setSingleStep(0.5);
            db->setSuffix(tr(" dB"));
            db->setValue(TrackDelays::ownDb(d, key));
            const int r = grid->rowCount();
            grid->addWidget(new QLabel(label, inner), r, 0);
            grid->addWidget(ms, r, 1);
            grid->addWidget(db, r, 2);
            boxes[key] = { ms, db };
            };
      for (const SoundLib::LibInstrument* li : patches) {
            // (its map delay plays added to the value here, never stored: TrackDelays::played)
            const double mapMs = TrackDelays::mapMs(li, part->score());
            row(TrackDelays::trackKey(li->name), mapMs == 0.0 ? tr("%1 (Kontakt track)").arg(li->name)
                : tr("%1 (Kontakt track; plus the map's %2 ms)").arg(li->name).arg(mapMs));
            if (li->switchType == SoundLib::SwitchType::NONE || li->articulations.empty())
                  row(TrackDelays::trackKey(li->name, li->name), QString("      ") + li->name);
            else
                  for (const SoundLib::Articulation& a : li->articulations)
                        row(TrackDelays::trackKey(li->name, PlainLiveSet::techniqueName(li, a.value)),
                            QString("      ") + PlainLiveSet::techniqueName(li, a.value));
            }
      // (a track's value whose patch or technique the library no longer has stays, listed last)
      for (const auto& t : d.tracks)
            row(t.first, t.first);
      for (const auto& l : d.levels)
            row(l.first, l.first);
      grid->setRowStretch(grid->rowCount(), 1);
      scroll->setWidget(inner);
      top->addWidget(scroll, 1);
      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
      connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
      connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
      top->addWidget(buttons);
      dialog.resize(520, 480);
      if (dialog.exec() != QDialog::Accepted)
            return;
      d.tracks.clear();
      d.levels.clear();
      for (const auto& b : boxes) {
            if (b.second.first->value() != 0.0)
                  d.tracks[b.first] = b.second.first->value();
            if (b.second.second->value() != 0.0)
                  d.levels[b.first] = b.second.second->value();
            }
      writeTrackDelays(part, d);
      updateTrackDelay(true);
      }

//---------------------------------------------------------
//   partNameChanged
//---------------------------------------------------------

void MixerDetails::partNameChanged()
      {
      if (!_mti)
            return;

      QString text = partNameLineEdit->text();
      Part* part = _mti->part();
      if (part->partName() == text) {
            return;
            }

      Score* score = part->score();
      if (score) {
            score->startCmd();
            score->undo(new ChangePart(part, part->instrument(), text));
            score->endCmd();
            }
      }

//---------------------------------------------------------
//   trackColorChanged
//---------------------------------------------------------

void MixerDetails::trackColorChanged(QColor col)
      {
      if (trackColorLabel->color() != col) {
            trackColorLabel->blockSignals(true);
            trackColorLabel->setColor(col);
            trackColorLabel->blockSignals(false);
            }

      _mti->setColor(col.rgb());
      }

//---------------------------------------------------------
//   propertyChanged
//---------------------------------------------------------

void MixerDetails::propertyChanged(Channel::Prop property)
      {
      if (!_mti)
            return;

      MidiMapping* _midiMap = _mti->midiMap();
      Channel* chan = _midiMap->articulation();

      switch (property) {
            case Channel::Prop::VOLUME: {
                  volumeSlider->blockSignals(true);
                  volumeSpinBox->blockSignals(true);

                  volumeSlider->setValue((int)chan->volume());
                  volumeSpinBox->setValue(chan->volume());

                  volumeSlider->blockSignals(false);
                  volumeSpinBox->blockSignals(false);
                  break;
                  }
            case Channel::Prop::PAN: {
                  panSlider->blockSignals(true);
                  panSpinBox->blockSignals(true);

                  panSlider->setValue((int)chan->pan());
                  panSpinBox->setValue(chan->pan());

                  panSlider->blockSignals(false);
                  panSpinBox->blockSignals(false);
                  break;
                  }
            case Channel::Prop::CHORUS: {
                  chorusSlider->blockSignals(true);
                  chorusSpinBox->blockSignals(true);

                  chorusSlider->setValue((int)chan->chorus());
                  chorusSpinBox->setValue(chan->chorus());

                  chorusSlider->blockSignals(false);
                  chorusSpinBox->blockSignals(false);
                  break;
                  }
            case Channel::Prop::REVERB: {
                  reverbSlider->blockSignals(true);
                  reverbSpinBox->blockSignals(true);

                  reverbSlider->setValue((int)chan->reverb());
                  reverbSpinBox->setValue(chan->reverb());

                  reverbSlider->blockSignals(false);
                  reverbSpinBox->blockSignals(false);
                  break;
                  }
            case Channel::Prop::COLOR: {
                  trackColorChanged(chan->color());
                  break;
                  }
            case Channel::Prop::NAME: {
                  partNameLineEdit->blockSignals(true);
                  Part* part = _mti->part();
                  QString partName = part->partName();
                  partNameLineEdit->setText(partName);
                  partNameLineEdit->blockSignals(false);
                  break;
                  }
            default:
                  break;
            }
      }

//---------------------------------------------------------
//   volumeChanged
//---------------------------------------------------------

void MixerDetails::volumeChanged(double value)
      {
      if (!_mti)
            return;

      _mti->setVolume(value);
      }


//---------------------------------------------------------
//   panChanged
//---------------------------------------------------------

void MixerDetails::panChanged(double value)
      {
      if (!_mti)
            return;

      _mti->setPan(value);
      }


//---------------------------------------------------------
//   reverbChanged
//---------------------------------------------------------

void MixerDetails::reverbChanged(double v)
      {
      if (!_mti)
            return;

      _mti->setReverb(v);
      }

//---------------------------------------------------------
//   chorusChanged
//---------------------------------------------------------

void MixerDetails::chorusChanged(double v)
      {
      if (!_mti)
            return;

      _mti->setChorus(v);
      }

//---------------------------------------------------------
//   patchChanged
//---------------------------------------------------------

void MixerDetails::patchChanged(int n)
      {
      if (!_mti)
            return;

      const MidiPatch* p = (MidiPatch*)patchCombo->itemData(n, Qt::UserRole).value<void*>();
      if (p == 0) {
            qDebug("PartEdit::patchChanged: no patch");
            return;
            }

      Part* part = _mti->midiMap()->part();
      Channel* channel = _mti->midiMap()->articulation();
      Score* score = part->score();
      if (score) {
            score->startCmd();
            score->undo(new ChangePatch(score, channel, p));
            score->undo(new SetUserBankController(channel, true));
            score->setLayoutAll();
            score->endCmd();
            }
      }

//---------------------------------------------------------
//   drumkitToggled
//---------------------------------------------------------

void MixerDetails::drumkitToggled(bool val)
      {
      if (_mti == 0)
            return;

      Part* part = _mti->part();
      Channel* channel = _mti->focusedChan();


      Instrument *instr;
      if (_mti->trackType() == MixerTrackItem::TrackType::CHANNEL)
            instr = _mti->instrument();
      else
            instr = part->instrument(Fraction(0,1));

      if (instr->useDrumset() == val)
            return;

      const MidiPatch* newPatch = 0;
      const QList<MidiPatch*> pl = synti->getPatchInfo();
      for (const MidiPatch* p : pl) {
            if (p->drum == val) {
                  newPatch = p;
                  break;
                  }
            }

      Score* score = part->score();
      if (newPatch) {
            score->startCmd();
            part->undoChangeProperty(Pid::USE_DRUMSET, val);
            score->undo(new ChangePatch(score, channel, newPatch));
            score->setLayoutAll();
            score->endCmd();
            }
      }

//---------------------------------------------------------
//   midiChannelChanged
//   handles MIDI port & channel change
//---------------------------------------------------------

void MixerDetails::midiChannelChanged(int)
      {
      if (_mti == 0)
            return;

      Part* part = _mti->part();
      Channel* channel = _mti->focusedChan();

      seq->stopNotes(channel->channel());
      int p =    portSpinBox->value() - 1;
      int c = channelSpinBox->value() - 1;

      MidiMapping* midiMap = _mti->midiMap();
      part->masterScore()->updateMidiMapping(midiMap->articulation(), part, p, c);

      part->score()->setInstrumentsChanged(true);
      part->score()->setLayoutAll();
      seq->initInstruments();

      // Update MIDI Out ports
      int maxPort = std::max(p, part->score()->masterScore()->midiPortCount());
      part->score()->masterScore()->setMidiPortCount(maxPort);
      if (seq->driver() && (preferences.getBool(PREF_IO_JACK_USEJACKMIDI) || preferences.getBool(PREF_IO_ALSA_USEALSAAUDIO)))
            seq->driver()->updateOutPortCount(maxPort + 1);
      }


}
