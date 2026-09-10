# Environment Notes

## 切换 n16r8 和 n8r2 板型（menuconfig）

说明：
- 这里的 CSV 指分区表文件，不是 CVS。
- PSRAM 模式不是固定 OCTAL，要跟板型匹配。

至少改 3 处（第 2 处包含两个联动项）：

1. `Xiaozhi Assistant`
- 切换板型：
  - n16r8 -> `ESP32-S3-WROOM-1-N16R8`
  - n8r2 -> `ESP32-S3-WROOM-1-N8R2`

2. `Serial Flasher Config` + `Partition Table`
- Flash Size 和分区表 CSV 必须配套：
  - n16r8 -> `Flash Size: 16MB` + `partitions/v2/16m.csv`
  - n8r2 -> `Flash Size: 8MB` + `partitions/v2/8m.csv`

3. `Component config -> ESP PSRAM`
- PSRAM Mode：
  - n16r8 -> `OCTAL`
  - n8r2 -> `QUAD`

## 快速自检（看 sdkconfig）

切完后建议确认这几项是否成对出现：
- n16r8:
  - `CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N16R8=y`
  - `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"`
  - `CONFIG_SPIRAM_MODE_OCT=y`
- n8r2:
  - `CONFIG_BOARD_TYPE_ESP32_S3_WROOM_1_N8R2=y`
  - `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/8m.csv"`
  - `CONFIG_SPIRAM_MODE_QUAD=y`

## 备注

- 切板后先保存 menuconfig 再继续后续操作。
- 若出现 partition table 相关报错，先检查 Flash Size 与 CSV 是否同档位匹配。
