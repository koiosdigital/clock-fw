#include "clock.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "led.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include "internet_time.h"
#include <esp_event.h>

#include <esp_wifi.h>
#include "wifi_provisioning/manager.h"
#include "protocomm_ble.h"


#define NUM_PIXELS 256

const char* letters =
"ITLISOTWENTYRONETWOETENMTHIRTEENFIVEMELEVENIFOURTHREEPNINETEENSUFOURTEENMIDNIGHTSIXTEENDEIGHTEENSEVENTEENOTWELVEHALFELQUARTEROTOPASTRONESATW"
"OSIXTWELVETFOURAFIVESEVENMEIGHTENINETENTTHREECELEVENINOTHENAFTERNOONMORNINGSATENIGHTEVENINGCANDTCOLDCOOLETWARMURAHOT";


LEDConfig_t wordclock_led_config = {
    .pin = (gpio_num_t)4,  // Example GPIO pin
    .count = NUM_PIXELS,
    .is_rgbw = false,   // Assuming RGB, not RGBW
};

uint8_t bits[NUM_PIXELS] = { 0 };
char wordclock_words_buffer[128] = { 0 };

bool add_word_to_buffer(const char* word)
{
    size_t buffer_len = strlen(wordclock_words_buffer);
    size_t word_len = strlen(word);
    // Add 1 for space if buffer is not empty, plus 1 for null terminator
    if (buffer_len + (buffer_len > 0 ? 1 : 0) + word_len >= sizeof(wordclock_words_buffer)) {
        return false;
    }
    if (buffer_len > 0) {
        strcat(wordclock_words_buffer, " ");
    }
    strcat(wordclock_words_buffer, word);
    return true;
}

void clear_word_buffer()
{
    memset(wordclock_words_buffer, 0, sizeof(wordclock_words_buffer));
}

bool word_buffer_to_bits(const char* buffer, const char* letters, uint8_t* bits, size_t bits_size)
{
    // Clear all bits
    memset(bits, 0, bits_size);

    // Make a mutable copy of the buffer to tokenize by spaces
    char buffer_copy[sizeof(wordclock_words_buffer)];
    strncpy(buffer_copy, buffer, sizeof(buffer_copy));
    buffer_copy[sizeof(buffer_copy) - 1] = '\0';

    // Search each space-delimited word in order within letters
    const char* search_start = letters;
    char* token = strtok(buffer_copy, " ");
    while (token != NULL) {
        const char* pos = strstr(search_start, token);
        if (pos) {
            size_t startIndex = pos - letters;
            size_t wordLen = strlen(token);
            // Mark bits for each character in this word
            for (size_t j = 0; j < wordLen; j++) {
                if (startIndex + j < bits_size) {
                    bits[startIndex + j] = 1;
                }
            }
            // Advance search pointer past this word to enforce order
            search_start = pos + wordLen;
        }
        else {
            // Word not found sequentially; stop searching further
            break;
        }
        token = strtok(NULL, " ");
    }

    return true;
}

void setTime(int hour, int minute) {
    clear_word_buffer(); // Clear the buffer before setting new time
    add_word_to_buffer("IT IS");
    bool use_to = false;

    //special case for midnight
    if (hour == 0 && minute == 0) {
        add_word_to_buffer("MIDNIGHT");
        goto end;
    }

    if (minute != 0) {
        if (minute > 30) { // to
            minute = 60 - minute;
            use_to = true;
        }
        else { // past
            use_to = false;
        }

        if (minute == 30) {
            add_word_to_buffer("HALF");
        }
        else if (minute >= 20) { //31-40
            add_word_to_buffer("TWENTY");
            switch (minute % 10) {
            case 1: add_word_to_buffer("ONE"); break;
            case 2: add_word_to_buffer("TWO"); break;
            case 3: add_word_to_buffer("THREE"); break;
            case 4: add_word_to_buffer("FOUR"); break;
            case 5: add_word_to_buffer("FIVE"); break;
            case 6: add_word_to_buffer("SIX"); break;
            case 7: add_word_to_buffer("SEVEN"); break;
            case 8: add_word_to_buffer("EIGHT"); break;
            case 9: add_word_to_buffer("NINE"); break;
            }
        }
        else if (minute >= 10) { //41 - 50
            switch (minute) {
            case 19: add_word_to_buffer("NINETEEN"); break;
            case 18: add_word_to_buffer("EIGHTEEN"); break;
            case 17: add_word_to_buffer("SEVENTEEN"); break;
            case 16: add_word_to_buffer("SIXTEEN"); break;
            case 15: add_word_to_buffer("A QUARTER"); break;
            case 14: add_word_to_buffer("FOURTEEN"); break;
            case 13: add_word_to_buffer("THIRTEEN"); break;
            case 12: add_word_to_buffer("TWELVE"); break;
            case 11: add_word_to_buffer("ELEVEN"); break;
            case 10: add_word_to_buffer("TEN"); break;
            }
        }
        else {
            switch (minute) {
            case 9: add_word_to_buffer("NINE"); break;
            case 8: add_word_to_buffer("EIGHT"); break;
            case 7: add_word_to_buffer("SEVEN"); break;
            case 6: add_word_to_buffer("SIX"); break;
            case 5: add_word_to_buffer("FIVE"); break;
            case 4: add_word_to_buffer("FOUR"); break;
            case 3: add_word_to_buffer("THREE"); break;
            case 2: add_word_to_buffer("TWO"); break;
            case 1: add_word_to_buffer("ONE"); break;
            }
        }

        if (use_to) {
            add_word_to_buffer("TO");
            hour = (hour + 1) % 24; // Increment hour for "to" case
        }
        else {
            add_word_to_buffer("PAST");
        }
    }

    switch (hour % 12) {
    case 0: add_word_to_buffer("TWELVE"); break; // Midnight
    case 1: add_word_to_buffer("ONE"); break;
    case 2: add_word_to_buffer("TWO"); break;
    case 3: add_word_to_buffer("THREE"); break;
    case 4: add_word_to_buffer("FOUR"); break;
    case 5: add_word_to_buffer("FIVE"); break;
    case 6: add_word_to_buffer("SIX"); break;
    case 7: add_word_to_buffer("SEVEN"); break;
    case 8: add_word_to_buffer("EIGHT"); break;
    case 9: add_word_to_buffer("NINE"); break;
    case 10: add_word_to_buffer("TEN"); break;
    case 11: add_word_to_buffer("ELEVEN"); break;
    }

    if (hour < 5) {
        add_word_to_buffer("AT NIGHT");
    }
    else if (hour < 12) {
        add_word_to_buffer("IN THE MORNING");
    }
    else if (hour < 17) {
        add_word_to_buffer("IN THE AFTERNOON");
    }
    else if (hour < 21) {
        add_word_to_buffer("IN THE EVENING");
    }
    else {
        add_word_to_buffer("AT NIGHT");
    }

end:
    // Convert the word buffer to bits
    if (!word_buffer_to_bits(wordclock_words_buffer, letters, bits, NUM_PIXELS)) {
        ESP_LOGE("Clock", "Failed to convert word buffer to bits");
        return;
    }

    // Set the LED mask based on the bits
    led_set_mask(bits);
}

void clock_task(void* pvParameters) {
    time_t now;
    struct tm timeinfo;
    int lastHour = -1;
    int lastMinute = -1;

    ESP_LOGI("Clock", "Clock task started - waiting for time sync");

    // Wait for time sync while LED shows cyclic yellow (set by wifi_connected handler)
    while (!is_time_synced()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI("Clock", "Time synced - switching to normal clock mode");

    // Restore saved LED configuration for normal clock operation
    led_persistent_config_t saved_config = led_get_persistent_config();
    led_set_color(saved_config.r, saved_config.g, saved_config.b, saved_config.w);
    led_set_effect(saved_config.effect);
    led_set_brightness(saved_config.brightness);
    led_set_speed(saved_config.speed);

    while (true) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMinute) {
            lastHour = timeinfo.tm_hour;
            lastMinute = timeinfo.tm_min;

            // Set the pixels based on the current time
            setTime(lastHour, lastMinute);
        }

        // Simulate a delay for the clock update
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void wifi_prov_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi Provisioning connected");
    led_set_color(0, 0, 255, 0); // Solid blue when provisioning connected
    led_set_effect(LED_SOLID);
}

void wifi_prov_started(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi Provisioning started");
    led_set_color(0, 0, 255, 0); // Blinking blue when provisioning started
    led_set_effect(LED_BREATHE);
}

void wifi_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi disconnected - waiting for connection");
    led_set_color(0, 255, 255, 0); // Blinking teal while waiting for WiFi
    led_set_effect(LED_BREATHE);
}

void wifi_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI("Clock", "WiFi connected - starting time sync");
    led_set_color(255, 255, 0, 0);  // Cyclic yellow during time sync
    led_set_effect(LED_CYCLIC);

    // Start the clock task which will handle the time sync and then switch to normal mode
    xTaskCreate(clock_task, "clock_task", 4096, NULL, 5, NULL);
}

void clock_init() {
    led_init(wordclock_led_config);
    led_set_effect(LED_BREATHE);  // Start with blinking teal waiting for WiFi
    led_set_color(0, 255, 255, 0);  // Teal color

    // Register event handlers for the LED flow
    esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, PROTOCOMM_TRANSPORT_BLE_CONNECTED, &wifi_prov_connected, NULL);
    esp_event_handler_register(WIFI_PROV_EVENT, WIFI_PROV_START, &wifi_prov_started, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_disconnected, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_connected, NULL);
}