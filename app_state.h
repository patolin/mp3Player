#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include "bt_manager.h"

enum ScreenMode { SCREEN_BROWSER, SCREEN_PLAYER };

extern ScreenMode screenMode;
extern bool btConnected;
extern char btPeerDisplayName[BT_NAME_BUF];
extern unsigned long lastProgressUiMs;
extern int btPickScroll;

static const int SCR_W = 240;
static const int SCR_H = 320;

static const int PL_PROGRESS_TOP = 164;
static const int PL_BACK_BTN_X = 166;
static const int PL_BACK_BTN_Y = 4;
static const int PL_BACK_BTN_W = 70;
static const int PL_BACK_BTN_H = 30;
static const int PL_VOLUME_Y = 224;
static const int PL_TRANSPORT_Y = 262;

static const int browseHeaderH = 30;
static const int browseListY = 55;
static const int browseFooterH = 36;
static const int browseItemH = 24;
static const int BROWSE_PLAYER_BTN_X = 72;
static const int BROWSE_PLAYER_BTN_Y = 4;
static const int BROWSE_PLAYER_BTN_W = 72;
static const int BROWSE_PLAYER_BTN_H = 22;
static const int BROWSE_PATH_PLAY_BTN_X = 100;
static const int BROWSE_PATH_PLAY_BTN_Y = 36;
static const int BROWSE_PATH_PLAY_BTN_W = 40;
static const int BROWSE_PATH_PLAY_BTN_H = 18;

static const int BT_PICK_LIST_Y = 42;
static const int BT_PICK_ITEM_H = 24;

static const unsigned long TOUCH_DEBOUNCE_MS = 220;
static const unsigned long DISPLAY_IDLE_OFF_MS = 30000;
static const unsigned long BOOT_DEBOUNCE_MS = 45;
static const unsigned long RGB_PLAY_BLINK_MS = 450;
static const unsigned long RGB_SEARCH_BLINK_MS = 280;
static const unsigned long TOUCH_RELEASE_TIMEOUT_MS = 500;
static const unsigned long NO_MUSIC_DELAY_MS = 500;
static const unsigned long UI_PROGRESS_REFRESH_MS = 450;
static const unsigned long VISUALIZER_REFRESH_MS = 50;

#endif
