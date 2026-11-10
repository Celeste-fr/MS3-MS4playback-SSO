//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __SCRIPTCONSOLE_H__
#define __SCRIPTCONSOLE_H__

#include <QDockWidget>
#include <QStyledItemDelegate>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QThread>
#include <QTimer>
#include <QHash>
#include "libmscore/script/scriptengine.h"

class QCompleter;
class QStandardItemModel;
class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QModelIndex;

namespace Ms {

class Score;

//---------------------------------------------------------
//   The script console (View › Script Console, Ctrl+Shift+P)
//
//   A dock at the bottom of the window, in tabs with the Timeline, for score scripts in Lua (the language and its
//   host: libmscore/script/scriptengine.h, scorescript.lua). Two tabs:
//
//    Console   a REPL: the output history above one input line. Enter runs the line on the current score (one
//              undo step per line), variables stay from line to line (ScriptEngine::Persist), a line that is an
//              expression prints its value (Echo). Up / Down walk the history (kept between sessions).
//    Scripts   a multi-line editor and the saved scripts: plain .lua files, one per script, in
//              the Scripts folder (Preferences › General, PREF_APP_PATHS_MYSCRIPTS, ScriptConsole::scriptsFolder), named as in the list. Ctrl+Enter runs the
//              line under the cursor (or the selected text), Ctrl+Shift+Enter the whole script; each run is one
//              undo step and starts with fresh variables. Ctrl+F finds. A changed script is saved when another
//              one is opened, when it runs and when MuseScore closes.
//   In both, Esc gives the keyboard back to the score.
//
//   The editor (ScriptEdit, a QPlainTextEdit):
//    - ScriptHighlighter gives each kind of word its own colour: Lua keyword, query function, note property,
//      part / section, mark, duration, dynamic, selection method, Lua's own functions, number, text, comment. The
//      words come from the engine (ScriptEngine::words: the sections are the current score's);
//    - completion (Tab, Ctrl+Space, or by itself after two letters): a QCompleter popup filled from
//      ScriptEngine::complete, which knows the context (after slur. its fields, inside add( the marks, in a query
//      this score's parts first …); every item shows its kind and a line of help;
//    - the error of the line being typed is underlined (a wavy line under the word the message names, else the
//      whole line), with the message, did-you-mean included, as its tool tip and in the status line;
//    - hovering a word shows its help.
//
//   Live preview: while typing, the text runs as a PREVIEW (no edit, no undo step, nothing persistent), 300 ms
//   after the last key: the notes its queries match are marked in the score view (ScoreView::setScriptPreview, a
//   tint behind each notehead) and counted in the status line ("12 notes, 4 changes"). The Scripts tab's preview
//   runs on a worker thread (ScriptPreviewer: its own ScriptEngine on a ScriptSnapshot made on the GUI thread,
//   a small instruction budget, cancelled when the text changes again); the Console's line runs on the GUI
//   thread with the same small budget, because it must see the console's variables, which live in the GUI
//   thread's engine. A result is dropped when the text or the score changed meanwhile.
//
//   Scripts always act on the master score, never on a part's excerpt (ScriptEngine::runOnScore).
//
//   Untested until the first build with a window: HANDOFF.md › Lua console.
//---------------------------------------------------------

//---------------------------------------------------------
//   ScriptHighlighter
//---------------------------------------------------------

class ScriptHighlighter : public QSyntaxHighlighter {
      Q_OBJECT

      QHash<QString, QString> _kinds;           // word -> kind
      bool _dark { false };

      QTextCharFormat format(const QString& kind) const;

   protected:
      void highlightBlock(const QString& text) override;

   public:
      ScriptHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}
      void setWords(const QVector<QPair<QString, QString>>& words, bool dark);
      static QColor kindColor(const QString& kind, bool dark);
      static QString kindName(const QString& kind);   // as the completion list shows it
      };

//---------------------------------------------------------
//   CompletionDelegate
//    the popup's rows are coloured by kind; the selected row takes the palette's highlighted text colour
//    (a kind colour on the selection colour was hard to read), the kind column in italics so that it still shows
//---------------------------------------------------------

class CompletionDelegate : public QStyledItemDelegate {
   protected:
      void initStyleOption(QStyleOptionViewItem*, const QModelIndex&) const override;
   public:
      CompletionDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
      };

//---------------------------------------------------------
//   ScriptEdit
//---------------------------------------------------------

class ScriptEdit : public QPlainTextEdit {
      Q_OBJECT

      ScriptEngine* _engine;
      bool _singleLine;
      ScriptHighlighter* _highlighter;
      QCompleter* _completer;
      QStandardItemModel* _model;
      ScriptCompletion _completion;
      int _errorLine { 0 };
      QString _errorText;

      void showCompletion(bool insertSingle);
      void updateErrorMark();

   private slots:
      void insertCompletion(const QModelIndex&);

   protected:
      void keyPressEvent(QKeyEvent*) override;
      bool event(QEvent*) override;
      void focusOutEvent(QFocusEvent*) override;

   signals:
      void run(const QString& code);            // the line / the selection (Enter in the console, Ctrl+Enter)
      void runAll();                            // Ctrl+Shift+Enter
      void escape();
      void history(int direction);              // the console's Up (-1) / Down (+1)
      void findRequested();

   public:
      ScriptEdit(ScriptEngine*, bool singleLine, QWidget* parent = nullptr);
      void reloadWords();                       // after the score (its sections) changed
      void setError(int line, const QString& message);     // line 0: none
      QString lineUnderCursor() const;
      };

//---------------------------------------------------------
//   ScriptPreviewer
//    preview runs off the GUI thread
//---------------------------------------------------------

class ScriptPreviewWorker : public QObject {
      Q_OBJECT
      ScriptEngine* _engine { nullptr };

   public slots:
      void run(int serial, const Ms::ScriptSnapshot& snapshot, const QString& code);

   signals:
      void done(int serial, const Ms::ScriptResult& result);

   public:
      ~ScriptPreviewWorker();
      ScriptEngine* engine() const { return _engine; }
      void makeEngine();
      };

class ScriptPreviewer : public QObject {
      Q_OBJECT
      QThread _thread;
      ScriptPreviewWorker* _worker;

   signals:
      void start(int serial, const Ms::ScriptSnapshot& snapshot, const QString& code);
      void done(int serial, const Ms::ScriptResult& result);

   public:
      static const int BUDGET = 3000000;        // instructions of one preview run
      ScriptPreviewer(QObject* parent = nullptr);
      ~ScriptPreviewer();
      void request(int serial, const ScriptSnapshot& snapshot, const QString& code);
      void cancel();
      };

//---------------------------------------------------------
//   ScriptConsole
//---------------------------------------------------------

class ScriptConsole : public QDockWidget {
      Q_OBJECT

      ScriptEngine* _engine;
      ScriptPreviewer* _previewer;
      QTabWidget* _tabs;
      // Console
      QPlainTextEdit* _output;
      ScriptEdit* _input;
      QLabel* _consoleStatus;
      QVector<QPair<QString, QString>> _log;    // the output as printed: text, kind (re-coloured when the theme changes)
      bool _logDark { false };
      bool _consoleError { false };
      bool _scriptError { false };
      bool _scriptHold { false };               // the Scripts status line shows a run's result
      QString _scriptHoldText;                  // what ran
      QString _scriptHoldFull;                  // the editor's text then
      QStringList _history;
      int _historyPos { -1 };
      QString _historyDraft;
      // Scripts
      QListWidget* _list;
      ScriptEdit* _editor;
      QLabel* _scriptStatus;
      QWidget* _findBar;
      QLineEdit* _find;
      QString _currentScript;                   // its name in the list; empty: not saved yet
      bool _loading { false };

      QTimer _previewTimer;
      int _serial { 0 };
      ScriptSnapshot _previewSnapshot;
      QString _wordsSignature;

      Score* score() const;
      ScriptEdit* activeEdit() const;
      QLabel* activeStatus() const;
      void print(const QString& text, const QString& kind);
      void appendOutput(const QString& text, const QString& kind);
      void recolour();
      void setStatus(QLabel*, const QString& text, bool error);
      static QColor errorColor(const QPalette&);
      bool execute(const QString& code, bool console);       // false: nothing ran (no score)
      void showResult(const ScriptResult&, ScriptEdit*, QLabel*, bool preview);
      void markNotes(const QVector<Note*>&);
      void refreshList(const QString& select = QString());
      bool saveCurrent();
      void loadScript(const QString& name);
      void refreshWords(Score*);

   private slots:
      void runConsoleLine(const QString&);
      void runScriptPart(const QString&);
      void runScript();
      void historyStep(int);
      void schedulePreview();
      void preview();
      void previewDone(int serial, const Ms::ScriptResult&);
      void toScore();
      void newScript();
      void deleteScript();
      void scriptSelected(QListWidgetItem* current, QListWidgetItem* previous);
      void showFind();
      void findNext();

   protected:
      void closeEvent(QCloseEvent*) override;
      void changeEvent(QEvent*) override;
      void showEvent(QShowEvent*) override;
      void hideEvent(QHideEvent*) override;

   signals:
      void closed(bool);

   public:
      ScriptConsole(QWidget* parent = nullptr);
      ~ScriptConsole();
      void scoreChanged();                      // another score is current, or the current one was edited
      void focusInput();
      void writeSettings();
      static QString scriptsFolder();           // the Scripts folder of Preferences › General (Documents/<app>/Scripts)
      static QString legacyScriptsFolder();     // <data folder>/scripts of the first build: read as a fallback, never moved
      static QString scriptFile(const QString& name);     // an existing script's file (new folder first, then the old), else where it is saved
      };

} // namespace Ms

Q_DECLARE_METATYPE(Ms::ScriptSnapshot)
Q_DECLARE_METATYPE(Ms::ScriptResult)

#endif
