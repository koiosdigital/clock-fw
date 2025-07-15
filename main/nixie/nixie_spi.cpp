#include "nixie_spi.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static const char* TAG = "nixie_spi";

#ifdef CONFIG_BASE_CLOCK_TYPE_NIXIE

static spi_device_handle_t spi_hv = NULL;
static gpio_num_t lat_pin = (gpio_num_t)CONFIG_SHIFTREG_LATCH_PIN;

void nixie_spi_init(void) {
    ESP_LOGI(TAG, "Initializing SPI for Nixie shift register control");

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_SHIFTREG_SPI_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_SHIFTREG_SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512, // 64 bits for shift register data
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        return;
    }

    // Configure SPI device interface
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,         // No command phase
        .address_bits = 0,         // No address phase
        .dummy_bits = 0,           // No dummy bits
        .mode = CONFIG_SHIFTREG_SPI_MODE,
        .clock_speed_hz = 200000,  // 200 kHz for reliable operation
        .spics_io_num = -1,        // No hardware CS
        .queue_size = 2,
    };

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_hv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to SPI bus: %s", esp_err_to_name(err));
        return;
    }

    // Configure latch pin for 74HC595D if needed
    if (lat_pin != GPIO_NUM_NC) {
        gpio_config_t latch_config = {
            .pin_bit_mask = (1ULL << lat_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&latch_config);

        // Set initial latch state (inactive)
#if defined(CONFIG_SHIFTREG_LATCH_INVERSION) && CONFIG_SHIFTREG_LATCH_INVERSION
        gpio_set_level(lat_pin, 1); // Inverted: high = inactive
#else
        gpio_set_level(lat_pin, 0); // Normal: low = inactive
#endif
        ESP_LOGI(TAG, "Configured latch pin %d", lat_pin);
    }

    ESP_LOGI(TAG, "SPI initialization complete");
}

void nixie_spi_latch(void) {
    if (lat_pin != GPIO_NUM_NC) {
#if defined(CONFIG_SHIFTREG_LATCH_INVERSION) && CONFIG_SHIFTREG_LATCH_INVERSION
        gpio_set_level(lat_pin, 0); // Inverted: pulse low
        gpio_set_level(lat_pin, 1); // Return to high
#else
        gpio_set_level(lat_pin, 1); // Normal: pulse high
        gpio_set_level(lat_pin, 0); // Return to low
#endif
        ESP_LOGD(TAG, "Latch pulsed");
    }
}

void nixie_spi_transmit_bitstream(const uint8_t* bitstream, size_t length_bits) {
    if (spi_hv == NULL) {
        ESP_LOGE(TAG, "SPI not initialized");
        return;
    }

    if (bitstream == NULL || length_bits == 0) {
        ESP_LOGE(TAG, "Invalid bitstream parameters");
        return;
    }

    // Apply data inversion if configured
    size_t length_bytes = (length_bits + 7) / 8;
    uint8_t* tx_buffer = (uint8_t*)malloc(length_bytes);
    if (tx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate TX buffer");
        return;
    }

    memcpy(tx_buffer, bitstream, length_bytes);

#if defined(CONFIG_SHIFTREG_DATA_INVERSION) && CONFIG_SHIFTREG_DATA_INVERSION
    for (size_t i = 0; i < length_bytes; i++) {
        tx_buffer[i] = ~tx_buffer[i];
    }
#endif

    spi_transaction_t txn = {
        .length = length_bits,
        .tx_buffer = tx_buffer,
    };

    // Transmit data
    esp_err_t err = spi_device_transmit(spi_hv, &txn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmission failed: %s", esp_err_to_name(err));
    }

    free(tx_buffer);

    nixie_spi_latch(); // Pulse latch to apply data
}

void nixie_spi_deinit(void) {
    if (spi_hv != NULL) {
        esp_err_t err = spi_bus_remove_device(spi_hv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove SPI device: %s", esp_err_to_name(err));
        }
        spi_hv = NULL;
    }

    esp_err_t err = spi_bus_free(SPI2_HOST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to free SPI bus: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "SPI deinitialized");
}

#endif