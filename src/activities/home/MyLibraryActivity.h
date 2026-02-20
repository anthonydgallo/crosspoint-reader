#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class MyLibraryActivity final : public ActivityWithSubactivity {
 private:
  enum class State { BROWSING, FILE_ACTIONS, DELETE_CONFIRM, MOVE_BROWSING };

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  State state = State::BROWSING;
  std::string deleteError;
  bool skipNextConfirmRelease = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;

  // Move state
  std::string moveSourcePath;
  std::string moveSourceName;
  bool moveSourceIsDir = false;
  std::string moveBrowsePath;
  std::vector<std::string> moveDirs;
  size_t moveSelectorIndex = 0;
  std::string moveError;

  // Callbacks
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoHome;

  // Data loading
  void loadFiles();
  void loadMoveDirs();
  size_t findEntry(const std::string& name) const;

  // Delete
  void deleteSelectedItem();

  // Rename
  void startRename();

  // Move
  void startMove();
  void executeMoveHere();

 public:
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onGoHome,
                             const std::function<void(const std::string& path)>& onSelectBook,
                             std::string initialPath = "/")
      : ActivityWithSubactivity("MyLibrary", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)),
        onSelectBook(onSelectBook),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
