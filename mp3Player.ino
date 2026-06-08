// ============================================================
// CYD Album Player (modular orchestrator)
// ============================================================

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "BluetoothA2DPSource.h"

#include "app_state.h"
#include "audio_engine.h"
#include "bt_manager.h"
#include "media_library.h"
#include "ui_renderer.h"

#define SD_CS 5
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TFT_BL 21
#define BOOT_BUTTON_PIN 0

#define RGB_LED_RED 4
#define RGB_LED_GREEN 16
#define RGB_LED_BLUE 17

#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25

#define TS_MINX 300
#define TS_MAXX 3900
#define TS_MINY 300
#define TS_MAXY 3900

#define COL_BG TFT_BLACK

SPIClass touchSPI(HSPI);
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
BluetoothA2DPSource a2dp;

ScreenMode screenMode = SCREEN_BROWSER;
bool btConnected = false;
char btPeerDisplayName[BT_NAME_BUF] = "";
unsigned long lastProgressUiMs = 0;
int btPickScroll = 0;

static unsigned long lastTouchTime = 0;

static bool displayBacklightOn = true;
static unsigned long lastUserActivityMs = 0;
static uint8_t bootBtnPhase = 0;
static unsigned long bootBtnMs = 0;
static bool mainPlayerUiReady = false;
static bool displayWakeNeedsRedraw = false;

static unsigned long rgbLastMs = 0;
static bool rgbBlinkPhase = false;
static bool rgbPrevBt = false;
static bool rgbPrevPlaying = false;

static inline int footerY() { return SCR_H - browseFooterH; }
static inline int visibleSlots() {
  int vis = (footerY() - browseListY) / browseItemH;
  return max(1, vis);
}
static inline int btPickVisibleSlots() {
  int vis = (footerY() - BT_PICK_LIST_Y) / BT_PICK_ITEM_H;
  return max(1, vis);
}

static void noteUserActivity() {
  if (!displayBacklightOn) return;
  lastUserActivityMs = millis();
}

static void toggleBacklightWithBoot() {
  if (displayBacklightOn) {
    digitalWrite(TFT_BL, LOW);
    displayBacklightOn = false;
  } else {
    digitalWrite(TFT_BL, HIGH);
    displayBacklightOn = true;
    lastUserActivityMs = millis();
    if (mainPlayerUiReady) displayWakeNeedsRedraw = true;
  }
}

static void updateDisplayBacklightTimeout() {
  if (!displayBacklightOn) return;
  if (millis() - lastUserActivityMs >= DISPLAY_IDLE_OFF_MS) {
    digitalWrite(TFT_BL, LOW);
    displayBacklightOn = false;
  }
}

static void pollBootButton() {
  bool down = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  unsigned long now = millis();
  if (bootBtnPhase == 0) {
    if (down) {
      bootBtnPhase = 1;
      bootBtnMs = now;
    }
  } else if (bootBtnPhase == 1) {
    if (!down) {
      bootBtnPhase = 0;
    } else if (now - bootBtnMs >= BOOT_DEBOUNCE_MS) {
      toggleBacklightWithBoot();
      bootBtnPhase = 2;
    }
  } else {
    if (!down) bootBtnPhase = 0;
  }
}

static inline void rgbLedAllOff() {
  digitalWrite(RGB_LED_RED, HIGH);
  digitalWrite(RGB_LED_GREEN, HIGH);
  digitalWrite(RGB_LED_BLUE, HIGH);
}

static void rgbLedInit() {
  pinMode(RGB_LED_RED, OUTPUT);
  pinMode(RGB_LED_GREEN, OUTPUT);
  pinMode(RGB_LED_BLUE, OUTPUT);
  rgbLedAllOff();
}

static void rgbLedUpdate() {
  unsigned long now = millis();
  const bool bt = a2dp.is_connected();
  const bool playing = (playerState == STATE_PLAYING);

  if (bt != rgbPrevBt || playing != rgbPrevPlaying) {
    rgbPrevBt = bt;
    rgbPrevPlaying = playing;
    rgbLastMs = now;
    rgbBlinkPhase = false;
    rgbLedAllOff();
  }

  if (bt && playing) {
    if (now - rgbLastMs >= RGB_PLAY_BLINK_MS) {
      rgbLastMs = now;
      rgbBlinkPhase = !rgbBlinkPhase;
    }
    digitalWrite(RGB_LED_RED, HIGH);
    digitalWrite(RGB_LED_GREEN, rgbBlinkPhase ? LOW : HIGH);
    digitalWrite(RGB_LED_BLUE, rgbBlinkPhase ? HIGH : LOW);
  } else if (!bt) {
    if (now - rgbLastMs >= RGB_SEARCH_BLINK_MS) {
      rgbLastMs = now;
      rgbBlinkPhase = !rgbBlinkPhase;
    }
    digitalWrite(RGB_LED_GREEN, HIGH);
    digitalWrite(RGB_LED_RED, rgbBlinkPhase ? LOW : HIGH);
    digitalWrite(RGB_LED_BLUE, rgbBlinkPhase ? HIGH : LOW);
  } else {
    rgbLedAllOff();
  }
}

static bool getTouchXY(int16_t& tx, int16_t& ty) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  tx = map(p.x, TS_MINX, TS_MAXX, 0, SCR_W);
  ty = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H);
  tx = constrain(tx, 0, SCR_W - 1);
  ty = constrain(ty, 0, SCR_H - 1);
  return true;
}

static void handleBluetoothPickerTouch(bool* redraw) {
  if (!displayBacklightOn) return;

  int16_t tx, ty;
  if (!getTouchXY(tx, ty)) return;
  noteUserActivity();

  unsigned long now = millis();
  if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) return;
  lastTouchTime = now;

  while (ts.touched()) delay(1);

  int fY = footerY();
  int vis = btPickVisibleSlots();
  int n = btScanCountSnapshot();

  if (ty >= fY && ty < SCR_H && tx <= 72) {
    btPickScroll = max(0, btPickScroll - vis);
    if (redraw) *redraw = true;
    return;
  }
  if (ty >= fY && ty < SCR_H && tx >= 168) {
    if (btPickScroll + vis < n) btPickScroll += vis;
    if (redraw) *redraw = true;
    return;
  }

  int listBottom = BT_PICK_LIST_Y + vis * BT_PICK_ITEM_H;
  if (ty < BT_PICK_LIST_Y || ty >= listBottom) return;

  int row = (ty - BT_PICK_LIST_Y) / BT_PICK_ITEM_H;
  int idx = btPickScroll + row;
  if (idx < 0 || idx >= n) return;

  char picked[BT_NAME_BUF];
  if (!btChooseByIndex(idx, picked, sizeof(picked))) return;
  strncpy(btPeerDisplayName, picked, sizeof(btPeerDisplayName) - 1);
  btPeerDisplayName[sizeof(btPeerDisplayName) - 1] = '\0';

  if (redraw) *redraw = true;
}

static void runBluetoothPickerUntilConnected() {
  const unsigned long kPickerShowMs = 2000;
  unsigned long tStart = millis();
  bool showedUi = false;
  int lastSnap = -1;
  bool connectingUi = false;

  btPickScroll = 0;
  btResetPickerState();

  while (!a2dp.is_connected()) {
    rgbLedUpdate();
    pollBootButton();
    updateDisplayBacklightTimeout();

    if (!showedUi && (millis() - tStart > kPickerShowMs)) {
      drawBluetoothPicker(false);
      showedUi = true;
      lastSnap = btScanCountSnapshot();
    }

    if (showedUi) {
      bool redraw = false;
      handleBluetoothPickerTouch(&redraw);
      int snap = btScanCountSnapshot();

      if (btHasUserChoice() && !connectingUi) {
        drawBluetoothPicker(true);
        connectingUi = true;
        lastSnap = snap;
      } else if (redraw && !connectingUi) {
        drawBluetoothPicker(false);
        lastSnap = btScanCountSnapshot();
      } else if (!connectingUi && snap != lastSnap) {
        drawBluetoothPicker(false);
        lastSnap = snap;
      }
    }
    delay(15);
  }

  const char* n = a2dp.get_name();
  if (n && n[0]) {
    strncpy(btPeerDisplayName, n, sizeof(btPeerDisplayName) - 1);
    btPeerDisplayName[sizeof(btPeerDisplayName) - 1] = '\0';
  } else if (!btPeerDisplayName[0]) {
    strncpy(btPeerDisplayName, "BT", sizeof(btPeerDisplayName) - 1);
    btPeerDisplayName[sizeof(btPeerDisplayName) - 1] = '\0';
  }
}

static void handleTouch() {
  if (!displayBacklightOn) return;

  int16_t tx, ty;
  if (!getTouchXY(tx, ty)) return;
  noteUserActivity();

  unsigned long now = millis();
  if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) return;
  lastTouchTime = now;

  {
    unsigned long rel = millis();
    while (ts.touched()) {
      audioPumpPlayingMax(120);
      if (millis() - rel > TOUCH_RELEASE_TIMEOUT_MS) break;
      delay(1);
    }
  }

  if (screenMode == SCREEN_BROWSER) {
    if (browseLevel == BROWSE_TRACKS && ty < browseHeaderH && tx < 64) {
      browseLevel = BROWSE_ALBUMS;
      browseAlbumIdx = -1;
      browseTrackScroll = 0;
      drawBrowser();
      return;
    }

    if (trackCount > 0 &&
        ty >= BROWSE_PLAYER_BTN_Y && ty < BROWSE_PLAYER_BTN_Y + BROWSE_PLAYER_BTN_H &&
        tx >= BROWSE_PLAYER_BTN_X && tx < BROWSE_PLAYER_BTN_X + BROWSE_PLAYER_BTN_W) {
      screenMode = SCREEN_PLAYER;
      drawPlayer();
      return;
    }

    if (trackCount > 0 &&
        ty >= BROWSE_PATH_PLAY_BTN_Y && ty < BROWSE_PATH_PLAY_BTN_Y + BROWSE_PATH_PLAY_BTN_H &&
        tx >= BROWSE_PATH_PLAY_BTN_X && tx < BROWSE_PATH_PLAY_BTN_X + BROWSE_PATH_PLAY_BTN_W) {
      screenMode = SCREEN_PLAYER;
      drawPlayer();
      return;
    }

    int fY = footerY();
    int vis = visibleSlots();
    int itemCount = (browseLevel == BROWSE_ALBUMS) ? albumCount : browseTrackCount;

    if (ty >= fY && ty <= fY + browseFooterH && tx <= 72) {
      if (browseLevel == BROWSE_ALBUMS) albumScroll = max(0, albumScroll - vis);
      else browseTrackScroll = max(0, browseTrackScroll - vis);
      drawBrowser();
      return;
    }
    if (ty >= fY && ty <= fY + browseFooterH && tx >= 168) {
      if (browseLevel == BROWSE_ALBUMS) {
        if (albumScroll + vis < albumCount) albumScroll += vis;
      } else {
        if (browseTrackScroll + vis < browseTrackCount) browseTrackScroll += vis;
      }
      drawBrowser();
      return;
    }

    int listBottom = browseListY + vis * browseItemH;
    if (ty < browseListY || ty >= listBottom) return;

    int idx = ((browseLevel == BROWSE_ALBUMS) ? albumScroll : browseTrackScroll) +
              ((ty - browseListY) / browseItemH);
    if (idx < 0 || idx >= itemCount) return;

    if (browseLevel == BROWSE_ALBUMS) {
      browseAlbumIdx = idx;
      loadBrowseAlbumTracks(albums[idx]);
      browseLevel = BROWSE_TRACKS;
      browseTrackScroll = 0;
      drawBrowser();
      return;
    }

    if (playerState != STATE_STOPPED) stopTrack(true);
    startPlayingFromPlaylistIndex(browseTrackIndices[idx]);
    screenMode = SCREEN_PLAYER;
    drawPlayer();
    return;
  }

  if (ty >= PL_BACK_BTN_Y && ty < PL_BACK_BTN_Y + PL_BACK_BTN_H &&
      tx >= PL_BACK_BTN_X && tx < PL_BACK_BTN_X + PL_BACK_BTN_W) {
    screenMode = SCREEN_BROWSER;
    drawBrowser();
    audioPumpPlayingMax(512);
    return;
  }

  if (ty >= PL_VOLUME_Y && ty <= PL_VOLUME_Y + 32) {
    if (tx < 68) {
      volumePercent -= 10;
      applyVolumePercent();
      drawVolumeControls();
      return;
    }
    if (tx >= 172) {
      volumePercent += 10;
      applyVolumePercent();
      drawVolumeControls();
      return;
    }
  }

  if (ty >= PL_TRANSPORT_Y && ty <= PL_TRANSPORT_Y + 44) {
    if (tx < 68) prevTrack();
    else if (tx < 172) togglePause();
    else nextTrack();
    drawPlayer();
  }
}

void setup() {
  Serial.begin(115200);
  delay(NO_MUSIC_DELAY_MS);

  rgbLedInit();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  tft.init();
  tft.writecommand(0x26);
  tft.writedata(2);
  delay(120);
  tft.writecommand(0x26);
  tft.writedata(1);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  SPI.begin();
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);

  if (!SD.begin(SD_CS)) {
    tft.setTextColor(TFT_RED, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 90);
    tft.print("SD Failed!");
    while (1) delay(1000);
  }

  audioInit();
  mediaSetPumpCallback(audioPumpPlayingMax);

  drawStartupScreen();

  a2dp.set_auto_reconnect(false);
  a2dp.clean_last_connection();
  audioSetSinkVolumeCallback([](int sinkVol) { a2dp.set_volume(sinkVol); });
  applyVolumePercent();
  a2dp.set_data_callback_in_frames(audioBtCallback);
  a2dp.set_ssid_callback(btScanSsidCallback);
  a2dp.start();

  runBluetoothPickerUntilConnected();
  btConnected = a2dp.is_connected();
  rgbPrevBt = btConnected;
  rgbPrevPlaying = (playerState == STATE_PLAYING);

  scanSD();
  if (trackCount == 0) {
    tft.setTextColor(TFT_YELLOW, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 120);
    tft.print("No music");
    while (1) {
      rgbLedUpdate();
      delay(NO_MUSIC_DELAY_MS);
    }
  }

  scanAlbums();
  browseLevel = BROWSE_ALBUMS;
  browseAlbumIdx = -1;
  browseTrackScroll = 0;
  screenMode = SCREEN_BROWSER;
  albumScroll = 0;
  drawBrowser();

  lastUserActivityMs = millis();
  mainPlayerUiReady = true;
}

void loop() {
  rgbLedUpdate();
  pollBootButton();
  updateDisplayBacklightTimeout();

  if (displayWakeNeedsRedraw) {
    displayWakeNeedsRedraw = false;
    if (screenMode == SCREEN_PLAYER) drawPlayer();
    else drawBrowser();
  }

  if (displayBacklightOn) {
    if (screenMode == SCREEN_PLAYER &&
        (playerState == STATE_PLAYING || playerState == STATE_PAUSED)) {
      unsigned long now = millis();
      if (now - lastProgressUiMs >= UI_PROGRESS_REFRESH_MS) {
        lastProgressUiMs = now;
        drawPlayerProgressArea();
      }
    }
    if (screenMode == SCREEN_PLAYER) updateVisualizerAnimation();
  }

  if (audioServicePlaybackLoop()) {
    if (screenMode == SCREEN_PLAYER && displayBacklightOn) drawPlayer();
  }

  handleTouch();
}

