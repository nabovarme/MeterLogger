/* ld/rom1.ld - Slot 1 for rboot (0x82000 to 0xFC000) */
MEMORY
{
  dport0_0_seg : org = 0x3FF00000, len = 0x10
  dram0_0_seg  : org = 0x3FFE8000, len = 0x14000
  iram1_0_seg  : org = 0x40100000, len = 0x8000
  irom0_0_seg  : org = 0x40282010, len = 0x79FF0
}

INCLUDE "eagle.app.v6.common.ld"
