#pragma once

#include <esp_ota_ops.h>

// ---------------------------------------------------------------------------
// Dual-boot partition switching for InkStorm/CrossInk.
//
// The partition table has two equal app slots:
//   ota_0  (app0) → CrossInk reader firmware (default)
//   ota_1  (app1) → InkStorm weather firmware
//
// Calling bootToInkStorm() or bootToCrossInk() sets the active boot
// partition and resets the device. On the next boot the ESP32 ROM loads
// whichever partition was selected.
// ---------------------------------------------------------------------------

inline const esp_partition_t* getCurrentBootPartition() {
  return esp_ota_get_boot_partition();
}

inline bool isRunningFromPartition(const char* label) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running && strcmp(running->label, label) == 0;
}

inline bool isRunningCrossInk() {
  return isRunningFromPartition("app0");
}

inline bool isRunningInkStorm() {
  return isRunningFromPartition("app1");
}

// Set ota_1 (InkStorm) as the next boot partition, then restart.
inline void bootToInkStorm() {
  const esp_partition_t* target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                            nullptr);
  if (target) {
    esp_ota_set_boot_partition(target);
  }
  esp_restart();
}

// Set ota_0 (CrossInk) as the next boot partition, then restart.
inline void bootToCrossInk() {
  const esp_partition_t* target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                            nullptr);
  if (target) {
    esp_ota_set_boot_partition(target);
  }
  esp_restart();
}
