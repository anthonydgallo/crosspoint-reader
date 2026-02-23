#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/ActivityWithSubactivity.h"
#include "util/ButtonNavigator.h"

// Simple text editor app for creating and editing .txt files on the SD card.
// Supports typing via on-screen keyboard, backspace, save, undo, redo,
// and quit without saving.
//
// App type: "texteditor"
// The app.json needs only name, type, and version (no entries required).
// The editor opens a file browser rooted at the app's folder on the SD card.
class TextEditorAppActivity final : public ActivityWithSubactivity {
  // Editor states
  enum class State {
    FILE_BROWSER,  // Browsing files in the app folder
    EDITING,       // Editing text content
    CONFIRM_QUIT,  // Confirm quit without saving
  };

  // Undo/redo history entry
  struct Snapshot {
    std::string text;
    int cursorPos;
  };

  static constexpr int MAX_UNDO_HISTORY = 20;
  static constexpr int MAX_FILE_SIZE = 8192;  // 8KB max file size
  static constexpr const char* TEXT_DIR = "/texts";

  ButtonNavigator buttonNavigator;

  // File browser state
  std::vector<std::string> files;
  int selectorIndex = 0;
  std::string browsePath;

  // Editor state
  State state = State::FILE_BROWSER;
  std::string currentFilePath;
  std::string currentFileName;
  std::string text;
  std::string savedText;  // Text as last saved (to detect changes)
  int cursorPos = 0;
  int scrollLine = 0;
  int linesPerPage = 0;

  // Undo/redo
  std::vector<Snapshot> undoStack;
  std::vector<Snapshot> redoStack;

  // Wrapped lines for display
  std::vector<std::string> wrappedLines;
  // Map from wrapped line index to character offset in text
  std::vector<int> lineStartOffsets;

  const AppManifest manifest;
  const std::function<void()> onGoHome;

  // File browser methods
  void loadFiles();
  void openFile(const std::string& filename);
  void createNewFile();

  // Editor methods
  void enterEditor(const std::string& filePath, const std::string& content);
  void saveFile();
  void insertChar(char c);
  void deleteChar();  // backspace
  void pushUndo();
  void undo();
  void redo();
  bool hasUnsavedChanges() const;

  // Text wrapping
  void rewrapText();
  int getCursorLine() const;
  void ensureCursorVisible();

  // Rendering
  void renderFileBrowser();
  void renderEditor();
  void renderConfirmQuit();

 public:
  explicit TextEditorAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest,
                                 const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("TextEditor", renderer, mappedInput), manifest(manifest), onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
