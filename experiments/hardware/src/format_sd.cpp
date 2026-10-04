// Destructive card utility, isolated from the normal recorder and diagnostics.
// Authorized by the owner on 2026-10-03 after confirming the card is empty.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sd_diskio.h"
#include "diskio.h"
#include "ff.h"
#include "esp_mac.h"

static void formatCard() {
  const uint8_t expectedMac[] = {0xe0, 0x72, 0xa1, 0xfb, 0x4c, 0x08};
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK || memcmp(mac, expectedMac, 6)) {
    Serial.println("HW FORMAT_REFUSED unexpected board MAC");
    return;
  }
  SPI.begin(7, 8, 9, 21);
  uint8_t drive = sdcard_init(21, &SPI, 4000000);
  if (drive == 0xff || disk_initialize(drive) & STA_NOINIT) {
    Serial.println("HW FORMAT_REFUSED card initialization failed");
    return;
  }
  uint8_t sector[512];
  bool valid = sd_read_raw(drive, sector, 0) && sector[510] == 0x55 && sector[511] == 0xaa;
  uint32_t lba = valid ? (uint32_t)sector[454] | ((uint32_t)sector[455] << 8) |
                         ((uint32_t)sector[456] << 16) | ((uint32_t)sector[457] << 24) : 0;
  if (!valid || lba != 2048 || !sd_read_raw(drive, sector, lba) || memcmp(sector + 3, "EXFAT   ", 8)) {
    Serial.println("HW FORMAT_REFUSED expected exFAT partition not found (already formatted or different card)");
    sdcard_uninit(drive);
    return;
  }
  char volume[3] = {(char)('0' + drive), ':', 0};
  const MKFS_PARM options = {FM_FAT32, 0, 0, 0, 0};
  uint8_t work[4096];
  Serial.printf("HW FORMAT_BEGIN FAT32 partition_lba=%lu\n", (unsigned long)lba);
  FRESULT result = f_mkfs(volume, &options, work, sizeof(work));
  Serial.printf("HW FORMAT_RESULT code=%u\n", (unsigned)result);
  if (result == FR_OK && sd_read_raw(drive, sector, 0)) {
    // f_mkfs may create a fresh partition boundary; inspect the resulting MBR.
    uint32_t formattedLba = (uint32_t)sector[454] | ((uint32_t)sector[455] << 8) |
                            ((uint32_t)sector[456] << 16) | ((uint32_t)sector[457] << 24);
    if (formattedLba < 0x10000000 && sd_read_raw(drive, sector, formattedLba))
      Serial.printf("HW FORMAT_VOLUME lba=%lu signature=%02x%02x filesystem=%.8s\n",
                    (unsigned long)formattedLba, sector[510], sector[511], sector + 82);
  }
  sdcard_uninit(drive);
  if (result != FR_OK) return;
  bool mounted = SD.begin(21, SPI, 4000000, "/sdcard", 10, false);
  Serial.printf("HW FORMAT_MOUNT success=%d bytes=%llu total=%llu\n", mounted,
                mounted ? SD.cardSize() : 0, mounted ? SD.totalBytes() : 0);
  if (mounted) SD.end();
  Serial.println("HW FORMAT_DONE");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("HW FORMAT_READY send FORMAT_SD_FAT32 within 60 seconds");
  uint32_t deadline = millis() + 60000;
  String command;
  while ((int32_t)(deadline - millis()) > 0) {
    while (Serial.available()) {
      char byte = Serial.read();
      if (byte == '\n') {
        command.trim();
        if (command == "FORMAT_SD_FAT32") { formatCard(); return; }
        command = "";
      } else if (command.length() < 40) command += byte;
    }
    delay(10);
  }
  Serial.println("HW FORMAT_REFUSED no command received; no writes performed");
}

void loop() { delay(1000); }
