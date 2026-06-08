#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include <Arduino.h>
#include "esp_gap_bt_api.h"

#define BT_SCAN_MAX 24
#define BT_NAME_BUF 40

bool btScanSsidCallback(const char* ssid, esp_bd_addr_t address, int rssi);

void btResetPickerState();
bool btHasUserChoice();

int btScanCountSnapshot();
bool btGetScanEntry(int idx, char* nameOut, int nameLen, int* rssiOut);
bool btChooseByIndex(int idx, char* pickedNameOut, int pickedNameLen);

#endif
