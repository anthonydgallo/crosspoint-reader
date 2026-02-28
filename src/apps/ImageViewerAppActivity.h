#pragma once

#include <string>
#include <vector>

#include "AppManifest.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Image viewer app activity for "imageviewer" type apps.
// Scans the SD card for .png, .jpg, .jpeg, and .heic files
// and displays them full-screen on the e-ink display.
class ImageViewerAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool viewingImage = false;
  int currentImageIndex = 0;

  const AppManifest manifest;

  struct ImageFile {
    std::string name;  // Display name (filename)
    std::string path;  // Full path to the image file
  };
  std::vector<ImageFile> imageFiles;

  void scanForImages();
  void renderList();
  void renderImage();
  bool convertAndDisplayImage(const std::string& imagePath);
  std::string getTempBmpPath() const;

 public:
  explicit ImageViewerAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const AppManifest& manifest)
      : Activity("ImageViewer", renderer, mappedInput), manifest(manifest) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
