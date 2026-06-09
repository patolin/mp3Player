#ifndef UI_RENDERER_H
#define UI_RENDERER_H

// UI + visualization extracted from main sketch

// Visualizer sample buffers and frequency table are declared in the main sketch.

// ── Visualizer (per-band Goertzel; samples in visRing from ConsumeSample) ──
static float visGoertzelMag(const float* x, int N, float freqHz, float sampleRateHz) {
  const float PI_F = 3.14159265f;
  if (freqHz <= 0 || freqHz >= sampleRateHz * 0.48f) return 0;
  float k = (0.5f + ((float)N * freqHz) / sampleRateHz);
  int ki = (int)k;
  if (ki < 1) ki = 1;
  if (ki >= N) ki = N - 1;
  float omega = (2.0f * PI_F * (float)ki) / (float)N;
  float sine = sinf(omega);
  float cosine = cosf(omega);
  float coeff = 2.0f * cosine;
  float q0 = 0, q1 = 0, q2 = 0;
  for (int i = 0; i < N; i++) {
    q0 = coeff * q1 - q2 + x[i];
    q2 = q1;
    q1 = q0;
  }
  float real = q1 - q2 * cosine;
  float imag = q2 * sine;
  return sqrtf(real * real + imag * imag) / (float)N;
}

static float visSampleRateHz() {
  if (currentType == AUDIO_WAV && cachedWavRateHz > 0) return (float)cachedWavRateHz;
  return 44100.0f;
}

static void computeVisBands() {
  if (visWritePos < (uint32_t)VIS_N) return;
  float wf[VIS_N];
  uint32_t end = visWritePos;
  for (int i = 0; i < VIS_N; i++) {
    uint32_t idx = (end - VIS_N + i) & (VIS_BUF_LEN - 1);
    float s = (float)visRing[idx];
    float w = 0.54f - 0.46f * cosf(2.0f * 3.14159265f * i / (float)(VIS_N - 1));
    wf[i] = s * w;
  }
  float sr = visSampleRateHz();
  float raw[NUM_VIS_BARS];
  for (int b = 0; b < NUM_VIS_BARS; b++) {
    float fh = VIS_FREQ_HZ[b];
    if (fh >= sr * 0.45f) fh = sr * 0.45f;
    raw[b] = visGoertzelMag(wf, VIS_N, fh, sr);
  }

  for (int b = 0; b < NUM_VIS_BARS; b++) {
    float treble = 0.55f + (float)b * 0.32f;
    float scaled = raw[b] * treble;
    visBandEnv[b] = visBandEnv[b] * 0.90f + scaled * 0.10f;
    if (visBandEnv[b] < 1e-7f) visBandEnv[b] = 1e-7f;
    float t = scaled / (visBandEnv[b] * 1.35f + 1e-6f);
    t = powf(t, 0.55f);
    if (t > 1.0f) t = 1.0f;
    if (t > visBandVal[b]) visBandVal[b] = visBandVal[b] * 0.28f + t * 0.72f;
    else visBandVal[b] = visBandVal[b] * 0.72f + t * 0.28f;
  }
}

static const int VIS_PX = 8;
static const int VIS_PY = PL_CASSETTE_TOP;
static const int VIS_PW = 224;
static const int VIS_PH = 108;
static const int VIS_IX = VIS_PX + 10;
static const int VIS_IY = VIS_PY + 22;
static const int VIS_IW = VIS_PW - 20;
static const int VIS_IH = VIS_PH - 34;

static uint16_t visBarColor(int b, float hNorm) {
  float t = (float)b / (float)((NUM_VIS_BARS > 1) ? (NUM_VIS_BARS - 1) : 1);
  uint8_t r = (uint8_t)(20.f + t * 120.f + hNorm * 80.f);
  uint8_t g = (uint8_t)(200.f - t * 140.f + hNorm * 40.f);
  uint8_t bl = (uint8_t)(160.f + (1.0f - t) * 60.f + hNorm * 30.f);
  return tft.color565(r, g, bl);
}

static void drawVisualizerBarsContent() {
  const int gap = 2;
  int barW = (VIS_IW - gap * (NUM_VIS_BARS - 1) - 4) / NUM_VIS_BARS;
  if (barW < 2) barW = 2;
  const int maxH = VIS_IH - 8;
  const int baseY = VIS_IY + VIS_IH - 4;
  for (int b = 0; b < NUM_VIS_BARS; b++) {
    int x = VIS_IX + 2 + b * (barW + gap);
    float h = visBandVal[b];
    int hh = (int)(h * (float)maxH);
    if (hh > maxH) hh = maxH;
    if (hh < 1) continue;
    tft.fillRoundRect(x, baseY - hh, barW, hh, 2, visBarColor(b, h));
  }
}

static void redrawVisualizerBarsOnly() {
  tft.fillRoundRect(VIS_IX, VIS_IY, VIS_IW, VIS_IH, 6, tft.color565(4, 4, 8));
  drawVisualizerBarsContent();
}

static void drawVisualizerPanel() {
  tft.fillRoundRect(VIS_PX, VIS_PY, VIS_PW, VIS_PH, 11, colTapeEdge());
  tft.fillRoundRect(VIS_PX + 4, VIS_PY + 4, VIS_PW - 8, VIS_PH - 8, 8, colTapeBody());
  tft.fillRoundRect(VIS_PX + 36, VIS_PY + 10, VIS_PW - 72, 12, 4, tft.color565(28, 28, 32));
  tft.drawRoundRect(VIS_PX + 36, VIS_PY + 10, VIS_PW - 72, 12, 4, tft.color565(50, 50, 56));
  tft.setTextSize(1);
  tft.setTextColor(colInfoCyan(), tft.color565(28, 28, 32));
  tft.setCursor(VIS_PX + 48, VIS_PY + 14);
  tft.print(STR_SPECTRUM);
  redrawVisualizerBarsOnly();
}

static unsigned long lastVisAnimMs = 0;

static void updateVisualizerAnimation() {
  if (screenMode != SCREEN_PLAYER) return;
  unsigned long now = millis();
  if (now - lastVisAnimMs < 50) return;
  lastVisAnimMs = now;

  if (playerState != STATE_PLAYING) {
    for (int i = 0; i < NUM_VIS_BARS; i++) {
      visBandVal[i] *= 0.82f;
      visBandEnv[i] *= 0.88f;
    }
  } else {
    computeVisBands();
  }
  redrawVisualizerBarsOnly();
}

/** Single centered line (size 1); truncates with ".." if wider than maxPx. */
static void drawTitleCentered(int y, int maxPx, const char* s) {
  char buf[52];
  strncpy(buf, s ? s : "", sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  const int charW = 6;
  int maxChars = maxPx / charW - 2;
  if (maxChars < 6) maxChars = 6;
  int len = (int)strlen(buf);
  if (len > maxChars) {
    buf[maxChars - 2] = '\0';
    strcat(buf, "..");
  }
  tft.setTextSize(1);
  int w = (int)strlen(buf) * charW;
  tft.setCursor((SCR_W / 2) - w / 2, y);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.print(buf);
}

// ── Render ─────────────────────────────────────────────────
static void drawBrowser() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCR_W, browseHeaderH, COL_BTN);

  tft.setTextColor(COL_TEXT, COL_BTN);
  tft.setTextSize(1);
  tft.setCursor(10, 4);
  if (browseLevel == BROWSE_ALBUMS)
    tft.print(STR_ALBUMS);
  else
    tft.print(STR_BACK_LIST);

  uint16_t btCol = btConnected ? COL_ACCENT : TFT_RED;
  tft.setTextSize(1);
  tft.fillRoundRect(150, 7, 26, 14, 4, btCol);
  tft.setTextColor(COL_BG, btCol);
  tft.setCursor(157, 10);
  tft.print(STR_BT_LABEL);

  tft.setTextColor(btCol, COL_BTN);
  tft.setCursor(180, 10);
  if (btConnected && btPeerDisplayName[0]) {
    char bname[12];
    strncpy(bname, btPeerDisplayName, sizeof(bname) - 1);
    bname[sizeof(bname) - 1] = '\0';
    tft.print(bname);
  } else if (btConnected) {
    tft.print(STR_BT_CONNECTED);
  } else {
    tft.print(STR_BT_DISCONNECTED);
  }

  if (trackCount > 0) {
    int bx = BROWSE_PLAYER_BTN_X, by = BROWSE_PLAYER_BTN_Y, bw = BROWSE_PLAYER_BTN_W, bh = BROWSE_PLAYER_BTN_H;
    tft.fillRoundRect(bx, by, bw, bh, 4, COL_BTN);
    uint16_t acc = colInfoCyan();
    int cx = bx + 12;
    int cy = by + bh / 2;
    tft.fillTriangle(cx - 4, cy - 5, cx - 4, cy + 5, cx + 6, cy, acc);
    tft.setTextColor(COL_TEXT, COL_BTN);
    tft.setTextSize(1);
    tft.setCursor(bx + 22, by + 8);
    tft.print(STR_PLAYER_BTN);
  }

  if (trackCount > 0) {
    int bx = BROWSE_PATH_PLAY_BTN_X, by = BROWSE_PATH_PLAY_BTN_Y;
    int bw = BROWSE_PATH_PLAY_BTN_W, bh = BROWSE_PATH_PLAY_BTN_H;
    tft.fillRoundRect(bx, by, bw, bh, 4, COL_BTN);
    uint16_t acc = colInfoCyan();
    int cx = bx + bw / 2;
    int cy = by + bh / 2;
    tft.fillTriangle(cx - 5, cy - 6, cx - 5, cy + 6, cx + 8, cy, acc);
  }

  int fY = footerY();
  int vis = visibleSlots();

  tft.fillRoundRect(8, fY + 6, 58, 24, 5, COL_BTN);
  tft.fillRoundRect(174, fY + 6, 58, 24, 5, COL_BTN);
  tft.setTextColor(COL_TEXT, COL_BTN);
  tft.setCursor(20, fY + 15);
  tft.print(STR_PREV_BTN);
  tft.setCursor(186, fY + 15);
  tft.print(STR_NEXT_BTN);

  int itemCount = (browseLevel == BROWSE_ALBUMS) ? albumCount : browseTrackCount;
  int scroll = (browseLevel == BROWSE_ALBUMS) ? albumScroll : browseTrackScroll;
  int totalPages = (itemCount + vis - 1) / vis;
  if (totalPages < 1) totalPages = 1;
  int page = (scroll / vis) + 1;
  if (page > totalPages) page = totalPages;
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(104, fY + 15);
  tft.printf("%d/%d", page, totalPages);

  for (int i = 0; i < vis; i++) {
    int idx = scroll + i;
    if (idx >= itemCount) break;
    int y = browseListY + i * browseItemH;
    tft.fillRoundRect(6, y + 1, SCR_W - 12, browseItemH - 3, 5, COL_DIR);
    tft.setTextSize(1);
    tft.setCursor(12, y + 7);
    if (browseLevel == BROWSE_ALBUMS) {
      tft.setTextColor(COL_TEXT, COL_DIR);
      tft.print(albums[idx]);
    } else {
      int pi = browseTrackIndices[idx];
      char disp[40];
      getDisplayName(playlist[pi], disp, sizeof(disp));
      bool on = (pi == currentTrack && (playerState == STATE_PLAYING || playerState == STATE_PAUSED));
      tft.setTextColor(on ? colInfoCyan() : COL_TEXT, COL_DIR);
      tft.print(disp);
    }
    if ((i & 1) == 0) audioPumpPlayingMax(48);
  }
  audioPumpPlayingMax(384);
}

// ── Splash / startup screen ─────────────────────────────────
static const char* const SPLASH_RAW_PATH = "/guara565.raw";
static const int SP_RAW_W = 200;
static const int SP_RAW_H = 218;

static inline void splashDrawPlus(int x, int y, uint16_t c) {
  tft.drawFastVLine(x, y - 2, 5, c);
  tft.drawFastHLine(x - 2, y, 5, c);
}

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

static void drawGuaraLogoProcedural(int ix, int iy, int iw, int ih) {
  tft.fillRect(ix, iy, iw, ih, COL_BG);

  const int cx = ix + iw / 2;
  uint16_t furHi = tft.color565(210, 210, 218);
  uint16_t furMid = tft.color565(165, 165, 175);
  uint16_t furLo = tft.color565(118, 118, 128);
  uint16_t sunHi = tft.color565(62, 62, 70);
  uint16_t sunLo = tft.color565(44, 44, 52);
  uint16_t accPurp = tft.color565(170, 80, 240);

  {
    const int syc = iy + 72;
    const int R = 58;
    for (int dy = -R; dy <= 0; dy++) {
      int rr = R * R - dy * dy;
      if (rr < 0) continue;
      int ww = (int)sqrtf((float)rr);
      uint16_t c = ((dy + R) & 2) ? sunHi : sunLo;
      tft.drawFastHLine(cx - ww, syc + dy, 2 * ww + 1, c);
    }
  }

  tft.fillTriangle(cx - 54, iy + 118, cx - 26, iy + 52, cx - 10, iy + 108, furHi);
  tft.fillTriangle(cx + 54, iy + 118, cx + 26, iy + 52, cx + 10, iy + 108, furHi);
  tft.fillTriangle(cx - 50, iy + 122, cx + 50, iy + 122, cx, iy + 198, furHi);

  tft.fillTriangle(cx - 46, iy + 128, cx - 8, iy + 128, cx - 28, iy + 188, furMid);
  tft.fillTriangle(cx + 46, iy + 128, cx + 8, iy + 128, cx + 28, iy + 188, furMid);

  tft.fillCircle(cx - 22, iy + 118, 10, furLo);
  tft.fillCircle(cx + 22, iy + 118, 10, furLo);

  tft.fillTriangle(cx - 18, iy + 138, cx + 18, iy + 138, cx, iy + 186, furMid);
  tft.fillTriangle(cx - 12, iy + 144, cx + 12, iy + 144, cx, iy + 178, furHi);

  tft.fillCircle(cx - 22, iy + 116, 4, COL_TEXT);
  tft.fillCircle(cx + 22, iy + 116, 4, COL_TEXT);
  tft.fillCircle(cx - 23, iy + 115, 2, COL_BG);
  tft.fillCircle(cx + 21, iy + 115, 2, COL_BG);

  tft.fillTriangle(cx - 5, iy + 158, cx + 5, iy + 158, cx, iy + 168, COL_BG);

  for (int k = 0; k < 4; k++) {
    tft.drawFastHLine(cx - 34 - k * 3, iy + 152 + k, 10, furLo);
    tft.drawFastHLine(cx + 24 + k * 3, iy + 152 + k, 10, furLo);
  }

  uint16_t xcol = tft.color565(90, 90, 98);
  splashDrawPlus(ix + 8, iy + 56, xcol);
  splashDrawPlus(ix + 14, iy + 92, COL_DIM);
  splashDrawPlus(ix + iw - 9, iy + 64, xcol);
  splashDrawPlus(ix + iw - 15, iy + 100, COL_DIM);
  splashDrawPlus(cx - 70, iy + 40, COL_DIM);
  splashDrawPlus(cx + 70, iy + 44, COL_DIM);

  {
    const int bx = ix + 14, by = iy + 168, bw = iw - 28, bh = 44;
    tft.fillTriangle(bx, by + bh, bx + 8, by, bx + bw - 8, by, furHi);
    tft.fillTriangle(bx, by + bh, bx + bw - 8, by, bx + bw, by + bh, furHi);
    tft.drawTriangle(bx, by + bh, bx + 8, by, bx + bw - 8, by, accPurp);
    tft.drawTriangle(bx, by + bh, bx + bw - 8, by, bx + bw, by + bh, accPurp);
    tft.setTextColor(COL_BG, furHi);
    tft.setTextSize(2);
    const char* g = STR_GUARA;
    int gw = (int)strlen(g) * 12;
    tft.setCursor(cx - gw / 2, by + 14);
    tft.print(g);
  }

  {
    const int bx2 = ix + 44, by2 = iy + 214, bw2 = iw - 88, bh2 = 28;
    tft.fillRoundRect(bx2, by2, bw2, bh2, 5, furHi);
    tft.drawRoundRect(bx2, by2, bw2, bh2, 5, accPurp);
    tft.setTextColor(COL_BG, furHi);
    tft.setTextSize(2);
    const char* c = STR_CREW;
    int cw = (int)strlen(c) * 12;
    tft.setCursor(cx - cw / 2, by2 + 8);
    tft.print(c);
  }
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
  uint16_t frameOut = tft.color565(48, 48, 56);
  uint16_t frameIn = tft.color565(28, 28, 34);
  tft.drawRoundRect(lx, ly, lw, lh, 10, frameOut);
  tft.drawRoundRect(lx + 2, ly + 2, lw - 4, lh - 4, 8, frameIn);
  tft.drawFastHLine(lx + 16, ly + 6, lw - 32, tft.color565(170, 80, 240));

  const int innerX = lx + 6;
  const int innerY = ly + 6;
  const int innerW = lw - 12;
  const int innerH = lh - 12;

  const int rawX = innerX + (innerW - SP_RAW_W) / 2;
  const int rawY = innerY + (innerH - SP_RAW_H) / 2;

  if (!tryDrawSplashFromSD(rawX, rawY))
    drawGuaraLogoProcedural(innerX, innerY, innerW, innerH);

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(40, 306);
  tft.print(STR_PREPARING_BT);
}

/** Progress bar, times, and technical line (periodic refresh from loop). */
static void drawPlayerProgressArea() {
  if (screenMode != SCREEN_PLAYER) return;

  const int y0 = PL_PROGRESS_TOP;
  tft.fillRect(0, y0, SCR_W, PL_PROGRESS_H, COL_BG);

  uint32_t el = elapsedPlaybackSec();
  char tEl[16], tTot[16];
  formatTimeHMS(tEl, sizeof(tEl), el);
  if (cachedDurationSec > 0) formatTimeHMS(tTot, sizeof(tTot), cachedDurationSec);
  else {
    strncpy(tTot, STR_TIME_UNKNOWN, sizeof(tTot) - 1);
    tTot[sizeof(tTot) - 1] = '\0';
  }

  const int bx = 10, by = y0 + 2, bw = SCR_W - 20, bh = 6;
  tft.drawRoundRect(bx, by, bw, bh + 2, 2, COL_DIM);
  tft.fillRect(bx + 1, by + 1, bw - 2, bh, tft.color565(22, 22, 26));
  if (cachedDurationSec > 0) {
    uint32_t fw = (uint32_t)(((uint64_t)el * (uint64_t)(bw - 4)) / (uint64_t)cachedDurationSec);
    if (fw > (uint32_t)(bw - 4)) fw = (uint32_t)(bw - 4);
    if (fw > 0) tft.fillRect(bx + 2, by + 2, (int)fw, bh - 2, colReelRed());
  }

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  if (playerState == STATE_PLAYING) {
    tft.fillTriangle(10, y0 + 18, 10, y0 + 24, 16, y0 + 21, COL_TEXT);
  } else {
    tft.fillRect(10, y0 + 17, 3, 8, COL_TEXT);
    tft.fillRect(14, y0 + 17, 3, 8, COL_TEXT);
  }
  tft.setCursor(22, y0 + 16);
  tft.print(tEl);
  tft.setCursor(SCR_W - 70, y0 + 16);
  tft.print(tTot);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(10, y0 + 28);
  tft.print(currentAlbumFolder[0] ? currentAlbumFolder : "-");

  tft.setTextColor(colInfoCyan(), COL_BG);
  tft.setCursor(10, y0 + 40);
  if (currentType == AUDIO_WAV && cachedWavRateHz > 0)
    tft.printf(STR_AUDIO_WAV_FMT, (unsigned long)cachedWavRateHz);
  else if (currentType == AUDIO_MP3)
    tft.print(STR_AUDIO_MP3);
  else
    tft.print(STR_AUDIO_UNKNOWN);
}

static void drawVolumeControls() {
  const int y = PL_VOLUME_Y;
  tft.fillRoundRect(10, y, 56, 30, 6, COL_BTN);
  tft.fillRoundRect(74, y, 92, 30, 6, COL_BTN);
  tft.fillRoundRect(174, y, 56, 30, 6, COL_BTN);

  uint16_t fg = COL_TEXT;
  const int cy = y + 15;

  {
    const int cx = 10 + 56 / 2;
    tft.fillRoundRect(cx - 10, cy - 3, 20, 6, 2, fg);
  }

  char vbuf[10];
  snprintf(vbuf, sizeof(vbuf), "%d%%", volumePercent);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BTN);
  int tw = (int)strlen(vbuf) * 6;
  tft.setCursor((SCR_W / 2) - (tw / 2), y + 11);
  tft.print(vbuf);

  {
    const int cx = 174 + 56 / 2;
    tft.fillRoundRect(cx - 2, cy - 10, 4, 20, 1, fg);
    tft.fillRoundRect(cx - 10, cy - 2, 20, 4, 2, fg);
  }
}

static void drawPlayerListBackIcon() {
  int bx = PL_BACK_BTN_X, by = PL_BACK_BTN_Y, bw = PL_BACK_BTN_W, bh = PL_BACK_BTN_H;
  tft.fillRoundRect(bx, by, bw, bh, 4, tft.color565(36, 36, 42));
  uint16_t fg = colInfoCyan();
  int cy = by + bh / 2;
  const int lineW = 30;
  const int rowGap = 7;
  int lx0 = bx + 14;
  for (int row = 0; row < 3; row++) {
    int ly = cy - 8 + row * rowGap;
    tft.fillRoundRect(lx0, ly + 1, 4, 4, 1, fg);
    tft.fillRoundRect(lx0 + 8, ly + 2, lineW, 2, 1, fg);
  }
}

static void drawPlayer() {
  tft.fillScreen(COL_BG);

  tft.fillRect(0, 0, SCR_W, PL_HEADER_H, colTopBarBg());
  tft.drawFastHLine(0, PL_HEADER_H - 1, SCR_W, tft.color565(40, 40, 48));

  uint16_t noteCol = tft.color565(170, 80, 240);
  tft.fillCircle(9, 17, 3, noteCol);
  tft.fillCircle(17, 15, 3, noteCol);
  tft.fillRect(11, 8, 3, 11, noteCol);
  tft.fillRect(19, 6, 3, 11, noteCol);
  tft.drawFastHLine(12, 6, 8, noteCol);

  tft.setTextColor(COL_TEXT, colTopBarBg());
  tft.setTextSize(2);
  tft.setCursor(22, 8);
  if (trackCount > 0) tft.printf("%d/%d", currentTrack + 1, trackCount);
  else tft.print(STR_TRACK_COUNTER_EMPTY);

  {
    char abuf[26];
    const char* src = currentAlbumFolder[0] ? currentAlbumFolder : "-";
    strncpy(abuf, src, sizeof(abuf) - 1);
    abuf[sizeof(abuf) - 1] = '\0';
    int mc = (int)sizeof(abuf) - 4;
    if ((int)strlen(abuf) > mc) {
      abuf[mc] = '\0';
      strcat(abuf, "..");
    }
    if ((int)strlen(abuf) > 14) {
      abuf[14] = '\0';
      strcat(abuf, "..");
    }
    tft.setTextSize(1);
    tft.setCursor(22, 26);
    tft.print(abuf);
  }

  uint16_t btCol = a2dp.is_connected() ? tft.color565(60, 200, 90) : tft.color565(200, 60, 60);
  tft.fillRoundRect(128, 6, 32, 16, 3, tft.color565(30, 30, 34));
  tft.setTextSize(1);
  tft.setTextColor(btCol, colTopBarBg());
  tft.setCursor(134, 10);
  tft.print(STR_BT_LABEL);

  drawPlayerListBackIcon();
  audioPumpPlayingMax(160);

  char title[64];
  if (trackCount > 0)
    getDisplayName(playlist[currentTrack], title, sizeof(title));
  else
    strncpy(title, STR_NO_TRACK, sizeof(title) - 1);
  title[sizeof(title) - 1] = '\0';
  tft.setTextSize(1);
  drawTitleCentered(PL_TITLE_Y, 220, title);

  drawVisualizerPanel();
  audioPumpPlayingMax(220);

  drawPlayerProgressArea();
  drawVolumeControls();
  lastProgressUiMs = millis();
  audioPumpPlayingMax(220);

  const int y = PL_TRANSPORT_Y;
  tft.fillRoundRect(10, y, 56, 42, 7, COL_BTN);
  tft.fillRoundRect(74, y, 92, 42, 7, COL_BTN);
  tft.fillRoundRect(174, y, 56, 42, 7, COL_BTN);

  uint16_t fg = COL_TEXT;
  int cy = y + 42 / 2;

  {
    int cxPrev = 10 + 56 / 2;
    tft.fillRect(cxPrev - 18, cy - 14, 3, 28, fg);
    tft.fillTriangle(cxPrev - 2, cy, cxPrev - 2 + 11, cy - 11, cxPrev - 2 + 11, cy + 11, fg);
    tft.fillTriangle(cxPrev + 8, cy, cxPrev + 8 + 11, cy - 11, cxPrev + 8 + 11, cy + 11, fg);
  }
  {
    int cxPlay = 74 + 92 / 2;
    if (playerState == STATE_PLAYING) {
      int wBar = 7;
      int gap = 5;
      tft.fillRect(cxPlay - gap - wBar, cy - 15, wBar, 30, fg);
      tft.fillRect(cxPlay + gap, cy - 15, wBar, 30, fg);
    } else {
      int size = 22;
      tft.fillTriangle(cxPlay - size / 3, cy - size / 2,
                       cxPlay - size / 3, cy + size / 2,
                       cxPlay + size / 2, cy, fg);
    }
  }
  {
    int cxNext = 174 + 56 / 2;
    tft.fillTriangle(cxNext - 10 - 11, cy - 11, cxNext - 10 - 11, cy + 11, cxNext - 10, cy, fg);
    tft.fillTriangle(cxNext - 2 - 11, cy - 11, cxNext - 2 - 11, cy + 11, cxNext - 2, cy, fg);
    tft.fillRect(cxNext + 7, cy - 14, 3, 28, fg);
  }
  audioPumpPlayingMax(320);
}

// ── Bluetooth device picker (startup) ──────────────────────
static const int BT_PICK_HEADER_H = 30;
static const int BT_PICK_LIST_Y    = 42;
static const int BT_PICK_ITEM_H    = 24;
static int       btPickScroll      = 0;

static inline int btPickVisibleSlots() {
  int vis = (footerY() - BT_PICK_LIST_Y) / BT_PICK_ITEM_H;
  return max(1, vis);
}

/** @param connecting  Show "Connecting..." state */
static void drawBluetoothPicker(bool connecting) {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCR_W, BT_PICK_HEADER_H, COL_BTN);
  tft.setTextColor(COL_TEXT, COL_BTN);
  tft.setTextSize(1);
  tft.setCursor(6, 4);
  tft.print(STR_BT_PICKER_HEADER);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(8, BT_PICK_LIST_Y - 14);
  int nPre = btScanCountSnapshot();
  if (connecting)
    tft.print(STR_BT_CONNECTING);
  else if (nPre == 0)
    tft.print(STR_BT_SCANNING);
  else
    tft.print(STR_BT_TAP_DEVICE);

  int vis = btPickVisibleSlots();
  int n = nPre;
  int fY = footerY();

  for (int row = 0; row < vis; row++) {
    int idx = btPickScroll + row;
    int y = BT_PICK_LIST_Y + row * BT_PICK_ITEM_H;
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

    tft.fillRoundRect(6, y + 1, SCR_W - 12, BT_PICK_ITEM_H - 3, 5, COL_BTN);
    tft.setTextColor(COL_TEXT, COL_BTN);
    tft.setTextSize(1);
    tft.setCursor(10, y + 7);
    tft.print(line);
  }

  tft.fillRoundRect(8, fY + 6, 58, 24, 5, COL_BTN);
  tft.fillRoundRect(174, fY + 6, 58, 24, 5, COL_BTN);
  tft.setTextColor(COL_TEXT, COL_BTN);
  tft.setCursor(20, fY + 15);
  tft.print(STR_PREV_BTN);
  tft.setCursor(186, fY + 15);
  tft.print(STR_NEXT_BTN);

  int totalPages = max(1, (n + vis - 1) / vis);
  int page = (n == 0) ? 1 : min(totalPages, (btPickScroll / vis) + 1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(104, fY + 15);
  tft.printf("%d/%d", page, totalPages);
}

#endif
