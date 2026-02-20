#pragma once

#include <string>
#include <vector>

// Represents a single app discovered on the SD card under /apps/.
// Each app folder must contain an app.json manifest file.
//
// Supported app types:
//   "rosary"     - Built-in rosary prayer activity (firmware provides the UI)
//   "textviewer" - Generic text viewer that displays a list of text files
//
// Example app.json for a textviewer app:
// {
//   "name": "My Prayer Book",
//   "type": "textviewer",
//   "version": "1.0",
//   "entries": [
//     {"title": "Morning Prayer", "file": "morning.txt"},
//     {"title": "Evening Prayer", "file": "evening.txt"}
//   ]
// }

struct AppManifest {
  std::string name;     // Display name shown in the menu
  std::string type;     // App type identifier (e.g., "rosary", "textviewer")
  std::string path;     // Absolute path to app folder on SD card (e.g., "/apps/rosary")
  std::string version;  // Version string from manifest

  // For "textviewer" type apps: ordered list of text entries
  struct Entry {
    std::string title;  // Display title for this entry
    std::string file;   // Filename relative to app folder (e.g., "morning.txt")
  };
  std::vector<Entry> entries;
};
