#include "ImageViewerAppActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool endsWithCI(const char* str, const char* suffix) {
  size_t strLen = strlen(str);
  size_t sufLen = strlen(suffix);
  if (sufLen > strLen) return false;
  const char* end = str + strLen - sufLen;
  for (size_t i = 0; i < sufLen; i++) {
    if (tolower(static_cast<unsigned char>(end[i])) != tolower(static_cast<unsigned char>(suffix[i]))) return false;
  }
  return true;
}

bool hasImageExtension(const char* filename) {
  return endsWithCI(filename, ".png") || endsWithCI(filename, ".jpg") || endsWithCI(filename, ".jpeg") ||
         endsWithCI(filename, ".heic");
}
}  // namespace

std::string ImageViewerAppActivity::getTempBmpPath() const { return manifest.path + "/.tmp_view.bmp"; }

void ImageViewerAppActivity::scanForImages() {
  imageFiles.clear();
  constexpr size_t MAX_IMAGES = 600;
  std::vector<std::string> dirsToScan = {"/"};

  while (!dirsToScan.empty() && imageFiles.size() < MAX_IMAGES) {
    const std::string currentDir = dirsToScan.back();
    dirsToScan.pop_back();

    auto dir = Storage.open(currentDir.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    dir.rewindDirectory();

    char name[256];
    for (auto entry = dir.openNextFile(); entry && imageFiles.size() < MAX_IMAGES; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));

      // Skip hidden/system entries
      if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
        entry.close();
        continue;
      }

      std::string fullPath = currentDir;
      if (fullPath.empty() || fullPath.back() != '/') {
        fullPath += "/";
      }
      fullPath += name;

      if (entry.isDirectory()) {
        dirsToScan.push_back(fullPath);
        entry.close();
        continue;
      }

      if (hasImageExtension(name)) {
        ImageFile img;
        img.path = fullPath;
        img.name = (fullPath.size() > 1) ? fullPath.substr(1) : fullPath;
        imageFiles.push_back(std::move(img));
      }

      entry.close();
    }

    dir.close();
    yield();
  }

  // Sort alphabetically by name
  std::sort(imageFiles.begin(), imageFiles.end(),
            [](const ImageFile& a, const ImageFile& b) { return a.name < b.name; });

  LOG_DBG("IMGV", "Found %d image(s) across SD card", static_cast<int>(imageFiles.size()));
  if (imageFiles.size() >= MAX_IMAGES) {
    LOG_INF("IMGV", "Image scan stopped at %d entries (cap)", static_cast<int>(MAX_IMAGES));
  }
}

void ImageViewerAppActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  viewingImage = false;
  scanForImages();
  requestUpdate();
}

void ImageViewerAppActivity::onExit() {
  // Clean up temp file if it exists
  std::string tempPath = getTempBmpPath();
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }
  Activity::onExit();
}

void ImageViewerAppActivity::loop() {
  if (viewingImage) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      viewingImage = false;
      requestUpdate();
      return;
    }

    // Navigate to next image
    buttonNavigator.onNext([this] {
      if (imageFiles.size() > 1) {
        currentImageIndex = (currentImageIndex + 1) % static_cast<int>(imageFiles.size());
        requestUpdate();
      }
    });

    // Navigate to previous image
    buttonNavigator.onPrevious([this] {
      if (imageFiles.size() > 1) {
        int count = static_cast<int>(imageFiles.size());
        currentImageIndex = (currentImageIndex - 1 + count) % count;
        requestUpdate();
      }
    });

    return;
  }

  // List mode
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int imageCount = static_cast<int>(imageFiles.size());

  buttonNavigator.onNext([this, imageCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, imageCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, imageCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, imageCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < imageCount) {
      currentImageIndex = selectorIndex;
      viewingImage = true;
      requestUpdate();
    }
  }
}

void ImageViewerAppActivity::render(RenderLock&&) {
  if (viewingImage) {
    renderImage();
  } else {
    renderList();
  }
}

void ImageViewerAppActivity::renderList() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, manifest.name.c_str());

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (imageFiles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No images found on SD card");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentY, pageWidth, contentHeight}, static_cast<int>(imageFiles.size()), selectorIndex,
        [this](int index) -> std::string { return imageFiles[index].name; }, nullptr,
        [](int) { return UIIcon::Image; }, nullptr);
  }

  const auto labels = mappedInput.mapLabels("« Back", "View", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ImageViewerAppActivity::renderImage() {
  if (currentImageIndex < 0 || currentImageIndex >= static_cast<int>(imageFiles.size())) {
    return;
  }

  const auto& image = imageFiles[currentImageIndex];

  // Show loading popup
  renderer.clearScreen();
  Rect popupRect = GUI.drawPopup(renderer, "Loading...");
  GUI.fillPopupProgress(renderer, popupRect, 20);
  renderer.displayBuffer();

  if (!convertAndDisplayImage(image.path)) {
    // Show error
    renderer.clearScreen();
    const auto pageHeight = renderer.getScreenHeight();

    if (endsWithCI(image.name.c_str(), ".heic")) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "HEIC format is not");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, "supported on this device");
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Could not display image");
    }

    std::string pageStr = std::to_string(currentImageIndex + 1) + "/" + std::to_string(imageFiles.size());
    const auto labels = mappedInput.mapLabels("« Back", pageStr.c_str(), "Prev", "Next");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

bool ImageViewerAppActivity::convertAndDisplayImage(const std::string& imagePath) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // HEIC is not supported on the device
  if (endsWithCI(imagePath.c_str(), ".heic")) {
    LOG_ERR("IMGV", "HEIC format not supported");
    return false;
  }

  // Determine image type
  bool isPng = endsWithCI(imagePath.c_str(), ".png");
  bool isJpeg = endsWithCI(imagePath.c_str(), ".jpg") || endsWithCI(imagePath.c_str(), ".jpeg");

  if (!isPng && !isJpeg) {
    LOG_ERR("IMGV", "Unsupported image format: %s", imagePath.c_str());
    return false;
  }

  // Open source image file
  FsFile sourceFile;
  if (!Storage.openFileForRead("IMGV", imagePath, sourceFile)) {
    LOG_ERR("IMGV", "Failed to open image: %s", imagePath.c_str());
    return false;
  }

  // Open temp BMP file for writing
  std::string tempPath = getTempBmpPath();
  FsFile tempBmp;
  if (!Storage.openFileForWrite("IMGV", tempPath, tempBmp)) {
    LOG_ERR("IMGV", "Failed to create temp BMP: %s", tempPath.c_str());
    sourceFile.close();
    return false;
  }

  // Convert source image to BMP (fit to screen without cropping)
  bool conversionOk = false;
  if (isPng) {
    conversionOk = PngToBmpConverter::pngFileToBmpStream(sourceFile, tempBmp, false);
  } else {
    conversionOk = JpegToBmpConverter::jpegFileToBmpStream(sourceFile, tempBmp, false);
  }

  sourceFile.close();
  tempBmp.close();

  if (!conversionOk) {
    LOG_ERR("IMGV", "Image conversion failed for: %s", imagePath.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }

  // Open the converted BMP for display
  FsFile bmpFile;
  if (!Storage.openFileForRead("IMGV", tempPath, bmpFile)) {
    LOG_ERR("IMGV", "Failed to open converted BMP");
    Storage.remove(tempPath.c_str());
    return false;
  }

  Bitmap bitmap(bmpFile, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("IMGV", "Failed to parse converted BMP headers");
    bmpFile.close();
    Storage.remove(tempPath.c_str());
    return false;
  }

  // Calculate position to center the image on screen
  int x, y;
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    // Center small images
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  // Render the image
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

  // Draw navigation hints
  std::string pageStr = std::to_string(currentImageIndex + 1) + "/" + std::to_string(imageFiles.size());
  const auto labels = mappedInput.mapLabels("\xC2\xAB Back", pageStr.c_str(), "Prev", "Next");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  bmpFile.close();
  Storage.remove(tempPath.c_str());

  return true;
}
