//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//  The script console: scriptconsole.h has the design.
//  Layout of this file: ScriptHighlighter, ScriptEdit (completion, error mark, keys), ScriptPreviewer (the worker
//  thread), ScriptConsole (the two tabs, running, preview, saved scripts).

#include "scriptconsole.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBlock>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include "musescore.h"
#include "preferences.h"
#include "scoreview.h"
#include "libmscore/score.h"
#include "libmscore/note.h"
#include "libmscore/part.h"

namespace Ms {

//---------------------------------------------------------
//   ScriptHighlighter
//---------------------------------------------------------

QColor ScriptHighlighter::kindColor(const QString& kind, bool dark)
      {
      struct C { const char* kind; const char* light; const char* dark; };
      static const C colors[] = {
            { "keyword",  "#7b3fb5", "#c79bf2" },
            { "query",    "#0b5cad", "#6cb6ff" },
            { "property", "#00695c", "#4fd1c0" },
            { "part",     "#a85400", "#f0a35e" },
            { "mark",     "#b0124f", "#ff7aa8" },
            { "duration", "#5d6f00", "#c3d45a" },
            { "dynamic",  "#8f3f71", "#e29ac6" },
            { "method",   "#1565c0", "#82b7f5" },
            { "builtin",  "#546e7a", "#a9bcc6" },
            { "variable", "#37474f", "#cfd8dc" },
            { "number",   "#1b7a3d", "#7ad69a" },
            { "string",   "#9c4a12", "#e8a878" },
            { "text",     "#9c4a12", "#e8a878" },
            { "comment",  "#7a7a7a", "#9a9a9a" },
            };
      for (const C& c : colors)
            if (kind == c.kind)
                  return QColor(dark ? c.dark : c.light);
      return QColor();
      }

QString ScriptHighlighter::kindName(const QString& kind)
      {
      if (kind == "keyword")  return tr("Lua keyword");
      if (kind == "query")    return tr("query");
      if (kind == "property") return tr("note property");
      if (kind == "part")     return tr("part");
      if (kind == "mark")     return tr("mark");
      if (kind == "duration") return tr("duration");
      if (kind == "dynamic")  return tr("dynamic");
      if (kind == "method")   return tr("selection");
      if (kind == "builtin")  return tr("Lua");
      if (kind == "variable") return tr("variable");
      if (kind == "text")     return tr("text");
      return kind;
      }

QTextCharFormat ScriptHighlighter::format(const QString& kind) const
      {
      QTextCharFormat f;
      const QColor c = kindColor(kind, _dark);
      if (c.isValid())
            f.setForeground(c);
      if (kind == "keyword" || kind == "query")
            f.setFontWeight(QFont::Bold);
      if (kind == "comment" || kind == "dynamic")
            f.setFontItalic(true);
      return f;
      }

void ScriptHighlighter::setWords(const QVector<QPair<QString, QString>>& words, bool dark)
      {
      _kinds.clear();
      for (const auto& w : words)
            _kinds.insert(w.first, w.second);
      _dark = dark;
      rehighlight();
      }

//---------------------------------------------------------
//   highlightBlock
//    block states: 0 code, 1 + n inside a long comment of level n, 100 + n inside a long string of level n
//---------------------------------------------------------

void ScriptHighlighter::highlightBlock(const QString& text)
      {
      const int n = text.size();
      int i = 0;
      int state = previousBlockState() < 0 ? 0 : previousBlockState();
      auto longOpen = [&text, n](int at) -> int {          // [==[ at `at`: its level, else -1
            if (at >= n || text[at] != '[')
                  return -1;
            int j = at + 1;
            while (j < n && text[j] == '=')
                  ++j;
            return j < n && text[j] == '[' ? j - at - 1 : -1;
            };
      auto longClose = [&text](int from, int level) -> int {    // the index after ]==], or -1
            const QString close = "]" + QString(level, '=') + "]";
            const int k = text.indexOf(close, from);
            return k < 0 ? -1 : k + close.size();
            };
      if (state != 0) {
            const bool comment = state < 100;
            const int end = longClose(0, comment ? state - 1 : state - 100);
            setFormat(0, end < 0 ? n : end, format(comment ? "comment" : "string"));
            if (end < 0) {
                  setCurrentBlockState(state);
                  return;
                  }
            i = end;
            state = 0;
            }
      QChar before;                                  // the last significant character before a word
      while (i < n) {
            const QChar c = text[i];
            if (c.isSpace()) {
                  ++i;
                  continue;
                  }
            if (c == '-' && i + 1 < n && text[i + 1] == '-') {
                  const int level = longOpen(i + 2);
                  if (level >= 0) {
                        const int end = longClose(i + 2, level);
                        setFormat(i, (end < 0 ? n : end) - i, format("comment"));
                        if (end < 0) {
                              state = 1 + level;
                              break;
                              }
                        i = end;
                        continue;
                        }
                  setFormat(i, n - i, format("comment"));
                  break;
                  }
            if (c == '"' || c == '\'') {
                  int j = i + 1;
                  while (j < n && text[j] != c)
                        j += text[j] == '\\' ? 2 : 1;
                  j = qMin(j + 1, n);
                  setFormat(i, j - i, format("string"));
                  i = j;
                  before = c;
                  continue;
                  }
            if (c == '[' && longOpen(i) >= 0) {
                  const int level = longOpen(i);
                  const int end = longClose(i + level + 2, level);
                  setFormat(i, (end < 0 ? n : end) - i, format("string"));
                  if (end < 0) {
                        state = 100 + level;
                        break;
                        }
                  i = end;
                  continue;
                  }
            if (c.isDigit() || (c == '.' && i + 1 < n && text[i + 1].isDigit())) {
                  int j = i + 1;
                  while (j < n && (text[j].isLetterOrNumber() || text[j] == '.'))
                        ++j;
                  setFormat(i, j - i, format("number"));
                  i = j;
                  before = c;
                  continue;
                  }
            if (c.isLetter() || c == '_') {
                  int j = i + 1;
                  while (j < n && (text[j].isLetterOrNumber() || text[j] == '_'))
                        ++j;
                  const QString word = text.mid(i, j - i);
                  QString kind = _kinds.value(word);
                  if (before == '.' || before == ':') {          // a field: only what a field can be
                        static const QStringList slurFields { "index", "length", "first", "last" };
                        if (kind != "method" && !(slurFields.contains(word)))
                              kind.clear();
                        else if (slurFields.contains(word) && kind != "method")
                              kind = "property";
                        else
                              kind = "method";
                        }
                  else if (kind == "method")
                        kind.clear();
                  if (!kind.isEmpty())
                        setFormat(i, j - i, format(kind));
                  i = j;
                  before = 'a';
                  continue;
                  }
            before = c;
            ++i;
            }
      setCurrentBlockState(state);
      }

//---------------------------------------------------------
//   CompletionDelegate
//---------------------------------------------------------

void CompletionDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
      {
      QStyledItemDelegate::initStyleOption(option, index);
      if (option->state & QStyle::State_Selected) {
            const QBrush text = option->palette.brush(QPalette::Active, QPalette::HighlightedText);
            option->palette.setBrush(QPalette::Text, text);
            option->palette.setBrush(QPalette::HighlightedText, text);
            option->palette.setBrush(QPalette::WindowText, text);
            if (index.column() == 1)
                  option->font.setItalic(true);
            }
      }

//---------------------------------------------------------
//   ScriptEdit
//---------------------------------------------------------

ScriptEdit::ScriptEdit(ScriptEngine* engine, bool singleLine, QWidget* parent)
   : QPlainTextEdit(parent), _engine(engine), _singleLine(singleLine)
      {
      QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
      setFont(f);
      setLineWrapMode(singleLine ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
      setTabChangesFocus(false);
      setMouseTracking(true);
      if (singleLine) {
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            setFixedHeight(QFontMetrics(f).lineSpacing() + 2 * frameWidth() + int(2 * document()->documentMargin()) + 2);
            }
      _highlighter = new ScriptHighlighter(document());

      _model = new QStandardItemModel(0, 3, this);
      _completer = new QCompleter(_model, this);
      _completer->setWidget(this);
      _completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);   // (the engine has filtered by context)
      QTreeView* popup = new QTreeView;
      popup->setRootIsDecorated(false);
      popup->setHeaderHidden(true);
      popup->setUniformRowHeights(true);
      popup->setAllColumnsShowFocus(true);
      popup->setSelectionBehavior(QAbstractItemView::SelectRows);
      popup->setFont(f);
      popup->setItemDelegate(new CompletionDelegate(popup));
      _completer->setPopup(popup);
      connect(_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated), this, &ScriptEdit::insertCompletion);
      reloadWords();
      }

void ScriptEdit::reloadWords()
      {
      const bool dark = palette().color(QPalette::Base).lightness() < 128;
      _highlighter->setWords(_engine->words(), dark);
      }

QString ScriptEdit::lineUnderCursor() const
      {
      QTextCursor c = textCursor();
      if (c.hasSelection())
            return c.selectedText().replace(QChar::ParagraphSeparator, '\n');
      return c.block().text();
      }

//---------------------------------------------------------
//   setError
//    a wavy line under the word the message names on that line ('mesure' …), else under the line
//---------------------------------------------------------

void ScriptEdit::setError(int line, const QString& message)
      {
      _errorLine = line;
      _errorText = message;
      updateErrorMark();
      }

void ScriptEdit::updateErrorMark()
      {
      QList<QTextEdit::ExtraSelection> marks;
      const QTextBlock block = _errorLine > 0 ? document()->findBlockByNumber(_errorLine - 1) : QTextBlock();
      if (block.isValid()) {
            QTextEdit::ExtraSelection s;
            s.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            s.format.setUnderlineColor(QColor("#d32f2f"));
            s.cursor = QTextCursor(block);
            int from = 0, length = block.text().size();
            const int q1 = _errorText.indexOf('\'');
            const int q2 = q1 < 0 ? -1 : _errorText.indexOf('\'', q1 + 1);
            if (q2 > q1 + 1) {
                  const QString word = _errorText.mid(q1 + 1, q2 - q1 - 1);
                  const int at = block.text().indexOf(word);
                  if (at >= 0) {
                        from = at;
                        length = word.size();
                        }
                  }
            s.cursor.setPosition(block.position() + from);
            s.cursor.setPosition(block.position() + from + qMax(1, length), QTextCursor::KeepAnchor);
            marks.append(s);
            }
      setExtraSelections(marks);
      }

//---------------------------------------------------------
//   showCompletion
//---------------------------------------------------------

void ScriptEdit::showCompletion(bool insertSingle)
      {
      const QTextCursor c = textCursor();
      _completion = _engine->complete(toPlainText(), c.position());
      if (_completion.items.isEmpty()) {
            _completer->popup()->hide();
            return;
            }
      if (insertSingle && _completion.items.size() == 1) {
            _model->setRowCount(0);
            QTextCursor t = textCursor();
            t.setPosition(_completion.from);
            t.setPosition(c.position(), QTextCursor::KeepAnchor);
            t.insertText(_completion.items[0].insert);
            setTextCursor(t);
            _completer->popup()->hide();
            return;
            }
      const bool dark = palette().color(QPalette::Base).lightness() < 128;
      _model->setRowCount(0);
      for (const ScriptCompletion::Item& it : _completion.items) {
            QStandardItem* word = new QStandardItem(it.word);
            QStandardItem* kind = new QStandardItem(ScriptHighlighter::kindName(it.kind));
            QStandardItem* help = new QStandardItem(it.help);
            const QColor col = ScriptHighlighter::kindColor(it.kind, dark);
            if (col.isValid()) {
                  word->setForeground(col);
                  kind->setForeground(col);
                  }
            QFont bold = font();
            bold.setBold(true);
            word->setFont(bold);
            word->setToolTip(it.help);
            _model->appendRow({ word, kind, help });
            }
      QTreeView* popup = static_cast<QTreeView*>(_completer->popup());
      for (int col = 0; col < 3; ++col)
            popup->resizeColumnToContents(col);
      QRect r = cursorRect();
      r.setWidth(qMin(width(), popup->columnWidth(0) + popup->columnWidth(1) + popup->columnWidth(2) + 40));
      _completer->complete(r);
      popup->setCurrentIndex(_model->index(0, 0));
      }

void ScriptEdit::insertCompletion(const QModelIndex& index)
      {
      const int row = index.row();
      if (row < 0 || row >= _completion.items.size())
            return;
      QTextCursor t = textCursor();
      const int end = t.position();
      t.setPosition(qMin(_completion.from, end));
      t.setPosition(end, QTextCursor::KeepAnchor);
      t.insertText(_completion.items[row].insert);
      setTextCursor(t);
      }

//---------------------------------------------------------
//   keyPressEvent
//---------------------------------------------------------

void ScriptEdit::keyPressEvent(QKeyEvent* e)
      {
      const bool ctrl  = e->modifiers() & Qt::ControlModifier;
      const bool shift = e->modifiers() & Qt::ShiftModifier;
      const bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
      if (_completer->popup()->isVisible()) {
            switch (e->key()) {
                  case Qt::Key_Return:
                  case Qt::Key_Enter:
                  case Qt::Key_Tab:
                        insertCompletion(_completer->popup()->currentIndex());
                        _completer->popup()->hide();
                        return;
                  case Qt::Key_Escape:
                        _completer->popup()->hide();
                        return;
                  default:
                        break;
                  }
            }
      if (enter && ctrl && shift) {
            emit runAll();
            return;
            }
      if (enter && (ctrl || (_singleLine && !shift))) {
            emit run(_singleLine ? toPlainText() : lineUnderCursor());
            return;
            }
      if (e->key() == Qt::Key_Escape) {
            emit escape();
            return;
            }
      if (ctrl && e->key() == Qt::Key_F) {
            emit findRequested();
            return;
            }
      if (ctrl && e->key() == Qt::Key_Space) {
            showCompletion(false);
            return;
            }
      if (e->key() == Qt::Key_Tab && !ctrl) {
            // after a word or a dot: complete; at the start of a line: indent
            const QString before = textCursor().block().text().left(textCursor().positionInBlock());
            if (!before.trimmed().isEmpty() && !before.endsWith(' ')) {
                  showCompletion(true);
                  return;
                  }
            insertPlainText("  ");
            return;
            }
      if (_singleLine && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && document()->blockCount() == 1) {
            emit history(e->key() == Qt::Key_Up ? -1 : 1);
            return;
            }
      QPlainTextEdit::keyPressEvent(e);
      // by itself: after two letters of a word, or right after a dot
      const QString typed = e->text();
      if (!typed.isEmpty() && (typed[0].isLetterOrNumber() || typed[0] == '_' || typed[0] == '.' || typed[0] == ':')) {
            const QString before = textCursor().block().text().left(textCursor().positionInBlock());
            int letters = 0;
            for (int i = before.size() - 1; i >= 0 && (before[i].isLetterOrNumber() || before[i] == '_'); --i)
                  ++letters;
            const bool afterDot = letters == 0 && before.endsWith('.') && before.size() > 1 && !before[before.size() - 2].isDigit()
               && before[before.size() - 2] != '.';
            if (letters >= 2 || afterDot || (letters >= 1 && _completer->popup()->isVisible()))
                  showCompletion(false);
            else
                  _completer->popup()->hide();
            }
      else if (_completer->popup()->isVisible()) {
            // Backspace inside a word keeps the list up to date; anything else (a bracket, a space) ends the word
            const QString before = textCursor().block().text().left(textCursor().positionInBlock());
            if (e->key() == Qt::Key_Backspace && !before.isEmpty() && (before.back().isLetterOrNumber() || before.back() == '_'))
                  showCompletion(false);
            else
                  _completer->popup()->hide();
            }
      }

void ScriptEdit::focusOutEvent(QFocusEvent* e)
      {
      QPlainTextEdit::focusOutEvent(e);
      }

//---------------------------------------------------------
//   event
//    tool tips: the error on its line, else the help of the word under the mouse
//---------------------------------------------------------

bool ScriptEdit::event(QEvent* e)
      {
      if (e->type() == QEvent::ToolTip) {
            QHelpEvent* he = static_cast<QHelpEvent*>(e);
            QTextCursor c = cursorForPosition(he->pos());
            QString tip;
            if (_errorLine > 0 && c.blockNumber() == _errorLine - 1)
                  tip = _errorText;
            else {
                  c.select(QTextCursor::WordUnderCursor);
                  const QString word = c.selectedText();
                  if (!word.isEmpty()) {
                        QString kind;
                        const QString help = _engine->help(word, &kind);
                        if (!help.isEmpty())
                              tip = QString("<b>%1</b> (%2)<br>%3").arg(word.toHtmlEscaped(), ScriptHighlighter::kindName(kind), help.toHtmlEscaped());
                        }
                  }
            if (tip.isEmpty())
                  QToolTip::hideText();
            else
                  QToolTip::showText(he->globalPos(), tip, this);
            return true;
            }
      if (e->type() == QEvent::ShortcutOverride) {
            // the editor's own keys win over MuseScore's application shortcuts (Ctrl+F is Find / Go to, Esc, letters …)
            QKeyEvent* ke = static_cast<QKeyEvent*>(e);
            const Qt::KeyboardModifiers mods = ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            const int k = ke->key();
            const bool ours = mods == Qt::NoModifier
               || (mods == Qt::ControlModifier && (k == Qt::Key_F || k == Qt::Key_Space || k == Qt::Key_Return || k == Qt::Key_Enter
                                                   || k == Qt::Key_Z || k == Qt::Key_Y || k == Qt::Key_A || k == Qt::Key_C
                                                   || k == Qt::Key_V || k == Qt::Key_X));
            if (ours) {
                  e->accept();
                  return true;
                  }
            }
      if (e->type() == QEvent::PaletteChange)
            reloadWords();
      return QPlainTextEdit::event(e);
      }

//---------------------------------------------------------
//   ScriptPreviewer
//---------------------------------------------------------

ScriptPreviewWorker::~ScriptPreviewWorker()
      {
      delete _engine;
      }

void ScriptPreviewWorker::makeEngine()
      {
      _engine = new ScriptEngine;
      _engine->setInstructionLimit(ScriptPreviewer::BUDGET);
      _engine->setMemoryLimit(size_t(256) * 1024 * 1024);
      }

void ScriptPreviewWorker::run(int serial, const ScriptSnapshot& snapshot, const QString& code)
      {
      ScriptResult r;
      if (!_engine->setSnapshot(snapshot))
            r.error = _engine->initError();
      else
            r = _engine->run(code, ScriptEngine::Preview);
      emit done(serial, r);
      }

ScriptPreviewer::ScriptPreviewer(QObject* parent)
   : QObject(parent)
      {
      qRegisterMetaType<Ms::ScriptSnapshot>("Ms::ScriptSnapshot");
      qRegisterMetaType<Ms::ScriptResult>("Ms::ScriptResult");
      _worker = new ScriptPreviewWorker;
      _worker->makeEngine();                    // (made here, used only on the thread from now on, cancel() apart)
      _worker->moveToThread(&_thread);
      connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
      connect(this, &ScriptPreviewer::start, _worker, &ScriptPreviewWorker::run);
      connect(_worker, &ScriptPreviewWorker::done, this, &ScriptPreviewer::done);
      _thread.setObjectName("script preview");
      _thread.start(QThread::LowPriority);
      }

ScriptPreviewer::~ScriptPreviewer()
      {
      cancel();
      _thread.quit();
      _thread.wait();
      }

void ScriptPreviewer::cancel()
      {
      _worker->engine()->cancel();
      }

void ScriptPreviewer::request(int serial, const ScriptSnapshot& snapshot, const QString& code)
      {
      cancel();                                 // a run in flight stops at its next check; its result is dropped by serial
      emit start(serial, snapshot, code);
      }

//---------------------------------------------------------
//   ScriptConsole
//---------------------------------------------------------

QString ScriptConsole::scriptsFolder()
      {
      // as MuseScore's other folders (Plugins, Styles): a preference, relative paths from the home folder
      QFileInfo f(preferences.getString(PREF_APP_PATHS_MYSCRIPTS));
      if (f.isRelative())
            f.setFile(QDir::home(), preferences.getString(PREF_APP_PATHS_MYSCRIPTS));
      return f.absoluteFilePath();
      }

QString ScriptConsole::scriptFile(const QString& name)
      {
      return scriptsFolder() + "/" + name + ".lua";
      }

ScriptConsole::ScriptConsole(QWidget* parent)
   : QDockWidget(parent)
      {
      setObjectName("ScriptConsole");
      setWindowTitle(tr("Script Console"));
      setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
      // The window title stays (the dock's tab label next to the Timeline, the View menu entry, a floating window's
      // frame). The title bar's caption repeated it right above the Console / Scripts tabs, so the bar goes: an empty
      // widget in its place, and the float and close buttons sit at the right end of the tab row instead.
      setTitleBarWidget(new QWidget(this));

      _engine = new ScriptEngine;
      _previewer = new ScriptPreviewer(this);
      connect(_previewer, &ScriptPreviewer::done, this, &ScriptConsole::previewDone);

      _tabs = new QTabWidget(this);
      _tabs->setDocumentMode(true);
      setWidget(_tabs);
      {
      QWidget* corner = new QWidget;
      QHBoxLayout* kl = new QHBoxLayout(corner);
      kl->setContentsMargins(0, 0, 0, 0);
      kl->setSpacing(0);
      QToolButton* floatButton = new QToolButton;
      floatButton->setAutoRaise(true);
      floatButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
      floatButton->setToolTip(tr("Float"));
      floatButton->setAccessibleName(tr("Float"));
      QToolButton* closeButton = new QToolButton;
      closeButton->setAutoRaise(true);
      closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
      closeButton->setToolTip(tr("Close"));
      closeButton->setAccessibleName(tr("Close"));
      kl->addWidget(floatButton);
      kl->addWidget(closeButton);
      _tabs->setCornerWidget(corner, Qt::TopRightCorner);
      connect(floatButton, &QToolButton::clicked, this, [this]() { setFloating(!isFloating()); });
      connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
      }

      // --- Console
      QWidget* console = new QWidget;
      QVBoxLayout* cl = new QVBoxLayout(console);
      cl->setContentsMargins(4, 4, 4, 4);
      cl->setSpacing(2);
      _output = new QPlainTextEdit;
      _output->setReadOnly(true);
      _output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
      _output->setMaximumBlockCount(5000);
      _output->setAccessibleName(tr("Output"));
      cl->addWidget(_output, 1);
      _input = new ScriptEdit(_engine, true);
      _input->setPlaceholderText(tr("Type a line, for example findall(strings, measure <= 8, slur.first).add(accent), and press Enter"));
      _input->setAccessibleName(tr("Console input"));
      cl->addWidget(_input);
      _consoleStatus = new QLabel;
      _consoleStatus->setTextFormat(Qt::PlainText);
      cl->addWidget(_consoleStatus);
      _tabs->addTab(console, tr("Console"));

      // --- Scripts
      QSplitter* scripts = new QSplitter(Qt::Horizontal);
      QWidget* left = new QWidget;
      QVBoxLayout* ll = new QVBoxLayout(left);
      ll->setContentsMargins(4, 4, 0, 4);
      ll->setSpacing(2);
      _list = new QListWidget;
      _list->setAccessibleName(tr("Saved scripts"));
      ll->addWidget(_list, 1);
      QHBoxLayout* buttons = new QHBoxLayout;
      QToolButton* newButton = new QToolButton;
      newButton->setText(tr("New"));
      newButton->setToolTip(tr("Start a new script"));
      QToolButton* deleteButton = new QToolButton;
      deleteButton->setText(tr("Delete"));
      deleteButton->setToolTip(tr("Delete the selected script"));
      QToolButton* runButton = new QToolButton;
      runButton->setText(tr("Run"));
      runButton->setToolTip(tr("Run the script on the score (Ctrl+Shift+Enter)"));
      buttons->addWidget(newButton);
      buttons->addWidget(deleteButton);
      buttons->addStretch(1);
      buttons->addWidget(runButton);
      ll->addLayout(buttons);
      scripts->addWidget(left);

      QWidget* right = new QWidget;
      QVBoxLayout* rl = new QVBoxLayout(right);
      rl->setContentsMargins(0, 4, 4, 4);
      rl->setSpacing(2);
      _editor = new ScriptEdit(_engine, false);
      _editor->setAccessibleName(tr("Script"));
      rl->addWidget(_editor, 1);
      _findBar = new QWidget;
      QHBoxLayout* fl = new QHBoxLayout(_findBar);
      fl->setContentsMargins(0, 0, 0, 0);
      fl->addWidget(new QLabel(tr("Find:")));
      _find = new QLineEdit;
      _find->setClearButtonEnabled(true);
      fl->addWidget(_find, 1);
      _findBar->hide();
      rl->addWidget(_findBar);
      QShortcut* closeFind = new QShortcut(QKeySequence(Qt::Key_Escape), _findBar, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
      connect(closeFind, &QShortcut::activated, this, [this]() { _findBar->hide(); _editor->setFocus(); });
      _scriptStatus = new QLabel;
      _scriptStatus->setTextFormat(Qt::PlainText);
      rl->addWidget(_scriptStatus);
      scripts->addWidget(right);
      scripts->setStretchFactor(0, 0);
      scripts->setStretchFactor(1, 1);
      _tabs->addTab(scripts, tr("Scripts"));

      connect(_input, &ScriptEdit::run, this, &ScriptConsole::runConsoleLine);
      connect(_input, &ScriptEdit::runAll, this, [this]() { runConsoleLine(_input->toPlainText()); });
      connect(_input, &ScriptEdit::history, this, &ScriptConsole::historyStep);
      connect(_input, &ScriptEdit::escape, this, &ScriptConsole::toScore);
      connect(_input, &QPlainTextEdit::textChanged, this, &ScriptConsole::schedulePreview);
      connect(_editor, &ScriptEdit::run, this, &ScriptConsole::runScriptPart);
      connect(_editor, &ScriptEdit::runAll, this, &ScriptConsole::runScript);
      connect(_editor, &ScriptEdit::escape, this, &ScriptConsole::toScore);
      connect(_editor, &ScriptEdit::findRequested, this, &ScriptConsole::showFind);
      connect(_editor, &QPlainTextEdit::textChanged, this, &ScriptConsole::schedulePreview);
      connect(_find, &QLineEdit::returnPressed, this, &ScriptConsole::findNext);
      connect(_find, &QLineEdit::textChanged, this, &ScriptConsole::findNext);
      connect(newButton, &QToolButton::clicked, this, &ScriptConsole::newScript);
      connect(deleteButton, &QToolButton::clicked, this, &ScriptConsole::deleteScript);
      connect(runButton, &QToolButton::clicked, this, &ScriptConsole::runScript);
      connect(_list, &QListWidget::currentItemChanged, this, &ScriptConsole::scriptSelected);
      connect(_tabs, &QTabWidget::currentChanged, this, [this](int) { schedulePreview(); });

      _previewTimer.setSingleShot(true);
      _previewTimer.setInterval(300);
      connect(&_previewTimer, &QTimer::timeout, this, &ScriptConsole::preview);

      QSettings s;
      _history = s.value("scriptConsole/history").toStringList();
      _tabs->setCurrentIndex(s.value("scriptConsole/tab", 0).toInt());
      refreshList(s.value("scriptConsole/script").toString());
      if (!_engine->valid())
            print(_engine->initError(), "error");
      }

ScriptConsole::~ScriptConsole()
      {
      delete _previewer;                        // (its thread ends before the engine's users go)
      _previewer = nullptr;
      delete _engine;
      }

void ScriptConsole::writeSettings()
      {
      saveCurrent();
      QSettings s;
      s.setValue("scriptConsole/history", QStringList(_history.mid(qMax(0, _history.size() - 200))));
      s.setValue("scriptConsole/tab", _tabs->currentIndex());
      s.setValue("scriptConsole/script", _currentScript);
      }

Score* ScriptConsole::score() const
      {
      return mscore ? mscore->currentScore() : nullptr;
      }

ScriptEdit* ScriptConsole::activeEdit() const
      {
      return _tabs->currentIndex() == 0 ? _input : _editor;
      }

QLabel* ScriptConsole::activeStatus() const
      {
      return _tabs->currentIndex() == 0 ? _consoleStatus : _scriptStatus;
      }

void ScriptConsole::focusInput()
      {
      if (isFloating())
            activateWindow();
      activeEdit()->setFocus(Qt::ShortcutFocusReason);
      }

void ScriptConsole::toScore()
      {
      markNotes({});
      if (mscore && mscore->currentScoreView())
            mscore->currentScoreView()->setFocus();
      }

void ScriptConsole::closeEvent(QCloseEvent* e)
      {
      writeSettings();
      emit closed(false);
      QDockWidget::closeEvent(e);
      }

void ScriptConsole::showEvent(QShowEvent* e)
      {
      QDockWidget::showEvent(e);
      scoreChanged();
      }

void ScriptConsole::hideEvent(QHideEvent* e)
      {
      markNotes({});
      QDockWidget::hideEvent(e);
      }

//---------------------------------------------------------
//   print
//---------------------------------------------------------

void ScriptConsole::print(const QString& text, const QString& kind)
      {
      _log.append({ text, kind });
      if (_log.size() > 6000)
            _log.remove(0, _log.size() - 5000);
      appendOutput(text, kind);
      }

void ScriptConsole::appendOutput(const QString& text, const QString& kind)
      {
      const QPalette pal = _output->palette();
      const bool dark = pal.color(QPalette::Base).lightness() < 128;
      QTextCharFormat f;
      if (kind == "input")
            f.setForeground(pal.color(QPalette::Disabled, QPalette::Text));
      else if (kind == "error")
            f.setForeground(errorColor(pal));
      else if (kind == "query")
            f.setForeground(ScriptHighlighter::kindColor("query", dark));
      else
            f.setForeground(pal.color(QPalette::Text));
      QTextCursor c(_output->document());
      c.movePosition(QTextCursor::End);
      if (!_output->document()->isEmpty())
            c.insertBlock();
      c.insertText(text, f);
      _output->verticalScrollBar()->setValue(_output->verticalScrollBar()->maximum());
      }

//---------------------------------------------------------
//   errorColor
//    the one colour of an error, in the output and the status lines, for the light and the dark theme
//---------------------------------------------------------

QColor ScriptConsole::errorColor(const QPalette& pal)
      {
      return QColor(pal.color(QPalette::Base).lightness() < 128 ? "#ff8a80" : "#c62828");
      }

void ScriptConsole::setStatus(QLabel* label, const QString& text, bool error)
      {
      if (label == _consoleStatus)
            _consoleError = error;
      else
            _scriptError = error;
      label->setStyleSheet(error ? QString("color: %1;").arg(errorColor(label->palette()).name()) : QString());
      label->setText(text);
      }

//---------------------------------------------------------
//   recolour
//    after a theme switch the output already printed is drawn again from the log, in the new colours
//---------------------------------------------------------

void ScriptConsole::recolour()
      {
      const bool dark = _output->palette().color(QPalette::Base).lightness() < 128;
      if (dark == _logDark)
            return;
      _logDark = dark;
      const int scroll = _output->verticalScrollBar()->value();
      const bool atEnd = scroll >= _output->verticalScrollBar()->maximum() - 2;
      _output->clear();
      for (const auto& e : _log)
            appendOutput(e.first, e.second);
      if (!atEnd)
            _output->verticalScrollBar()->setValue(scroll);
      if (_consoleError)
            setStatus(_consoleStatus, _consoleStatus->text(), true);
      if (_scriptError)
            setStatus(_scriptStatus, _scriptStatus->text(), true);
      }

void ScriptConsole::changeEvent(QEvent* e)
      {
      QDockWidget::changeEvent(e);
      if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::StyleChange)
            QTimer::singleShot(0, this, &ScriptConsole::recolour);      // (once the new palette has reached the children)
      }

//---------------------------------------------------------
//   execute
//    one run = one undo step; its output goes to the Console tab's history
//---------------------------------------------------------

bool ScriptConsole::execute(const QString& code, bool console)
      {
      if (code.trimmed().isEmpty())
            return false;
      Score* s = score();
      _previewTimer.stop();
      ++_serial;                                // a preview in flight is out of date
      _previewer->cancel();
      markNotes({});
      ScriptEdit* edit = console ? _input : _editor;
      QLabel* status = console ? _consoleStatus : _scriptStatus;
      if (!s) {
            setStatus(status, tr("No score is open."), true);
            return false;
            }
      for (const QString& line : code.split('\n'))
            print((console ? "> " : "  ") + line, "input");
      const int flags = console ? (ScriptEngine::Persist | ScriptEngine::Echo) : 0;
      const ScriptResult r = _engine->runOnScore(s, code, flags);
      for (const ScriptLog& l : r.log)
            print(l.text, l.kind);
      if (!r.ok())
            print(r.error, "error");
      else if (r.changes > 0)
            print(r.changes == 1 ? tr("1 change to the score") : tr("%1 changes to the score").arg(r.changes), "query");
      mscore->endCmd();                         // the window follows the edit (inspector, undo, repaint)
      showResult(r, edit, status, false);
      if (!console) {
            // the Scripts tab stays where it is and its status line keeps the result until the text changes
            // (the owner, 2026-11-10: "30 changes to the score" or the error)
            if (r.ok())
                  setStatus(status, r.changes == 0 ? tr("No changes to the score")
                                  : r.changes == 1 ? tr("1 change to the score") : tr("%1 changes to the score").arg(r.changes), false);
            _scriptHold = true;
            _scriptHoldText = code;
            _scriptHoldFull = _editor->toPlainText();
            }
      scoreChanged();
      edit->setFocus();                         // (the edit's update gave the keyboard to the score view)
      return true;
      }

//---------------------------------------------------------
//   showResult
//---------------------------------------------------------

void ScriptConsole::showResult(const ScriptResult& r, ScriptEdit* edit, QLabel* status, bool preview)
      {
      if (!r.ok()) {
            edit->setError(r.errorLine, r.error);
            setStatus(status, r.error, true);   // (the same error colour typing or running: the owner, 2026-11-10)
            return;
            }
      edit->setError(0, QString());
      QStringList parts;
      // (no %n: the plural forms would need a translation file even in English)
      const int notes = r.matched.size();
      if (notes > 0 || preview)
            parts << (notes == 1 ? tr("1 note") : tr("%1 notes").arg(notes));
      if (r.changes > 0) {
            if (preview)
                  parts << (r.changes == 1 ? tr("1 change when run") : tr("%1 changes when run").arg(r.changes));
            else
                  parts << (r.changes == 1 ? tr("1 change") : tr("%1 changes").arg(r.changes));
            }
      setStatus(status, parts.join(", "), false);
      }

void ScriptConsole::runConsoleLine(const QString& line)
      {
      if (line.trimmed().isEmpty())
            return;
      if (_history.isEmpty() || _history.last() != line)
            _history.append(line);
      _historyPos = -1;
      _historyDraft.clear();
      // a line that ran, failed or not, is done: the input clears and Up brings it back from the history
      if (execute(line, true))
            _input->clear();
      }

void ScriptConsole::runScriptPart(const QString& code)
      {
      saveCurrent();
      execute(code, false);
      }

void ScriptConsole::runScript()
      {
      saveCurrent();
      execute(_editor->toPlainText(), false);
      }

void ScriptConsole::historyStep(int direction)
      {
      if (_history.isEmpty())
            return;
      if (_historyPos < 0) {
            if (direction > 0)
                  return;
            _historyDraft = _input->toPlainText();
            _historyPos = _history.size();
            }
      _historyPos += direction;
      if (_historyPos >= _history.size()) {
            _historyPos = -1;
            _input->setPlainText(_historyDraft);
            }
      else {
            _historyPos = qMax(0, _historyPos);
            _input->setPlainText(_history[_historyPos]);
            }
      _input->moveCursor(QTextCursor::End);
      }

//---------------------------------------------------------
//   preview
//---------------------------------------------------------

void ScriptConsole::schedulePreview()
      {
      if (_loading || !isVisible())
            return;
      _previewTimer.start();
      }

void ScriptConsole::refreshWords(Score* s)
      {
      // the sections are the score's: new words (colours, completion) when its parts change
      QString sig;
      for (const Part* p : s->masterScore()->parts())
            sig += p->partName() + '|' + p->instrumentId() + ';';
      if (sig == _wordsSignature)
            return;
      _wordsSignature = sig;
      _engine->setSnapshot(ScriptEngine::snapshot(s));
      _input->reloadWords();
      _editor->reloadWords();
      }

void ScriptConsole::scoreChanged()
      {
      if (!isVisible())
            return;
      ++_serial;                                // a preview in flight is of the score as it was
      _previewer->cancel();
      markNotes({});
      if (Score* s = score())
            refreshWords(s);
      schedulePreview();
      }

void ScriptConsole::preview()
      {
      ScriptEdit* edit = activeEdit();
      QLabel* status = activeStatus();
      const QString code = edit->toPlainText();
      Score* s = score();
      if (edit == _editor && _scriptHold && code == _scriptHoldFull)      // the run's result stays: nothing changed in the text
            return;
      if (edit == _editor)
            _scriptHold = false;
      ++_serial;
      if (!s || code.trimmed().isEmpty()) {
            _previewer->cancel();
            markNotes({});
            edit->setError(0, QString());
            setStatus(status, QString(), false);
            return;
            }
      _previewSnapshot = ScriptEngine::snapshot(s);
      if (edit == _input) {
            // the console's line sees the console's variables: on this thread, with the preview's small budget
            const qint64 limit = _engine->instructionLimit();
            _engine->setInstructionLimit(ScriptPreviewer::BUDGET);
            _engine->setSnapshot(_previewSnapshot);
            const ScriptResult r = _engine->run(code, ScriptEngine::Preview | ScriptEngine::Persist | ScriptEngine::Echo);
            _engine->setInstructionLimit(limit);
            previewDone(_serial, r);
            }
      else
            _previewer->request(_serial, _previewSnapshot, code);
      }

void ScriptConsole::previewDone(int serial, const ScriptResult& r)
      {
      if (serial != _serial)                    // the text or the score changed meanwhile
            return;
      QVector<Note*> notes;
      for (int id : r.matched)
            if (id >= 1 && id <= _previewSnapshot.notes.size())
                  notes.append(_previewSnapshot.notes[id - 1].note);
      markNotes(r.ok() ? notes : QVector<Note*>());
      showResult(r, activeEdit(), activeStatus(), true);
      }

//---------------------------------------------------------
//   markNotes
//    the matched notes in the score view; as rectangles, so that nothing points into an edited score
//---------------------------------------------------------

void ScriptConsole::markNotes(const QVector<Note*>& notes)
      {
      ScoreView* view = mscore ? mscore->currentScoreView() : nullptr;
      if (!view)
            return;
      QVector<QRectF> rects;
      const bool excerpt = view->score() && !view->score()->isMaster();
      for (Note* n : notes) {
            Note* shown = n;
            if (excerpt) {                      // a part is shown: the note's copy there
                  shown = nullptr;
                  for (ScoreElement* e : n->linkList())
                        if (e->score() == view->score())
                              shown = toNote(e);
                  }
            if (shown)
                  rects.append(shown->canvasBoundingRect());
            }
      view->setScriptPreview(rects);
      }

//---------------------------------------------------------
//   saved scripts
//    <data folder>/scripts/<name>.lua
//---------------------------------------------------------

void ScriptConsole::refreshList(const QString& select)
      {
      _loading = true;
      _list->clear();
            QStringList names;
      for (const QString& f : QDir(scriptsFolder()).entryList({ "*.lua" }, QDir::Files, QDir::Name | QDir::IgnoreCase))
            names << f.left(f.size() - 4);
      names.sort(Qt::CaseInsensitive);
      _list->addItems(names);
      _loading = false;
      const QList<QListWidgetItem*> found = select.isEmpty() ? QList<QListWidgetItem*>() : _list->findItems(select, Qt::MatchExactly);
      if (!found.isEmpty())
            _list->setCurrentItem(found.first());
      }

bool ScriptConsole::saveCurrent()
      {
      if (_currentScript.isEmpty() || !_editor->document()->isModified())
            return true;
      QDir().mkpath(scriptsFolder());
      QFile f(scriptsFolder() + "/" + _currentScript + ".lua");
      if (!f.open(QIODevice::WriteOnly)) {
            setStatus(_scriptStatus, tr("Cannot write %1").arg(f.fileName()), true);
            return false;
            }
      f.write(_editor->toPlainText().toUtf8());
      _editor->document()->setModified(false);
      return true;
      }

void ScriptConsole::loadScript(const QString& name)
      {
      QFile f(scriptFile(name));
      if (!f.open(QIODevice::ReadOnly))
            return;
      _loading = true;
      _editor->setPlainText(QString::fromUtf8(f.readAll()));
      _editor->document()->setModified(false);
      _loading = false;
      _currentScript = name;
      schedulePreview();
      }

void ScriptConsole::scriptSelected(QListWidgetItem* current, QListWidgetItem*)
      {
      if (_loading || !current || current->text() == _currentScript)
            return;
      saveCurrent();
      loadScript(current->text());
      }

void ScriptConsole::newScript()
      {
      bool ok = false;
      QString name = QInputDialog::getText(this, tr("New Script"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
      if (!ok || name.isEmpty())
            return;
      name.remove(QRegularExpression("[\\\\/:*?\"<>|]"));
      if (name.isEmpty())
            return;
      if (QFile::exists(scriptFile(name))) {
            QMessageBox::warning(this, tr("New Script"), tr("A script named \"%1\" already exists.").arg(name));
            return;
            }
      saveCurrent();
      // what is in the editor without a name becomes the new script's text
      const QString text = _currentScript.isEmpty() ? _editor->toPlainText() : QString();
      QDir().mkpath(scriptsFolder());
      QFile f(scriptsFolder() + "/" + name + ".lua");
      if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("New Script"), tr("Cannot write %1").arg(f.fileName()));
            return;
            }
      f.write(text.toUtf8());
      f.close();
      _currentScript.clear();
      refreshList(name);
      _editor->setFocus();
      }

void ScriptConsole::deleteScript()
      {
      QListWidgetItem* item = _list->currentItem();
      if (!item)
            return;
      const QString name = item->text();
      if (QMessageBox::question(this, tr("Delete Script"), tr("Delete the script \"%1\"?").arg(name),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
      QFile::remove(scriptFile(name));
      if (name == _currentScript) {
            _currentScript.clear();
            _loading = true;
            _editor->clear();
            _editor->document()->setModified(false);
            _loading = false;
            }
      refreshList(_currentScript);
      }

//---------------------------------------------------------
//   find
//---------------------------------------------------------

void ScriptConsole::showFind()
      {
      _findBar->show();
      const QString sel = _editor->textCursor().selectedText();
      if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            _find->setText(sel);
      _find->setFocus();
      _find->selectAll();
      }

void ScriptConsole::findNext()
      {
      const QString text = _find->text();
      if (text.isEmpty())
            return;
      QTextCursor c = _editor->textCursor();
      if (c.hasSelection() && c.selectedText().compare(text, Qt::CaseInsensitive) != 0) {
            c.setPosition(c.selectionStart());              // typing on: search again from the match's start
            _editor->setTextCursor(c);
            }
      if (!_editor->find(text)) {
            _editor->moveCursor(QTextCursor::Start);        // wrap around
            if (!_editor->find(text))
                  _editor->setTextCursor(c);
            }
      }

} // namespace Ms
