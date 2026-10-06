//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3EditorWindow: a hosted plug-in's editor (IPlugView) in a window of its own. The view
//  is attached to the window's native handle (HWND, NSView or X11 window); it can ask for
//  another size (IPlugFrame) and, on Linux, for MuseScore's event loop (IRunLoop).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __VST3EDITOR_H__
#define __VST3EDITOR_H__

#include <memory>

#include <QWidget>

namespace Steinberg {
class IPlugView;
}

namespace Ms {

class Vst3EditorFrame;

class Vst3EditorWindow : public QWidget {
      Q_OBJECT

      Steinberg::IPlugView* _view { nullptr };
      Vst3EditorFrame* _frame { nullptr };
      bool _attached { false };
      bool _resizing { false };

      void attach();
      // the view goes when the window closes, before its owner can destroy the plug-in (close() only schedules
      // the window's deletion)
      void detach();

   protected:
      void showEvent(QShowEvent*) override;
      void closeEvent(QCloseEvent*) override;
      void resizeEvent(QResizeEvent*) override;

   public:
      // takes the view (IPlugView, already referenced once)
      Vst3EditorWindow(Steinberg::IPlugView* view, const QString& title, QWidget* parent = nullptr);
      ~Vst3EditorWindow() override;
      void resizeToView(int width, int height);     // the view's size, in its pixels

   signals:
      void closed();
      };

} // namespace Ms
#endif
