/*
 * Inclusion des en-têtes du SDK Goodix depuis du C ou du C++.
 * Les en-têtes CMSIS/SDK doivent être vus avec une liaison C (sinon cmsis_gcc.h ne compile pas en C++).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "gr_includes.h"
#include "app_io.h"
#include "app_timer.h"
#include "app_pwr_mgmt.h"
#include "hal_flash.h"
#include "gr55xx_delay.h"
#include "gr55xx_ll_calendar.h"

#ifdef __cplusplus
}
#endif
