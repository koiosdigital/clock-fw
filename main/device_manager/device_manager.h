#pragma once

#include "levitree_cpcd.pb-c.h"

#define MCU_ID_LEN 32

typedef struct DeviceList_t {
    LevitreeCpcd__AttachedDevice* device;
    DeviceList_t* next;
} DeviceList_t;

LevitreeCpcd__AttachedDevice get_attached_device(uint8_t* mcu_id);
DeviceList_t* get_attached_device_list();
void discover_attached_devices();

uint8_t* get_self_mcu_id();
