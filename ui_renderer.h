#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <lvgl.h>

#ifndef ENABLE_JPG_COVER
#define ENABLE_JPG_COVER 0
#endif

#ifndef UI_DEBUG_BREADCRUMBS
#define UI_DEBUG_BREADCRUMBS 0
#endif

static void uiTrace(const char* tag) {
#if UI_DEBUG_BREADCRUMBS
  Serial.print("[UI] ");
  Serial.print((unsigned long)millis());
  Serial.print(' ');
  Serial.println(tag);
#else
  (void)tag;
#endif
}

static bool lvUiReady = false;

static lv_display_t* lvDisplay = nullptr;
static lv_indev_t* lvInput = nullptr;

static uint8_t* lvBufA = nullptr;
static uint8_t* lvBufB = nullptr;
static size_t lvBufBytes = 0;

static bool uiAllocLvBuffers() {
  if (lvBufA && lvBufB && lvBufBytes > 0) return true;

  const int lineCandidates[] = {20, 16, 12, 10, 8, 6, 4, 2, 1};
  const uint32_t caps[] = {MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT};

  for (size_t ci = 0; ci < (sizeof(caps) / sizeof(caps[0])); ci++) {
    for (size_t li = 0; li < (sizeof(lineCandidates) / sizeof(lineCandidates[0])); li++) {
      size_t bytes = (size_t)SCR_W * (size_t)lineCandidates[li] * 2u;
      uint8_t* a = (uint8_t*)heap_caps_malloc(bytes, caps[ci]);
      if (!a) continue;
      uint8_t* b = (uint8_t*)heap_caps_malloc(bytes, caps[ci]);
      if (!b) {
        heap_caps_free(a);
        continue;
      }

      lvBufA = a;
      lvBufB = b;
      lvBufBytes = bytes;
      return true;
    }
  }

  return false;
}

static lv_fs_drv_t lvFsDrv;
static bool lvFsRegistered = false;

static lv_obj_t* uiRoot = nullptr;

// Player widgets
static lv_obj_t* plProgress = nullptr;
static lv_obj_t* plElapsed = nullptr;
static lv_obj_t* plTotal = nullptr;
static lv_obj_t* plTech = nullptr;
static lv_obj_t* plVol = nullptr;
static lv_obj_t* plCover = nullptr;
static lv_obj_t* plCoverFallback = nullptr;

static char currentCoverSrc[128];

// Forward declarations used by callbacks
static void drawBrowser();
static void drawPlayer();
static void drawPlayerProgressArea();
static void drawVolumeControls();
static void drawBluetoothPicker(bool connecting);

// Bluetooth picker UI geometry
static const int BT_PICK_HEADER_H = 30;
static const int BT_PICK_LIST_Y = 42;
static const int BT_PICK_ITEM_H = 24;
static int btPickScroll = 0;

static inline int btPickVisibleSlots() {
  int vis = (footerY() - BT_PICK_LIST_Y) / BT_PICK_ITEM_H;
  return max(1, vis);
}

static uint32_t lvTickCb() {
  return (uint32_t)millis();
}

static void lvFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap) {
  uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
  uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)pxMap, (uint32_t)w * (uint32_t)h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void lvInputReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  if (!displayBacklightOn || !ts.touched()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  TS_Point p = ts.getPoint();
  int16_t tx = map(p.x, TS_MINX, TS_MAXX, 0, SCR_W);
  int16_t ty = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H);
  tx = constrain(tx, 0, SCR_W - 1);
  ty = constrain(ty, 0, SCR_H - 1);

  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = tx;
  data->point.y = ty;
  noteUserActivity();
}

// ---- LVGL FS driver for SD card (S:/...) ----
struct LvSdFile {
  File f;
};

static void* lvFsOpenCb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
  (void)drv;
  if (mode != LV_FS_MODE_RD) return nullptr;

  const char* openPath = path;
  if (openPath && openPath[0] == '/') openPath++;

  LvSdFile* h = new LvSdFile();
  if (!h) return nullptr;
  h->f = SD.open(String("/") + openPath, FILE_READ);
  if (!h->f) {
    delete h;
    return nullptr;
  }
  return h;
}

static lv_fs_res_t lvFsCloseCb(lv_fs_drv_t* drv, void* fileP) {
  (void)drv;
  LvSdFile* h = (LvSdFile*)fileP;
  if (!h) return LV_FS_RES_UNKNOWN;
  if (h->f) h->f.close();
  delete h;
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvFsReadCb(lv_fs_drv_t* drv, void* fileP, void* buf, uint32_t btr, uint32_t* br) {
  (void)drv;
  LvSdFile* h = (LvSdFile*)fileP;
  if (!h || !h->f) return LV_FS_RES_UNKNOWN;
  size_t n = h->f.read((uint8_t*)buf, btr);
  if (br) *br = (uint32_t)n;
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvFsSeekCb(lv_fs_drv_t* drv, void* fileP, uint32_t pos, lv_fs_whence_t whence) {
  (void)drv;
  LvSdFile* h = (LvSdFile*)fileP;
  if (!h || !h->f) return LV_FS_RES_UNKNOWN;
  SeekMode m = SeekSet;
  if (whence == LV_FS_SEEK_CUR) m = SeekCur;
  else if (whence == LV_FS_SEEK_END) m = SeekEnd;
  return h->f.seek(pos, m) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t lvFsTellCb(lv_fs_drv_t* drv, void* fileP, uint32_t* posP) {
  (void)drv;
  LvSdFile* h = (LvSdFile*)fileP;
  if (!h || !h->f || !posP) return LV_FS_RES_UNKNOWN;
  *posP = (uint32_t)h->f.position();
  return LV_FS_RES_OK;
}

static void uiRegisterLvSdFs() {
  if (lvFsRegistered) return;
  lv_fs_drv_init(&lvFsDrv);
  lvFsDrv.letter = 'S';
  lvFsDrv.open_cb = lvFsOpenCb;
  lvFsDrv.close_cb = lvFsCloseCb;
  lvFsDrv.read_cb = lvFsReadCb;
  lvFsDrv.seek_cb = lvFsSeekCb;
  lvFsDrv.tell_cb = lvFsTellCb;
  lv_fs_drv_register(&lvFsDrv);
  lvFsRegistered = true;
}

static void uiLvInit() {
  if (lvUiReady) return;
  uiTrace("uiLvInit:start");

  if (!uiAllocLvBuffers()) {
    uiTrace("uiLvInit:alloc-fail");
    tft.fillScreen(COL_BG);
    tft.setTextColor(TFT_RED, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(12, 120);
    tft.print("No RAM for LVGL");
    while (1) delay(1000);
  }

  lv_init();
  lv_tick_set_cb(lvTickCb);
  uiTrace("uiLvInit:lv-init-ok");

  lvDisplay = lv_display_create(SCR_W, SCR_H);
  if (!lvDisplay) {
    uiTrace("uiLvInit:display-fail");
    while (1) delay(1000);
  }
  lv_display_set_flush_cb(lvDisplay, lvFlushCb);
  lv_display_set_buffers(lvDisplay, lvBufA, lvBufB, lvBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  uiTrace("uiLvInit:display-ok");

  lvInput = lv_indev_create();
  if (!lvInput) {
    uiTrace("uiLvInit:input-fail");
    while (1) delay(1000);
  }
  lv_indev_set_type(lvInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvInput, lvInputReadCb);
  uiTrace("uiLvInit:input-ok");

  uiRegisterLvSdFs();

  lvUiReady = true;
  uiTrace("uiLvInit:ready");
}

static void uiBeginScreen() {
  if (!lvUiReady) {
    uiRoot = nullptr;
    uiTrace("uiBeginScreen:not-ready");
    return;
  }

  lv_obj_t* scr = lv_obj_create(nullptr);
  if (!scr) {
    uiRoot = nullptr;
    uiTrace("uiBeginScreen:scr-fail");
    return;
  }
  lv_obj_set_size(scr, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_screen_load(scr);
  uiRoot = scr;
  uiTrace("uiBeginScreen:ok");
}

static bool hasExtInsensitive(const char* name, const char* ext) {
  int nl = (int)strlen(name);
  int el = (int)strlen(ext);
  if (nl < el) return false;
  return strcasecmp(name + (nl - el), ext) == 0;
}

static bool findAlbumArtForCurrentTrack(char* outPath, size_t outLen) {
  outPath[0] = '\0';
  if (trackCount <= 0 || currentTrack < 0 || currentTrack >= trackCount) return false;

  const char* tr = playlist[currentTrack];
  const char* slash = strrchr(tr, '/');
  if (!slash) return false;

  char folder[128];
  size_t fl = (size_t)(slash - tr);
  if (fl == 0 || fl >= sizeof(folder)) return false;
  memcpy(folder, tr, fl);
  folder[fl] = '\0';

  const char* preferred[] = {
    "cover.jpg", "folder.jpg", "album.jpg", "front.jpg", "cover.jpeg", "folder.jpeg"
  };

  for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); i++) {
    char c[160];
    snprintf(c, sizeof(c), "%s/%s", folder, preferred[i]);
    File f = SD.open(c, FILE_READ);
    if (f) {
      f.close();
      strncpy(outPath, c, outLen - 1);
      outPath[outLen - 1] = '\0';
      return true;
    }
  }

  File d = SD.open(folder);
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return false;
  }

  bool ok = false;
  while (true) {
    File e = d.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      const char* n = e.name();
      if (n && (hasExtInsensitive(n, ".jpg") || hasExtInsensitive(n, ".jpeg"))) {
        char c[160];
        snprintf(c, sizeof(c), "%s/%s", folder, n);
        strncpy(outPath, c, outLen - 1);
        outPath[outLen - 1] = '\0';
        ok = true;
        e.close();
        break;
      }
    }
    e.close();
  }
  d.close();
  return ok;
}

static void uiShowCoverInPlayer() {
  if (!plCoverFallback) return;

#if !ENABLE_JPG_COVER
  if (plCover) lv_obj_add_flag(plCover, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(plCoverFallback, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(plCoverFallback, "Portada JPG desactivada");
  return;
#endif

  if (!plCover) return;

  char cover[128];
  bool found = findAlbumArtForCurrentTrack(cover, sizeof(cover));
  if (!found) {
    lv_obj_add_flag(plCover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(plCoverFallback, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(plCoverFallback, "Sin portada JPG");
    return;
  }

  char src[140];
  snprintf(src, sizeof(src), "S:%s", cover);
  strncpy(currentCoverSrc, src, sizeof(currentCoverSrc) - 1);
  currentCoverSrc[sizeof(currentCoverSrc) - 1] = '\0';

  lv_obj_clear_flag(plCover, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(plCoverFallback, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(plCover, currentCoverSrc);
}

static void cbBrowserPrev(lv_event_t* e) {
  (void)e;
  int vis = visibleSlots();
  if (browseLevel == BROWSE_ALBUMS) albumScroll = max(0, albumScroll - vis);
  else browseTrackScroll = max(0, browseTrackScroll - vis);
  drawBrowser();
}

static void cbBrowserNext(lv_event_t* e) {
  (void)e;
  int vis = visibleSlots();
  if (browseLevel == BROWSE_ALBUMS) {
    if (albumScroll + vis < albumCount) albumScroll += vis;
  } else {
    if (browseTrackScroll + vis < browseTrackCount) browseTrackScroll += vis;
  }
  drawBrowser();
}

static void cbBrowserBack(lv_event_t* e) {
  (void)e;
  browseLevel = BROWSE_ALBUMS;
  browseAlbumIdx = -1;
  browseTrackScroll = 0;
  drawBrowser();
}

static void cbGoPlayer(lv_event_t* e) {
  (void)e;
  if (trackCount <= 0) return;
  screenMode = SCREEN_PLAYER;
  drawPlayer();
}

static void cbBrowserItem(lv_event_t* e) {
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  if (browseLevel == BROWSE_ALBUMS) {
    browseAlbumIdx = (int)idx;
    loadBrowseAlbumTracks(albums[idx]);
    browseLevel = BROWSE_TRACKS;
    browseTrackScroll = 0;
    drawBrowser();
    return;
  }

  if (idx < 0 || idx >= browseTrackCount) return;
  if (playerState != STATE_STOPPED) stopTrack(true);
  startPlayingFromPlaylistIndex(browseTrackIndices[idx]);
  screenMode = SCREEN_PLAYER;
  drawPlayer();
}

static void cbPlayerBack(lv_event_t* e) {
  (void)e;
  screenMode = SCREEN_BROWSER;
  drawBrowser();
}

static void cbVolDown(lv_event_t* e) {
  (void)e;
  volumePercent -= 10;
  applyVolumePercent();
  drawVolumeControls();
}

static void cbVolUp(lv_event_t* e) {
  (void)e;
  volumePercent += 10;
  applyVolumePercent();
  drawVolumeControls();
}

static void cbPrevTrack(lv_event_t* e) {
  (void)e;
  prevTrack();
  drawPlayer();
}

static void cbPlayPause(lv_event_t* e) {
  (void)e;
  togglePause();
  drawPlayerProgressArea();
}

static void cbNextTrack(lv_event_t* e) {
  (void)e;
  nextTrack();
  drawPlayer();
}

static void cbPickerPrev(lv_event_t* e) {
  (void)e;
  int vis = btPickVisibleSlots();
  btPickScroll = max(0, btPickScroll - vis);
  drawBluetoothPicker(false);
}

static void cbPickerNext(lv_event_t* e) {
  (void)e;
  int vis = btPickVisibleSlots();
  int n = btScanCountSnapshot();
  if (btPickScroll + vis < n) btPickScroll += vis;
  drawBluetoothPicker(false);
}

static void cbPickerItem(lv_event_t* e) {
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  int n = btScanCountSnapshot();
  if (idx < 0 || idx >= n) return;

  portENTER_CRITICAL(&btScanMux);
  memcpy(btChosenAddr, btScanList[idx].addr, ESP_BD_ADDR_LEN);
  strncpy(btPeerDisplayName, btScanList[idx].name, sizeof(btPeerDisplayName) - 1);
  btPeerDisplayName[sizeof(btPeerDisplayName) - 1] = '\0';
  btUserHasChoice = true;
  portEXIT_CRITICAL(&btScanMux);

  drawBluetoothPicker(true);
}

static void drawBrowser() {
  if (!lvUiReady) return;

  uiBeginScreen();
  if (!uiRoot) {
    uiTrace("drawBrowser:no-root");
    return;
  }

  lv_obj_t* header = lv_obj_create(uiRoot);
  if (!header) {
    uiTrace("drawBrowser:header-fail");
    return;
  }
  lv_obj_set_size(header, SCR_W, browseHeaderH);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x1e1e1e), 0);

  if (browseLevel == BROWSE_TRACKS) {
    lv_obj_t* back = lv_button_create(header);
    lv_obj_set_size(back, 64, 22);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_add_event_cb(back, cbBrowserBack, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* txt = lv_label_create(back);
    lv_label_set_text(txt, STR_BACK_LIST);
    lv_obj_center(txt);
  } else {
    lv_obj_t* ttl = lv_label_create(header);
    lv_label_set_text(ttl, STR_ALBUMS);
    lv_obj_align(ttl, LV_ALIGN_LEFT_MID, 8, 0);
  }

  if (trackCount > 0) {
    lv_obj_t* playerBtn = lv_button_create(header);
    lv_obj_set_size(playerBtn, BROWSE_PLAYER_BTN_W, BROWSE_PLAYER_BTN_H);
    lv_obj_set_pos(playerBtn, BROWSE_PLAYER_BTN_X, BROWSE_PLAYER_BTN_Y);
    lv_obj_add_event_cb(playerBtn, cbGoPlayer, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ptxt = lv_label_create(playerBtn);
    lv_label_set_text(ptxt, STR_PLAYER_BTN);
    lv_obj_center(ptxt);

    lv_obj_t* pathPlay = lv_button_create(uiRoot);
    lv_obj_set_size(pathPlay, BROWSE_PATH_PLAY_BTN_W, BROWSE_PATH_PLAY_BTN_H);
    lv_obj_set_pos(pathPlay, BROWSE_PATH_PLAY_BTN_X, BROWSE_PATH_PLAY_BTN_Y);
    lv_obj_add_event_cb(pathPlay, cbGoPlayer, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* tri = lv_label_create(pathPlay);
    lv_label_set_text(tri, LV_SYMBOL_PLAY);
    lv_obj_center(tri);
  }

  lv_obj_t* btBadge = lv_label_create(header);
  lv_label_set_text(btBadge, STR_BT_LABEL);
  lv_obj_align(btBadge, LV_ALIGN_RIGHT_MID, -58, 0);

  lv_obj_t* btText = lv_label_create(header);
  if (btConnected && btPeerDisplayName[0]) lv_label_set_text(btText, btPeerDisplayName);
  else if (btConnected) lv_label_set_text(btText, STR_BT_CONNECTED);
  else lv_label_set_text(btText, STR_BT_DISCONNECTED);
  lv_obj_align(btText, LV_ALIGN_RIGHT_MID, -8, 0);

  int vis = visibleSlots();
  int itemCount = (browseLevel == BROWSE_ALBUMS) ? albumCount : browseTrackCount;
  int scroll = (browseLevel == BROWSE_ALBUMS) ? albumScroll : browseTrackScroll;

  lv_obj_t* brList = lv_list_create(uiRoot);
  if (!brList) {
    uiTrace("drawBrowser:list-fail");
    return;
  }
  lv_obj_set_size(brList, SCR_W - 12, vis * browseItemH + 6);
  lv_obj_set_pos(brList, 6, browseListY);

  for (int i = 0; i < vis; i++) {
    int idx = scroll + i;
    if (idx >= itemCount) break;

    char line[64];
    if (browseLevel == BROWSE_ALBUMS) {
      snprintf(line, sizeof(line), "%s", albums[idx]);
    } else {
      int pi = browseTrackIndices[idx];
      char disp[44];
      getDisplayName(playlist[pi], disp, sizeof(disp));
      snprintf(line, sizeof(line), "%s", disp);
    }

    lv_obj_t* b = lv_list_add_button(brList, nullptr, line);
    lv_obj_add_event_cb(b, cbBrowserItem, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
  }

  int fy = footerY();
  lv_obj_t* prev = lv_button_create(uiRoot);
  if (!prev) {
    uiTrace("drawBrowser:prev-fail");
    return;
  }
  lv_obj_set_size(prev, 58, 24);
  lv_obj_set_pos(prev, 8, fy + 6);
  lv_obj_add_event_cb(prev, cbBrowserPrev, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ptxt = lv_label_create(prev);
  lv_label_set_text(ptxt, STR_PREV_BTN);
  lv_obj_center(ptxt);

  lv_obj_t* next = lv_button_create(uiRoot);
  if (!next) {
    uiTrace("drawBrowser:next-fail");
    return;
  }
  lv_obj_set_size(next, 58, 24);
  lv_obj_set_pos(next, 174, fy + 6);
  lv_obj_add_event_cb(next, cbBrowserNext, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ntxt = lv_label_create(next);
  lv_label_set_text(ntxt, STR_NEXT_BTN);
  lv_obj_center(ntxt);

  int totalPages = (itemCount + vis - 1) / vis;
  if (totalPages < 1) totalPages = 1;
  int page = (scroll / vis) + 1;
  if (page > totalPages) page = totalPages;

  char pageTxt[16];
  snprintf(pageTxt, sizeof(pageTxt), "%d/%d", page, totalPages);
  lv_obj_t* pageLbl = lv_label_create(uiRoot);
  if (!pageLbl) {
    uiTrace("drawBrowser:page-fail");
    return;
  }
  lv_label_set_text(pageLbl, pageTxt);
  lv_obj_align(pageLbl, LV_ALIGN_BOTTOM_MID, 0, -12);

  uiTrace("drawBrowser:done");
  lv_timer_handler();
}

static void drawPlayerProgressArea() {
  if (!lvUiReady || !plProgress) return;

  uint32_t el = elapsedPlaybackSec();
  uint32_t val = 0;
  if (cachedDurationSec > 0) {
    val = (uint32_t)(((uint64_t)el * 100u) / (uint64_t)cachedDurationSec);
    if (val > 100) val = 100;
  }
  lv_bar_set_value(plProgress, (int)val, LV_ANIM_OFF);

  char tEl[16], tTot[16];
  formatTimeHMS(tEl, sizeof(tEl), el);
  if (cachedDurationSec > 0) formatTimeHMS(tTot, sizeof(tTot), cachedDurationSec);
  else {
    strncpy(tTot, STR_TIME_UNKNOWN, sizeof(tTot) - 1);
    tTot[sizeof(tTot) - 1] = '\0';
  }

  if (plElapsed) lv_label_set_text(plElapsed, tEl);
  if (plTotal) lv_label_set_text(plTotal, tTot);

  if (plTech) {
    char tech[40];
    if (currentType == AUDIO_WAV && cachedWavRateHz > 0) {
      snprintf(tech, sizeof(tech), STR_AUDIO_WAV_FMT, (unsigned long)cachedWavRateHz);
      lv_label_set_text(plTech, tech);
    } else if (currentType == AUDIO_MP3) {
      lv_label_set_text(plTech, STR_AUDIO_MP3);
    } else {
      lv_label_set_text(plTech, STR_AUDIO_UNKNOWN);
    }
  }
}

static void drawVolumeControls() {
  if (!plVol) return;
  char v[12];
  snprintf(v, sizeof(v), "%d%%", volumePercent);
  lv_label_set_text(plVol, v);
}

static void drawPlayer() {
  if (!lvUiReady) return;

  uiBeginScreen();
  if (!uiRoot) {
    uiTrace("drawPlayer:no-root");
    return;
  }

  lv_obj_t* header = lv_obj_create(uiRoot);
  if (!header) {
    uiTrace("drawPlayer:header-fail");
    return;
  }
  lv_obj_set_size(header, SCR_W, PL_HEADER_H);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x101214), 0);

  lv_obj_t* plCounter = lv_label_create(header);
  if (trackCount > 0) {
    char c[18];
    snprintf(c, sizeof(c), "%d/%d", currentTrack + 1, trackCount);
    lv_label_set_text(plCounter, c);
  } else {
    lv_label_set_text(plCounter, STR_TRACK_COUNTER_EMPTY);
  }
  lv_obj_align(plCounter, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t* plAlbum = lv_label_create(header);
  lv_label_set_text(plAlbum, currentAlbumFolder[0] ? currentAlbumFolder : "-");
  lv_obj_align(plAlbum, LV_ALIGN_LEFT_MID, 56, 0);

  lv_obj_t* plBt = lv_label_create(header);
  lv_label_set_text(plBt, a2dp.is_connected() ? STR_BT_CONNECTED : STR_BT_DISCONNECTED);
  lv_obj_align(plBt, LV_ALIGN_RIGHT_MID, -82, 0);

  lv_obj_t* backBtn = lv_button_create(header);
  lv_obj_set_size(backBtn, PL_BACK_BTN_W, PL_BACK_BTN_H);
  lv_obj_set_pos(backBtn, PL_BACK_BTN_X, PL_BACK_BTN_Y);
  lv_obj_add_event_cb(backBtn, cbPlayerBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* btxt = lv_label_create(backBtn);
  lv_label_set_text(btxt, "Lista");
  lv_obj_center(btxt);

  lv_obj_t* plTitle = lv_label_create(uiRoot);
  if (trackCount > 0) {
    char title[64];
    getDisplayName(playlist[currentTrack], title, sizeof(title));
    lv_label_set_text(plTitle, title);
  } else {
    lv_label_set_text(plTitle, STR_NO_TRACK);
  }
  lv_obj_align(plTitle, LV_ALIGN_TOP_MID, 0, PL_TITLE_Y - 2);

  // Cover area replaces FFT visualization.
  lv_obj_t* coverPanel = lv_obj_create(uiRoot);
  if (!coverPanel) {
    uiTrace("drawPlayer:cover-fail");
    return;
  }
  lv_obj_set_size(coverPanel, 224, 108);
  lv_obj_set_pos(coverPanel, 8, PL_CASSETTE_TOP);
  lv_obj_set_style_bg_color(coverPanel, lv_color_hex(0x121212), 0);

#if ENABLE_JPG_COVER
  plCover = lv_image_create(coverPanel);
  lv_obj_center(plCover);
#else
  plCover = nullptr;
#endif

  plCoverFallback = lv_label_create(coverPanel);
  lv_label_set_text(plCoverFallback, ENABLE_JPG_COVER ? "Sin portada JPG" : "Portada JPG desactivada");
  lv_obj_center(plCoverFallback);

  uiShowCoverInPlayer();

  plProgress = lv_bar_create(uiRoot);
  if (!plProgress) {
    uiTrace("drawPlayer:progress-fail");
    return;
  }
  lv_obj_set_size(plProgress, SCR_W - 20, 8);
  lv_obj_set_pos(plProgress, 10, PL_PROGRESS_TOP + 2);
  lv_bar_set_range(plProgress, 0, 100);

  plElapsed = lv_label_create(uiRoot);
  lv_obj_set_pos(plElapsed, 10, PL_PROGRESS_TOP + 14);

  plTotal = lv_label_create(uiRoot);
  lv_obj_set_pos(plTotal, SCR_W - 70, PL_PROGRESS_TOP + 14);

  plTech = lv_label_create(uiRoot);
  lv_obj_set_pos(plTech, 10, PL_PROGRESS_TOP + 38);

  lv_obj_t* vdown = lv_button_create(uiRoot);
  lv_obj_set_size(vdown, 56, 30);
  lv_obj_set_pos(vdown, 10, PL_VOLUME_Y);
  lv_obj_add_event_cb(vdown, cbVolDown, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vdt = lv_label_create(vdown);
  lv_label_set_text(vdt, "-");
  lv_obj_center(vdt);

  lv_obj_t* vmid = lv_obj_create(uiRoot);
  lv_obj_set_size(vmid, 92, 30);
  lv_obj_set_pos(vmid, 74, PL_VOLUME_Y);
  plVol = lv_label_create(vmid);
  lv_obj_center(plVol);

  lv_obj_t* vup = lv_button_create(uiRoot);
  lv_obj_set_size(vup, 56, 30);
  lv_obj_set_pos(vup, 174, PL_VOLUME_Y);
  lv_obj_add_event_cb(vup, cbVolUp, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vut = lv_label_create(vup);
  lv_label_set_text(vut, "+");
  lv_obj_center(vut);

  lv_obj_t* prev = lv_button_create(uiRoot);
  lv_obj_set_size(prev, 56, 42);
  lv_obj_set_pos(prev, 10, PL_TRANSPORT_Y);
  lv_obj_add_event_cb(prev, cbPrevTrack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ptxt = lv_label_create(prev);
  lv_label_set_text(ptxt, "<<");
  lv_obj_center(ptxt);

  lv_obj_t* play = lv_button_create(uiRoot);
  lv_obj_set_size(play, 92, 42);
  lv_obj_set_pos(play, 74, PL_TRANSPORT_Y);
  lv_obj_add_event_cb(play, cbPlayPause, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pltxt = lv_label_create(play);
  lv_label_set_text(pltxt, (playerState == STATE_PLAYING) ? "||" : LV_SYMBOL_PLAY);
  lv_obj_center(pltxt);

  lv_obj_t* next = lv_button_create(uiRoot);
  lv_obj_set_size(next, 56, 42);
  lv_obj_set_pos(next, 174, PL_TRANSPORT_Y);
  lv_obj_add_event_cb(next, cbNextTrack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ntxt = lv_label_create(next);
  lv_label_set_text(ntxt, ">>");
  lv_obj_center(ntxt);

  drawVolumeControls();
  drawPlayerProgressArea();
  lastProgressUiMs = millis();

  uiTrace("drawPlayer:done");
  lv_timer_handler();
}

// FFT removed when using LVGL cover-art player.
static void updateVisualizerAnimation() {}

// Keep startup screen with TFT to preserve existing splash behavior.
static const char* const SPLASH_RAW_PATH = "/guara565.raw";
static const int SP_RAW_W = 200;
static const int SP_RAW_H = 218;

static bool tryDrawSplashFromSD(int dstX, int dstY) {
  File f = SD.open(SPLASH_RAW_PATH, FILE_READ);
  if (!f) return false;
  long need = (long)SP_RAW_W * (long)SP_RAW_H * 2L;
  if (f.size() != need) {
    f.close();
    return false;
  }
  uint16_t line[SP_RAW_W];
  for (int y = 0; y < SP_RAW_H; y++) {
    if (f.read((uint8_t*)line, SP_RAW_W * 2) != (size_t)(SP_RAW_W * 2)) {
      f.close();
      return false;
    }
    tft.pushImage(dstX, dstY + y, SP_RAW_W, 1, line);
  }
  f.close();
  return true;
}

static void drawStartupScreen() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  const char* wel = STR_WELCOME;
  int welW = (int)strlen(wel) * 12;
  tft.setCursor((SCR_W - welW) / 2, 8);
  tft.print(wel);

  const int lx = 12, ly = 40, lw = 216, lh = 258;
  tft.drawRoundRect(lx, ly, lw, lh, 10, tft.color565(48, 48, 56));

  const int innerX = lx + 6;
  const int innerY = ly + 6;
  const int innerW = lw - 12;
  const int innerH = lh - 12;
  const int rawX = innerX + (innerW - SP_RAW_W) / 2;
  const int rawY = innerY + (innerH - SP_RAW_H) / 2;

  if (!tryDrawSplashFromSD(rawX, rawY)) {
    tft.fillRect(innerX, innerY, innerW, innerH, COL_BG);
  }

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(40, 306);
  tft.print(STR_PREPARING_BT);
}

static void drawBluetoothPicker(bool connecting) {
  if (!lvUiReady) return;

  uiBeginScreen();
  if (!uiRoot) {
    uiTrace("drawBtPicker:no-root");
    return;
  }

  lv_obj_t* header = lv_obj_create(uiRoot);
  if (!header) {
    uiTrace("drawBtPicker:header-fail");
    return;
  }
  lv_obj_set_size(header, SCR_W, BT_PICK_HEADER_H);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);

  lv_obj_t* ht = lv_label_create(header);
  lv_label_set_text(ht, STR_BT_PICKER_HEADER);
  lv_obj_align(ht, LV_ALIGN_LEFT_MID, 6, 0);

  lv_obj_t* msg = lv_label_create(uiRoot);
  int n = btScanCountSnapshot();
  if (connecting) lv_label_set_text(msg, STR_BT_CONNECTING);
  else if (n == 0) lv_label_set_text(msg, STR_BT_SCANNING);
  else lv_label_set_text(msg, STR_BT_TAP_DEVICE);
  lv_obj_set_pos(msg, 8, BT_PICK_LIST_Y - 16);

  int vis = btPickVisibleSlots();
  lv_obj_t* list = lv_list_create(uiRoot);
  if (!list) {
    uiTrace("drawBtPicker:list-fail");
    return;
  }
  lv_obj_set_size(list, SCR_W - 12, vis * BT_PICK_ITEM_H + 6);
  lv_obj_set_pos(list, 6, BT_PICK_LIST_Y);

  for (int row = 0; row < vis; row++) {
    int idx = btPickScroll + row;
    if (idx >= n) break;

    char nmCopy[BT_NAME_BUF];
    int rssi = -128;
    portENTER_CRITICAL(&btScanMux);
    strncpy(nmCopy, btScanList[idx].name, sizeof(nmCopy) - 1);
    nmCopy[sizeof(nmCopy) - 1] = '\0';
    rssi = btScanList[idx].rssi;
    portEXIT_CRITICAL(&btScanMux);

    char line[BT_NAME_BUF + 16];
    snprintf(line, sizeof(line), "%.28s  %d", nmCopy, rssi);
    lv_obj_t* b = lv_list_add_button(list, nullptr, line);
    lv_obj_add_event_cb(b, cbPickerItem, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
  }

  int fy = footerY();
  lv_obj_t* prev = lv_button_create(uiRoot);
  if (!prev) {
    uiTrace("drawBtPicker:prev-fail");
    return;
  }
  lv_obj_set_size(prev, 58, 24);
  lv_obj_set_pos(prev, 8, fy + 6);
  lv_obj_add_event_cb(prev, cbPickerPrev, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ptxt = lv_label_create(prev);
  lv_label_set_text(ptxt, STR_PREV_BTN);
  lv_obj_center(ptxt);

  lv_obj_t* next = lv_button_create(uiRoot);
  if (!next) {
    uiTrace("drawBtPicker:next-fail");
    return;
  }
  lv_obj_set_size(next, 58, 24);
  lv_obj_set_pos(next, 174, fy + 6);
  lv_obj_add_event_cb(next, cbPickerNext, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ntxt = lv_label_create(next);
  lv_label_set_text(ntxt, STR_NEXT_BTN);
  lv_obj_center(ntxt);

  int totalPages = max(1, (n + vis - 1) / vis);
  int page = (n == 0) ? 1 : min(totalPages, (btPickScroll / vis) + 1);
  char pageTxt[16];
  snprintf(pageTxt, sizeof(pageTxt), "%d/%d", page, totalPages);
  lv_obj_t* pg = lv_label_create(uiRoot);
  if (!pg) {
    uiTrace("drawBtPicker:page-fail");
    return;
  }
  lv_label_set_text(pg, pageTxt);
  lv_obj_align(pg, LV_ALIGN_BOTTOM_MID, 0, -12);

  uiTrace(connecting ? "drawBtPicker:connecting" : "drawBtPicker:done");
  lv_timer_handler();
}

#endif
