//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3EditorWindow: a hosted plug-in's editor window (see vst3editor.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3editor.h"

#include <map>

#include <QCloseEvent>
#include <QSocketNotifier>
#include <QTimer>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

using namespace Steinberg;

namespace Ms {

//---------------------------------------------------------
//   Vst3EditorFrame
//    the host side of the view: its size (IPlugFrame) and, for X11 views, the run loop
//    (Linux::IRunLoop exists on Linux only: the SDK defines its id there)
//---------------------------------------------------------

#ifdef Q_OS_LINUX
class Vst3EditorFrame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame, Linux::IRunLoop>> {
      std::map<Linux::IEventHandler*, QSocketNotifier*> _fds;
      std::map<Linux::ITimerHandler*, QTimer*> _timers;
#else
class Vst3EditorFrame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame>> {
#endif
      Vst3EditorWindow* _window;

   public:
      explicit Vst3EditorFrame(Vst3EditorWindow* w) : _window(w) {}
#ifdef Q_OS_LINUX
      ~Vst3EditorFrame()
            {
            for (auto& f : _fds)
                  delete f.second;
            for (auto& t : _timers)
                  delete t.second;
            }
#endif

      tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
            {
            if (!view || !newSize)
                  return kInvalidArgument;
            _window->resizeToView(newSize->getWidth(), newSize->getHeight());
            view->onSize(newSize);
            return kResultTrue;
            }

#ifdef Q_OS_LINUX
      tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override
            {
            QSocketNotifier* n = new QSocketNotifier(fd, QSocketNotifier::Read);
            QObject::connect(n, &QSocketNotifier::activated, [handler, fd]() { handler->onFDIsSet(fd); });
            _fds[handler] = n;
            return kResultTrue;
            }
      tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
            {
            auto i = _fds.find(handler);
            if (i == _fds.end())
                  return kResultFalse;
            i->second->deleteLater();
            _fds.erase(i);
            return kResultTrue;
            }
      tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval ms) override
            {
            QTimer* t = new QTimer;
            QObject::connect(t, &QTimer::timeout, [handler]() { handler->onTimer(); });
            t->start(int(ms));
            _timers[handler] = t;
            return kResultTrue;
            }
      tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override
            {
            auto i = _timers.find(handler);
            if (i == _timers.end())
                  return kResultFalse;
            i->second->stop();
            i->second->deleteLater();
            _timers.erase(i);
            return kResultTrue;
            }
#endif
      };

//---------------------------------------------------------
//   Vst3EditorWindow
//---------------------------------------------------------

Vst3EditorWindow::Vst3EditorWindow(IPlugView* view, const QString& title, QWidget* parent)
   : QWidget(parent, Qt::Window), _view(view)
      {
      setWindowTitle(title);
      setAttribute(Qt::WA_NativeWindow);
      setAttribute(Qt::WA_DeleteOnClose);
      _frame = new Vst3EditorFrame(this);
      _view->setFrame(_frame);
      ViewRect r;
      if (_view->getSize(&r) == kResultTrue)
            resizeToView(r.getWidth(), r.getHeight());
      else
            resize(800, 600);
      if (_view->canResize() != kResultTrue)
            setFixedSize(size());
      }

Vst3EditorWindow::~Vst3EditorWindow()
      {
      detach();
      delete _frame;
      }

void Vst3EditorWindow::detach()
      {
      if (!_view)
            return;
      if (_attached)
            _view->removed();
      _view->setFrame(nullptr);
      _view->release();
      _view = nullptr;
      _attached = false;
      }

// the view's size: pixels on Windows and Linux, points on macOS
void Vst3EditorWindow::resizeToView(int width, int height)
      {
#ifdef Q_OS_MAC
      const qreal scale = 1.0;
#else
      const qreal scale = devicePixelRatioF();
#endif
      _resizing = true;
      const QSize s(qRound(width / scale), qRound(height / scale));
      if (minimumSize() == maximumSize())
            setFixedSize(s);
      else
            resize(s);
      _resizing = false;
      }

void Vst3EditorWindow::attach()
      {
      if (_attached || !_view)
            return;
#if defined(Q_OS_WIN)
      FIDString type = kPlatformTypeHWND;
#elif defined(Q_OS_MAC)
      FIDString type = kPlatformTypeNSView;
#else
      FIDString type = kPlatformTypeX11EmbedWindowID;
#endif
      if (_view->isPlatformTypeSupported(type) != kResultTrue)
            return;
      if (FUnknownPtr<IPlugViewContentScaleSupport> scale = FUnknownPtr<IPlugViewContentScaleSupport>(_view))
            scale->setContentScaleFactor(float(devicePixelRatioF()));
      _attached = _view->attached(reinterpret_cast<void*>(winId()), type) == kResultTrue;
      }

void Vst3EditorWindow::showEvent(QShowEvent* e)
      {
      QWidget::showEvent(e);
      attach();
      }

void Vst3EditorWindow::closeEvent(QCloseEvent* e)
      {
      emit closed();
      detach();
      QWidget::closeEvent(e);
      }

void Vst3EditorWindow::resizeEvent(QResizeEvent* e)
      {
      QWidget::resizeEvent(e);
      if (_resizing || !_attached || !_view)
            return;
#ifdef Q_OS_MAC
      const qreal scale = 1.0;
#else
      const qreal scale = devicePixelRatioF();
#endif
      ViewRect r(0, 0, qRound(width() * scale), qRound(height() * scale));
      if (_view->checkSizeConstraint(&r) == kResultTrue)
            _view->onSize(&r);
      }

} // namespace Ms
