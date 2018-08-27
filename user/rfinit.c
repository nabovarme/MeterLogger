/******************************************************************************
 * Copyright 2016 Vowstar
 *
 * FileName: init.c
 *
 * Description: System and user APP initialization.
 *
 * Modification history:
 *     2016/03/24, v1.0 create this file.
*******************************************************************************/

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

void __attribute__((weak))
user_rf_pre_init(void) {
  // Warning: IF YOU DON'T KNOW WHAT YOU ARE DOING, DON'T TOUCH THESE CODE

  // Control RF_CAL by esp_init_data_default.bin(0~127byte) 108 byte when wakeup
  // Will low current
  // system_phy_set_rfoption(0)；

  // Process RF_CAL when wakeup.
  // Will high current
  system_phy_set_rfoption(1);

  // Set Wi-Fi Tx Power, Unit: 0.25dBm, Range: [0, 82]
  system_phy_set_max_tpw(82);
  
  system_phy_freq_trace_enable(1);
}
