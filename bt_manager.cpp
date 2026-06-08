#include "bt_manager.h"

#include <string.h>
#include "freertos/portmacro.h"

struct BtScanEntry {
  char name[BT_NAME_BUF];
  uint8_t addr[ESP_BD_ADDR_LEN];
  int rssi;
};

static BtScanEntry btScanList[BT_SCAN_MAX];
static int btScanCount = 0;
static portMUX_TYPE btScanMux = portMUX_INITIALIZER_UNLOCKED;
static bool btUserHasChoice = false;
static uint8_t btChosenAddr[ESP_BD_ADDR_LEN];

static bool btAddrEquals(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, ESP_BD_ADDR_LEN) == 0;
}

bool btScanSsidCallback(const char* ssid, esp_bd_addr_t address, int rssi) {
  bool accept = false;
  portENTER_CRITICAL(&btScanMux);

  bool duplicate = false;
  for (int i = 0; i < btScanCount; i++) {
    if (btAddrEquals(btScanList[i].addr, address)) {
      duplicate = true;
      break;
    }
  }

  if (!duplicate && btScanCount < BT_SCAN_MAX) {
    BtScanEntry* e = &btScanList[btScanCount++];
    memset(e, 0, sizeof(*e));
    if (ssid && ssid[0]) {
      strncpy(e->name, ssid, sizeof(e->name) - 1);
    } else {
      strncpy(e->name, "(no name)", sizeof(e->name) - 1);
    }
    e->name[sizeof(e->name) - 1] = '\0';
    memcpy(e->addr, address, ESP_BD_ADDR_LEN);
    e->rssi = rssi;
  }

  if (btUserHasChoice && memcmp(address, btChosenAddr, ESP_BD_ADDR_LEN) == 0) {
    accept = true;
  }

  portEXIT_CRITICAL(&btScanMux);
  return accept;
}

void btResetPickerState() {
  portENTER_CRITICAL(&btScanMux);
  btUserHasChoice = false;
  portEXIT_CRITICAL(&btScanMux);
}

bool btHasUserChoice() {
  portENTER_CRITICAL(&btScanMux);
  bool chosen = btUserHasChoice;
  portEXIT_CRITICAL(&btScanMux);
  return chosen;
}

int btScanCountSnapshot() {
  portENTER_CRITICAL(&btScanMux);
  int n = btScanCount;
  portEXIT_CRITICAL(&btScanMux);
  return n;
}

bool btGetScanEntry(int idx, char* nameOut, int nameLen, int* rssiOut) {
  if (!nameOut || nameLen <= 0 || !rssiOut) return false;

  bool ok = false;
  portENTER_CRITICAL(&btScanMux);
  if (idx >= 0 && idx < btScanCount) {
    strncpy(nameOut, btScanList[idx].name, nameLen - 1);
    nameOut[nameLen - 1] = '\0';
    *rssiOut = btScanList[idx].rssi;
    ok = true;
  }
  portEXIT_CRITICAL(&btScanMux);
  return ok;
}

bool btChooseByIndex(int idx, char* pickedNameOut, int pickedNameLen) {
  if (!pickedNameOut || pickedNameLen <= 0) return false;

  bool ok = false;
  portENTER_CRITICAL(&btScanMux);
  if (idx >= 0 && idx < btScanCount) {
    memcpy(btChosenAddr, btScanList[idx].addr, ESP_BD_ADDR_LEN);
    strncpy(pickedNameOut, btScanList[idx].name, pickedNameLen - 1);
    pickedNameOut[pickedNameLen - 1] = '\0';
    btUserHasChoice = true;
    ok = true;
  }
  portEXIT_CRITICAL(&btScanMux);
  return ok;
}
