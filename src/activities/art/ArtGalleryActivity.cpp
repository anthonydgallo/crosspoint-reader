#include "ArtGalleryActivity.h"

#include <GfxRenderer.h>

#include <cmath>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ─── Lifecycle ───────────────────────────────────────────────────

void ArtGalleryActivity::onEnter() {
  Activity::onEnter();
  currentArt = 0;
  showingArt = false;
  requestUpdate();
}

void ArtGalleryActivity::onExit() { Activity::onExit(); }

void ArtGalleryActivity::loop() {
  if (showingArt) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showingArt = false;
      requestUpdate();
      return;
    }

    buttonNavigator.onNextRelease([this] {
      currentArt = ButtonNavigator::nextIndex(currentArt, ART_COUNT);
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this] {
      currentArt = ButtonNavigator::previousIndex(currentArt, ART_COUNT);
      requestUpdate();
    });

    return;
  }

  // Menu mode
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  buttonNavigator.onNext([this] {
    currentArt = ButtonNavigator::nextIndex(currentArt, ART_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    currentArt = ButtonNavigator::previousIndex(currentArt, ART_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showingArt = true;
    requestUpdate();
  }
}

void ArtGalleryActivity::render(Activity::RenderLock&&) {
  if (showingArt) {
    renderArtPiece();
  } else {
    renderMenu();
  }
}

const char* ArtGalleryActivity::getArtTitle(int index) const {
  switch (index) {
    case 0:
      return "Mountain Landscape";
    case 1:
      return "Geometric Mandala";
    case 2:
      return "Tree of Life";
    case 3:
      return "The Great Wave";
    case 4:
      return "Zen Enso";
    case 5:
      return "City Skyline";
    case 6:
      return "Concentric Circles";
    case 7:
      return "Labyrinth";
    default:
      return "Unknown";
  }
}

// ─── Menu ────────────────────────────────────────────────────────

void ArtGalleryActivity::renderMenu() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Art Gallery");

  int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentY, pageWidth, contentHeight}, ART_COUNT, currentArt,
      [this](int index) -> std::string { return getArtTitle(index); }, nullptr, nullptr, nullptr);

  const auto labels = mappedInput.mapLabels("\x11 Back", "View", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ─── Art rendering ───────────────────────────────────────────────

void ArtGalleryActivity::renderArtPiece() {
  renderer.clearScreen();

  switch (currentArt) {
    case 0:
      drawMountainLandscape();
      break;
    case 1:
      drawGeometricMandala();
      break;
    case 2:
      drawTreeOfLife();
      break;
    case 3:
      drawGreatWave();
      break;
    case 4:
      drawZenEnso();
      break;
    case 5:
      drawCitySkyline();
      break;
    case 6:
      drawConcentricCircles();
      break;
    case 7:
      drawLabyrinth();
      break;
  }

  // Draw title bar at bottom
  renderer.fillRect(0, renderer.getScreenHeight() - 34, renderer.getScreenWidth(), 34, false);
  renderer.fillRect(0, renderer.getScreenHeight() - 35, renderer.getScreenWidth(), 1, true);
  renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 28, getArtTitle(currentArt), true);

  // Page indicator
  std::string pageStr =
      std::to_string(currentArt + 1) + "/" + std::to_string(ART_COUNT);
  int pageWidth = renderer.getTextWidth(SMALL_FONT_ID, pageStr.c_str());
  renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - pageWidth - 10,
                    renderer.getScreenHeight() - 28, pageStr.c_str(), true);

  renderer.displayBuffer();
}

// ─── Helper methods ──────────────────────────────────────────────

void ArtGalleryActivity::fillCircle(int cx, int cy, int radius, bool state) {
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        renderer.drawPixel(cx + x, cy + y, state);
      }
    }
  }
}

void ArtGalleryActivity::drawCircle(int cx, int cy, int radius, int lineWidth, bool state) {
  int outerR2 = radius * radius;
  int innerR = radius - lineWidth;
  int innerR2 = innerR * innerR;
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      int d = x * x + y * y;
      if (d <= outerR2 && d >= innerR2) {
        renderer.drawPixel(cx + x, cy + y, state);
      }
    }
  }
}

void ArtGalleryActivity::fillCircleDither(int cx, int cy, int radius, Color color) {
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        int px = cx + x;
        int py = cy + y;
        if (px >= 0 && px < renderer.getScreenWidth() && py >= 0 && py < renderer.getScreenHeight()) {
          // Approximate dithering with pixel state based on position
          bool state;
          switch (color) {
            case Color::Black:
              state = true;
              break;
            case Color::DarkGray:
              state = ((px + py) % 2 == 0) || ((px % 3 == 0) && (py % 3 == 0));
              break;
            case Color::LightGray:
              state = ((px + py) % 3 == 0);
              break;
            case Color::White:
            default:
              state = false;
              break;
          }
          renderer.drawPixel(px, py, state);
        }
      }
    }
  }
}

// ─── Art Piece 1: Mountain Landscape ─────────────────────────────

void ArtGalleryActivity::drawMountainLandscape() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;  // Leave room for title

  // Sky gradient using dithered rectangles (top portion)
  int skyHeight = H * 2 / 5;
  // Light sky at top
  renderer.fillRectDither(0, 0, W, skyHeight / 3, Color::LightGray);

  // Sun disc
  int sunX = W * 3 / 4;
  int sunY = skyHeight / 3;
  fillCircle(sunX, sunY, 35, false);  // White sun
  drawCircle(sunX, sunY, 36, 2, true);  // Outline

  // Far mountains (lighter, smaller) - Layer 1
  for (int x = 0; x < W; x++) {
    // Generate mountain profile using simple sine-like shapes
    float t = static_cast<float>(x) / W;
    int peakH = static_cast<int>(
        skyHeight + 60 * sinf(t * 3.14159f * 2.5f) - 40 * sinf(t * 3.14159f * 5.0f + 1.0f) +
        20 * sinf(t * 3.14159f * 8.0f + 2.0f));
    peakH = (peakH < skyHeight - 80) ? skyHeight - 80 : peakH;

    // Draw dithered column for far mountains
    for (int y = peakH; y < skyHeight + 80; y++) {
      if ((x + y) % 3 == 0) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  // Mid mountains - Layer 2 (darker)
  int midBase = skyHeight + 60;
  for (int x = 0; x < W; x++) {
    float t = static_cast<float>(x) / W;
    int peakH = static_cast<int>(
        midBase - 120 * sinf(t * 3.14159f * 1.8f + 0.5f) - 50 * sinf(t * 3.14159f * 4.0f + 1.5f));
    peakH = (peakH < skyHeight - 20) ? skyHeight - 20 : peakH;

    for (int y = peakH; y < midBase + 30; y++) {
      if ((x + y) % 2 == 0) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  // Close mountains - Layer 3 (solid black)
  int closeBase = H * 3 / 5;
  for (int x = 0; x < W; x++) {
    float t = static_cast<float>(x) / W;
    int peakH = static_cast<int>(closeBase - 160 * sinf(t * 3.14159f * 1.2f + 2.0f) -
                                 70 * sinf(t * 3.14159f * 3.0f + 0.8f));
    peakH = (peakH < midBase - 60) ? midBase - 60 : peakH;

    for (int y = peakH; y < closeBase + 20; y++) {
      renderer.drawPixel(x, y, true);
    }
  }

  // Foreground valley (solid black)
  renderer.fillRect(0, closeBase + 10, W, H - closeBase - 10, true);

  // Pine tree silhouettes in foreground
  for (int i = 0; i < 12; i++) {
    int tx = 20 + i * (W - 40) / 11 + (i % 3) * 8 - 12;
    int treeH = 60 + (i % 4) * 25;
    int treeBase = closeBase + 10;

    // Tree trunk
    renderer.fillRect(tx - 2, treeBase - treeH, 4, treeH, false);

    // Tree triangles (white silhouette against black)
    for (int layer = 0; layer < 3; layer++) {
      int layerY = treeBase - treeH + layer * (treeH / 4);
      int layerW = 6 + layer * 8;
      for (int row = 0; row < treeH / 4; row++) {
        int w = layerW * (treeH / 4 - row) / (treeH / 4);
        if (w > 0) {
          renderer.drawLine(tx - w, layerY + row, tx + w, layerY + row, false);
        }
      }
    }
  }
}

// ─── Art Piece 2: Geometric Mandala ──────────────────────────────

void ArtGalleryActivity::drawGeometricMandala() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;
  const int cx = W / 2;
  const int cy = H / 2;

  // Outer decorative border
  renderer.drawRect(8, 8, W - 16, H - 16, 2, true);
  renderer.drawRect(14, 14, W - 28, H - 28, 1, true);

  // Outermost ring with dithered fill
  fillCircleDither(cx, cy, 200, Color::LightGray);
  fillCircle(cx, cy, 190, false);

  // Radiating lines from center (like sun rays)
  for (int angle = 0; angle < 360; angle += 10) {
    float rad = angle * 3.14159f / 180.0f;
    int x1 = cx + static_cast<int>(50 * cosf(rad));
    int y1 = cy + static_cast<int>(50 * sinf(rad));
    int x2 = cx + static_cast<int>(190 * cosf(rad));
    int y2 = cy + static_cast<int>(190 * sinf(rad));
    renderer.drawLine(x1, y1, x2, y2, true);
  }

  // Concentric circles
  drawCircle(cx, cy, 190, 3, true);
  drawCircle(cx, cy, 160, 2, true);
  drawCircle(cx, cy, 130, 2, true);
  drawCircle(cx, cy, 100, 2, true);
  drawCircle(cx, cy, 70, 2, true);

  // Petal pattern at radius 130
  for (int angle = 0; angle < 360; angle += 30) {
    float rad = angle * 3.14159f / 180.0f;
    int px = cx + static_cast<int>(130 * cosf(rad));
    int py = cy + static_cast<int>(130 * sinf(rad));
    fillCircle(px, py, 18, true);
    fillCircle(px, py, 14, false);
  }

  // Diamond pattern at radius 100
  for (int angle = 15; angle < 360; angle += 30) {
    float rad = angle * 3.14159f / 180.0f;
    int px = cx + static_cast<int>(100 * cosf(rad));
    int py = cy + static_cast<int>(100 * sinf(rad));

    // Draw small diamond
    int dxPoints[] = {px, px + 8, px, px - 8};
    int dyPoints[] = {py - 12, py, py + 12, py};
    renderer.fillPolygon(dxPoints, dyPoints, 4, true);
  }

  // Inner flower of life pattern
  fillCircle(cx, cy, 50, true);
  fillCircle(cx, cy, 45, false);

  for (int angle = 0; angle < 360; angle += 60) {
    float rad = angle * 3.14159f / 180.0f;
    int px = cx + static_cast<int>(25 * cosf(rad));
    int py = cy + static_cast<int>(25 * sinf(rad));
    drawCircle(px, py, 25, 2, true);
  }

  // Central dot
  fillCircle(cx, cy, 8, true);
}

// ─── Art Piece 3: Tree of Life ───────────────────────────────────

void ArtGalleryActivity::drawTreeOfLife() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;
  const int cx = W / 2;

  // Ground line
  int groundY = H * 3 / 4;

  // Ground texture - dithered
  for (int y = groundY; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if ((x + y * 3) % 5 == 0 || (x * 2 + y) % 7 == 0) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  // Trunk
  int trunkW = 24;
  int trunkTop = groundY - 180;
  renderer.fillRect(cx - trunkW / 2, trunkTop, trunkW, groundY - trunkTop, true);

  // Bark texture on trunk
  for (int y = trunkTop; y < groundY; y += 6) {
    renderer.drawLine(cx - trunkW / 2 + 3, y, cx - trunkW / 2 + 8, y + 4, false);
    renderer.drawLine(cx + trunkW / 2 - 8, y + 3, cx + trunkW / 2 - 3, y + 7, false);
  }

  // Roots
  for (int i = -3; i <= 3; i++) {
    int rootStartX = cx + i * (trunkW / 6);
    int rootEndX = cx + i * 45;
    int rootEndY = groundY + 40 + abs(i) * 12;

    // Draw root as thick line
    renderer.drawLine(rootStartX, groundY, rootEndX, rootEndY, 3, true);

    // Small root branches
    if (abs(i) > 1) {
      renderer.drawLine(rootEndX, rootEndY, rootEndX + i * 15, rootEndY + 15, 2, true);
    }
  }

  // Main branches - recursive-like structure done iteratively
  struct Branch {
    int x1, y1, x2, y2;
    int width;
  };

  Branch branches[80];
  int branchCount = 0;

  // Level 1 branches
  float baseAngles[] = {-1.2f, -0.8f, -0.4f, 0.0f, 0.4f, 0.8f, 1.2f};
  for (int i = 0; i < 7 && branchCount < 80; i++) {
    float angle = baseAngles[i] - 1.5708f;  // offset from vertical
    int len = 80 + (i % 3) * 20;
    int bx2 = cx + static_cast<int>(len * cosf(angle));
    int by2 = trunkTop + static_cast<int>(len * sinf(angle));
    branches[branchCount++] = {cx, trunkTop, bx2, by2, 6};
  }

  // Level 2 branches
  int level1End = branchCount;
  for (int i = 0; i < level1End && branchCount < 60; i++) {
    const Branch& parent = branches[i];
    float dx = static_cast<float>(parent.x2 - parent.x1);
    float dy = static_cast<float>(parent.y2 - parent.y1);
    float angle = atan2f(dy, dx);

    for (int j = -1; j <= 1; j += 2) {
      float childAngle = angle + j * 0.5f;
      int len = 50 + (i % 2) * 15;
      int bx2 = parent.x2 + static_cast<int>(len * cosf(childAngle));
      int by2 = parent.y2 + static_cast<int>(len * sinf(childAngle));
      if (branchCount < 80) {
        branches[branchCount++] = {parent.x2, parent.y2, bx2, by2, 3};
      }
    }
  }

  // Level 3 branches (thinnest)
  int level2End = branchCount;
  for (int i = level1End; i < level2End && branchCount < 78; i++) {
    const Branch& parent = branches[i];
    float dx = static_cast<float>(parent.x2 - parent.x1);
    float dy = static_cast<float>(parent.y2 - parent.y1);
    float angle = atan2f(dy, dx);

    for (int j = -1; j <= 1; j += 2) {
      float childAngle = angle + j * 0.4f;
      int len = 30 + (i % 3) * 8;
      int bx2 = parent.x2 + static_cast<int>(len * cosf(childAngle));
      int by2 = parent.y2 + static_cast<int>(len * sinf(childAngle));
      if (branchCount < 80) {
        branches[branchCount++] = {parent.x2, parent.y2, bx2, by2, 1};
      }
    }
  }

  // Draw all branches
  for (int i = 0; i < branchCount; i++) {
    renderer.drawLine(branches[i].x1, branches[i].y1, branches[i].x2, branches[i].y2, branches[i].width, true);
  }

  // Leaves as small filled circles at branch endpoints (level 2 and 3)
  for (int i = level1End; i < branchCount; i++) {
    fillCircle(branches[i].x2, branches[i].y2, 10 - branches[i].width, true);
    fillCircle(branches[i].x2, branches[i].y2, 7 - branches[i].width, false);
  }
}

// ─── Art Piece 4: The Great Wave ─────────────────────────────────

void ArtGalleryActivity::drawGreatWave() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;

  // Sky - white background already from clearScreen

  // Draw multiple wave layers from back to front

  // Background wave (gentle, far away)
  for (int x = 0; x < W; x++) {
    float t = static_cast<float>(x) / W;
    int waveY = H / 3 + static_cast<int>(20 * sinf(t * 3.14159f * 3.0f + 1.0f));
    // Dithered water below wave
    for (int y = waveY; y < H / 3 + 60; y++) {
      if ((x + y) % 4 == 0) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  // Main large wave - the signature curl
  int waveBaseY = H / 2 + 40;
  for (int x = 0; x < W; x++) {
    float t = static_cast<float>(x) / W;

    // Main wave crest with dramatic curl
    float waveShape = 0;

    // Primary wave form
    waveShape += 120 * sinf(t * 3.14159f * 1.0f + 0.3f);

    // Add the curl at the crest
    if (t > 0.2f && t < 0.7f) {
      float curlT = (t - 0.2f) / 0.5f;
      waveShape += 80 * sinf(curlT * 3.14159f) * sinf(curlT * 3.14159f);
    }

    int waveTopY = waveBaseY - static_cast<int>(waveShape);

    // Wave face - dark fill
    for (int y = waveTopY; y < waveBaseY + 30; y++) {
      if (y >= 0 && y < H) {
        renderer.drawPixel(x, y, true);
      }
    }

    // Foam/spray at the crest - white dots above wave
    if (t > 0.25f && t < 0.65f) {
      float curlT = (t - 0.25f) / 0.4f;
      int sprayH = static_cast<int>(30 * sinf(curlT * 3.14159f));
      for (int y = waveTopY - sprayH; y < waveTopY; y++) {
        if (y >= 0 && y < H) {
          // Spray pattern: scattered white dots on black
          if ((x * 7 + y * 13) % 5 < 2) {
            renderer.drawPixel(x, y, true);
          }
        }
      }
    }

    // White foam lines on wave face
    for (int line = 0; line < 5; line++) {
      int foamY = waveTopY + 8 + line * 12 + static_cast<int>(4 * sinf(x * 0.15f + line * 1.3f));
      if (foamY >= 0 && foamY < H && foamY > waveTopY && foamY < waveBaseY + 20) {
        renderer.drawPixel(x, foamY, false);
        if (x % 2 == 0 && foamY + 1 < H) {
          renderer.drawPixel(x, foamY + 1, false);
        }
      }
    }
  }

  // Curl fingers (white hooks at wave tip)
  for (int finger = 0; finger < 8; finger++) {
    int fx = W / 3 + finger * 18;
    float ft = static_cast<float>(fx) / W;
    float waveShape = 120 * sinf(ft * 3.14159f * 1.0f + 0.3f);
    if (ft > 0.2f && ft < 0.7f) {
      float curlT = (ft - 0.2f) / 0.5f;
      waveShape += 80 * sinf(curlT * 3.14159f) * sinf(curlT * 3.14159f);
    }
    int waveTopY = (H / 2 + 40) - static_cast<int>(waveShape);

    // Curling finger
    for (int i = 0; i < 15; i++) {
      float angle = (finger * 0.3f) + i * 0.15f;
      int px = fx + static_cast<int>(i * 1.5f);
      int py = waveTopY - 5 + static_cast<int>(i * sinf(angle) * 0.8f);
      if (px >= 0 && px < W && py >= 0 && py < H) {
        renderer.drawPixel(px, py, false);
        if (py + 1 < H) renderer.drawPixel(px, py + 1, false);
      }
    }
  }

  // Water below waves - ocean body with horizontal line texture
  for (int y = waveBaseY + 30; y < H; y++) {
    for (int x = 0; x < W; x++) {
      // Horizontal wave lines pattern
      int waveOffset = static_cast<int>(8 * sinf(x * 0.03f + y * 0.1f));
      if ((y + waveOffset) % 8 < 2) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  // Small boat silhouette in the trough
  int boatX = W * 3 / 4;
  int boatY = waveBaseY + 15;
  // Hull
  int hullXpts[] = {boatX - 20, boatX + 20, boatX + 15, boatX - 15};
  int hullYpts[] = {boatY, boatY, boatY + 10, boatY + 10};
  renderer.fillPolygon(hullXpts, hullYpts, 4, true);
  // Mast
  renderer.drawLine(boatX, boatY, boatX, boatY - 30, 2, true);
  // Sail
  int sailXpts[] = {boatX + 2, boatX + 18, boatX + 2};
  int sailYpts[] = {boatY - 28, boatY - 10, boatY - 5};
  renderer.fillPolygon(sailXpts, sailYpts, 3, true);
}

// ─── Art Piece 5: Zen Enso ──────────────────────────────────────

void ArtGalleryActivity::drawZenEnso() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;
  const int cx = W / 2;
  const int cy = H / 2 - 30;

  // The enso - a thick, brush-stroke-like circle with a gap
  // Varying thickness simulates a brush stroke
  int baseRadius = 160;

  for (int angle = 20; angle < 350; angle++) {
    float rad = angle * 3.14159f / 180.0f;

    // Vary brush thickness along the stroke
    // Thick at start, thinning toward end
    float progress = static_cast<float>(angle - 20) / 330.0f;
    int thickness;
    if (progress < 0.1f) {
      // Taper in at start
      thickness = static_cast<int>(8 + progress * 180);
    } else if (progress > 0.85f) {
      // Taper out at end
      float fadeout = (progress - 0.85f) / 0.15f;
      thickness = static_cast<int>(26 * (1.0f - fadeout * fadeout));
    } else {
      // Full width in middle with slight variation
      thickness = 22 + static_cast<int>(4 * sinf(progress * 6.0f));
    }

    // Draw thick point at this angle
    for (int r = baseRadius - thickness / 2; r <= baseRadius + thickness / 2; r++) {
      int px = cx + static_cast<int>(r * cosf(rad));
      int py = cy + static_cast<int>(r * sinf(rad));
      if (px >= 0 && px < W && py >= 0 && py < H) {
        renderer.drawPixel(px, py, true);
      }
    }

    // Add ink splatter/texture along outer edge
    if (angle % 3 == 0 && thickness > 15) {
      float outerRad = baseRadius + thickness / 2 + 2;
      int px = cx + static_cast<int>(outerRad * cosf(rad));
      int py = cy + static_cast<int>(outerRad * sinf(rad));
      if (px >= 0 && px < W && py >= 0 && py < H) {
        renderer.drawPixel(px, py, true);
        renderer.drawPixel(px + 1, py, true);
      }
    }
  }

  // Signature "chop" (seal) in bottom right - small red seal simulation (black square with character)
  int sealX = W - 80;
  int sealY = H - 100;
  renderer.drawRect(sealX, sealY, 40, 40, 2, true);
  // Simple character inside seal
  renderer.drawLine(sealX + 10, sealY + 8, sealX + 30, sealY + 8, 2, true);
  renderer.drawLine(sealX + 20, sealY + 8, sealX + 20, sealY + 32, 2, true);
  renderer.drawLine(sealX + 10, sealY + 20, sealX + 30, sealY + 20, 2, true);
  renderer.drawLine(sealX + 10, sealY + 32, sealX + 30, sealY + 32, 2, true);
}

// ─── Art Piece 6: City Skyline ───────────────────────────────────

void ArtGalleryActivity::drawCitySkyline() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;

  // Night sky - fill black
  renderer.fillRect(0, 0, W, H, true);

  // Stars
  // Simple pseudo-random stars using deterministic pattern
  for (int i = 0; i < 200; i++) {
    int sx = (i * 197 + 43) % W;
    int sy = (i * 131 + 77) % (H / 2);
    renderer.drawPixel(sx, sy, false);  // White dot on black sky
    // Some stars are bigger
    if (i % 5 == 0) {
      renderer.drawPixel(sx + 1, sy, false);
      renderer.drawPixel(sx, sy + 1, false);
    }
  }

  // Moon
  int moonX = W - 80;
  int moonY = 60;
  fillCircle(moonX, moonY, 30, false);
  // Crescent shadow
  fillCircle(moonX + 12, moonY - 5, 26, true);

  // City skyline - buildings from left to right
  struct Building {
    int x, width, height;
    bool hasAntenna;
    int windowCols;
  };

  Building buildings[] = {
      {10, 35, 200, false, 3},  {40, 50, 320, true, 4},  {85, 30, 180, false, 2},
      {110, 60, 400, true, 5},  {165, 40, 250, false, 3}, {200, 55, 350, true, 4},
      {248, 35, 190, false, 3}, {278, 70, 450, true, 5},  {342, 45, 280, false, 3},
      {382, 55, 370, true, 4},  {430, 40, 220, false, 3},
  };

  int buildingCount = sizeof(buildings) / sizeof(buildings[0]);
  int groundY = H - 40;

  for (int i = 0; i < buildingCount; i++) {
    const Building& b = buildings[i];
    int topY = groundY - b.height;

    // Building body - white rectangle on black
    renderer.fillRect(b.x, topY, b.width, b.height, false);
    // Building outline
    renderer.drawRect(b.x, topY, b.width, b.height, 1, true);

    // Windows - grid of small black rectangles
    int windowW = 5;
    int windowH = 7;
    int windowSpacingX = (b.width - 6) / b.windowCols;
    int windowSpacingY = 18;

    for (int wy = topY + 12; wy < groundY - 15; wy += windowSpacingY) {
      for (int wx = 0; wx < b.windowCols; wx++) {
        int windowX = b.x + 5 + wx * windowSpacingX;

        // Some windows are "lit" (white/clear), some are dark
        bool lit = ((wx + wy / 18 + i) % 3 != 0);
        if (lit) {
          renderer.fillRect(windowX, wy, windowW, windowH, true);
        }
      }
    }

    // Antenna on tall buildings
    if (b.hasAntenna) {
      int antennaX = b.x + b.width / 2;
      renderer.drawLine(antennaX, topY - 25, antennaX, topY, 1, false);
      // Blinking light at top
      fillCircle(antennaX, topY - 25, 2, false);
    }

    // Rooftop details
    if (i % 2 == 0) {
      // Water tank
      renderer.fillRect(b.x + b.width / 2 - 6, topY - 12, 12, 12, false);
      renderer.drawRect(b.x + b.width / 2 - 6, topY - 12, 12, 12, 1, true);
    }
  }

  // Ground/street level
  renderer.fillRect(0, groundY, W, H - groundY, false);
  renderer.drawLine(0, groundY, W, groundY, 1, true);

  // Street details - road markings
  for (int x = 10; x < W; x += 30) {
    renderer.fillRect(x, groundY + 18, 15, 3, true);
  }
}

// ─── Art Piece 7: Concentric Circles ─────────────────────────────

void ArtGalleryActivity::drawConcentricCircles() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;
  const int cx = W / 2;
  const int cy = H / 2;

  // Outer decorative frame
  renderer.drawRect(4, 4, W - 8, H - 8, 2, true);

  // Alternating filled and unfilled circles
  for (int r = 200; r > 0; r -= 12) {
    if ((r / 12) % 2 == 0) {
      // Filled ring
      drawCircle(cx, cy, r, 8, true);
    } else {
      // Thin ring
      drawCircle(cx, cy, r, 2, true);
    }
  }

  // Central dot
  fillCircle(cx, cy, 10, true);

  // Offset circles for Moire-like interference pattern
  int offset = 30;
  for (int r = 200; r > 0; r -= 20) {
    drawCircle(cx + offset, cy - offset, r, 1, true);
  }

  // Another offset group
  for (int r = 180; r > 0; r -= 25) {
    drawCircle(cx - offset, cy + offset, r, 1, true);
  }
}

// ─── Art Piece 8: Labyrinth ─────────────────────────────────────

void ArtGalleryActivity::drawLabyrinth() {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight() - 35;
  const int cx = W / 2;
  const int cy = H / 2;

  // Classical 7-circuit labyrinth (Cretan/Classical style)
  // Drawn as concentric semicircular arcs connected by paths

  int spacing = 18;
  int baseR = 7 * spacing + 10;

  // Outer border circle
  drawCircle(cx, cy, baseR + spacing, 2, true);

  // Draw the 7 circuits as semicircular arcs
  // The classical labyrinth has a specific pattern of connected arcs

  // Top half arcs (left side closed, right side open, alternating)
  for (int circuit = 1; circuit <= 7; circuit++) {
    int r = circuit * spacing;

    // Draw arcs - classical pattern
    // Even circuits: top-left arc
    // Odd circuits: top-right arc
    // This creates the winding path pattern

    for (int angle = 0; angle < 180; angle++) {
      float rad = angle * 3.14159f / 180.0f;

      bool drawTop, drawBottom;

      // Classical labyrinth arc pattern
      if (circuit % 2 == 1) {
        drawTop = (angle >= 0 && angle <= 180);
        drawBottom = false;
      } else {
        drawTop = false;
        drawBottom = (angle >= 0 && angle <= 180);
      }

      if (drawTop) {
        int px = cx + static_cast<int>(r * cosf(rad));
        int py = cy - static_cast<int>(r * sinf(rad));  // Top half
        for (int w = 0; w < 2; w++) {
          if (px >= 0 && px < W && py + w >= 0 && py + w < H) {
            renderer.drawPixel(px, py + w, true);
          }
        }
      }

      if (drawBottom) {
        int px = cx + static_cast<int>(r * cosf(rad));
        int py = cy + static_cast<int>(r * sinf(rad));  // Bottom half
        for (int w = 0; w < 2; w++) {
          if (px >= 0 && px < W && py + w >= 0 && py + w < H) {
            renderer.drawPixel(px, py + w, true);
          }
        }
      }
    }
  }

  // Draw the connecting vertical lines that make the path work
  // Left side connections
  for (int circuit = 1; circuit <= 7; circuit += 2) {
    int r1 = circuit * spacing;
    int r2 = (circuit + 1) * spacing;
    if (r2 <= 7 * spacing) {
      renderer.drawLine(cx - r1, cy, cx - r2, cy, 2, true);
    }
  }

  // Right side connections
  for (int circuit = 2; circuit <= 6; circuit += 2) {
    int r1 = circuit * spacing;
    int r2 = (circuit + 1) * spacing;
    renderer.drawLine(cx + r1, cy, cx + r2, cy, 2, true);
  }

  // Bottom half arcs (complement the top half)
  for (int circuit = 1; circuit <= 7; circuit++) {
    int r = circuit * spacing;

    for (int angle = 180; angle < 360; angle++) {
      float rad = angle * 3.14159f / 180.0f;

      bool draw;
      if (circuit % 2 == 0) {
        draw = true;
      } else {
        draw = true;
      }

      if (draw) {
        int px = cx + static_cast<int>(r * cosf(rad));
        int py = cy - static_cast<int>(r * sinf(rad));
        for (int w = 0; w < 2; w++) {
          if (px >= 0 && px < W && py + w >= 0 && py + w < H) {
            renderer.drawPixel(px, py + w, true);
          }
        }
      }
    }
  }

  // Entrance path at top
  renderer.fillRect(cx - 2, cy - baseR - spacing, 4, spacing, false);

  // Center marker
  fillCircle(cx, cy, 8, true);

  // Decorative corners
  int cornerSize = 30;
  // Top-left
  renderer.drawLine(15, 15, 15 + cornerSize, 15, 2, true);
  renderer.drawLine(15, 15, 15, 15 + cornerSize, 2, true);
  // Top-right
  renderer.drawLine(W - 15, 15, W - 15 - cornerSize, 15, 2, true);
  renderer.drawLine(W - 15, 15, W - 15, 15 + cornerSize, 2, true);
  // Bottom-left
  renderer.drawLine(15, H - 15, 15 + cornerSize, H - 15, 2, true);
  renderer.drawLine(15, H - 15, 15, H - 15 - cornerSize, 2, true);
  // Bottom-right
  renderer.drawLine(W - 15, H - 15, W - 15 - cornerSize, H - 15, 2, true);
  renderer.drawLine(W - 15, H - 15, W - 15, H - 15 - cornerSize, 2, true);
}
