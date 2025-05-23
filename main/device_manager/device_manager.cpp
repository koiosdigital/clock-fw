#include "device_manager.h"

#include <nvs_flash.h>
#include <esp_random.h>
#include <string.h>

DeviceList_t device_list = {
    .device = nullptr,
    .next = nullptr
};

DeviceList_t tmp_device_list = {
    .device = nullptr,
    .next = nullptr
};

LevitreeCpcd__AttachedDevice get_attached_device(uint8_t* mcu_id) {
    LevitreeCpcd__AttachedDevice device = LEVITREE_CPCD__ATTACHED_DEVICE__INIT;
    device.device_type = LEVITREE_CPCD__DEVICE_TYPE__DEVICE_MOTHERBOARD;
    device.mcu_id.data = mcu_id;
    device.mcu_id.len = MCU_ID_LEN;

    // Check if the device is already in the list
    DeviceList_t* current = &device_list;
    while (current != nullptr) {
        if (current->device != nullptr && memcmp(current->device->mcu_id.data, mcu_id, MCU_ID_LEN) == 0) {
            return *current->device;
        }
        current = current->next;
    }

    return device;
}

DeviceList_t* get_attached_device_list() {
    return &device_list;
}

void discover_attached_devices() {
    //TODO: Implement the discovery logic
}

uint8_t* get_self_mcu_id() {
    static uint8_t mcu_id[MCU_ID_LEN];
    static bool initialized = false;

    if (!initialized) {
        // Try to load from NVS
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            size_t required_size = sizeof(mcu_id);
            err = nvs_get_blob(nvs_handle, "mcu_id", mcu_id, &required_size);
            if (err != ESP_OK || required_size != sizeof(mcu_id)) {
                // Not found or wrong size, generate random
                esp_fill_random(mcu_id, sizeof(mcu_id));
                nvs_set_blob(nvs_handle, "mcu_id", mcu_id, sizeof(mcu_id));
                nvs_commit(nvs_handle);
            }
            nvs_close(nvs_handle);
        }
        else {
            // NVS open failed, just generate random
            esp_fill_random(mcu_id, sizeof(mcu_id));
        }
        initialized = true;
    }

    return mcu_id;
}