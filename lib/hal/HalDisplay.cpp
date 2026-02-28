#include <HalDisplay.h>
#include <HalGPIO.h>

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin() { einkDisplay.begin(); }

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  uint8_t* frameBuffer = einkDisplay.getFrameBuffer();
  if (!frameBuffer || !imageData || w == 0 || h == 0) {
    return;
  }

  const uint16_t srcWidthBytes = (w + 7) / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= EInkDisplay::DISPLAY_HEIGHT) {
      break;
    }

    const size_t srcRowOffset = static_cast<size_t>(row) * srcWidthBytes;
    const size_t destRowOffset = static_cast<size_t>(destY) * EInkDisplay::DISPLAY_WIDTH_BYTES;

    for (uint16_t col = 0; col < w; col++) {
      const uint16_t destX = x + col;
      if (destX >= EInkDisplay::DISPLAY_WIDTH) {
        break;
      }

      const size_t srcByteIndex = srcRowOffset + (col / 8);
      const uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcByteIndex]) : imageData[srcByteIndex];
      const uint8_t srcBit = (srcByte >> (7 - (col % 8))) & 0x01;

      // Icon bitmaps use 1=white (transparent) and 0=black (draw).
      if (srcBit == 0) {
        const size_t destByteIndex = destRowOffset + (destX / 8);
        const uint8_t destBit = 7 - (destX % 8);
        frameBuffer[destByteIndex] &= ~(1U << destBit);
      }
    }
  }
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }
