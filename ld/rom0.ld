/* ld/rom0.ld - Slot 0 for rboot (0x02000) */
MEMORY
{
  dport0_0_seg : org = 0x3FF00000, len = 0x10
  dram0_0_seg  : org = 0x3FFE8000, len = 0x14000
  iram1_0_seg  : org = 0x40100000, len = 0x8000
  irom0_0_seg  : org = 0x40202010, len = 0x7C000
}

INCLUDE "eagle.app.v6.common.ld"
