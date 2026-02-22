#pragma once

#include <functional>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Art gallery activity displaying procedurally-generated artwork
 * optimized for the Xteink X4's 480x800 e-ink display.
 *
 * Uses high-contrast patterns, dithered grays, and geometric designs
 * that render cleanly on the 4.3" grayscale e-paper panel.
 */
class ArtGalleryActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int currentArt = 0;
  bool showingArt = false;

  const std::function<void()> onGoHome;

  static constexpr int ART_COUNT = 8;

  void renderMenu();
  void renderArtPiece();

  // Individual art renderers
  void drawMountainLandscape();
  void drawGeometricMandala();
  void drawTreeOfLife();
  void drawGreatWave();
  void drawZenEnso();
  void drawCitySkyline();
  void drawConcentricCircles();
  void drawLabyrinth();

  // Helper: draw a filled circle using pixels
  void fillCircle(int cx, int cy, int radius, bool state = true);
  // Helper: draw a circle outline
  void drawCircle(int cx, int cy, int radius, int lineWidth = 1, bool state = true);
  // Helper: draw a dithered filled circle
  void fillCircleDither(int cx, int cy, int radius, Color color);

  const char* getArtTitle(int index) const;

 public:
  explicit ArtGalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onGoHome)
      : Activity("ArtGallery", renderer, mappedInput), onGoHome(onGoHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
