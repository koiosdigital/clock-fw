#include "wordclock.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdint.h"

#include "kd_pixdriver.h"
#include <soc/gpio_num.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include "kd_common.h"
#include "clock_events.h"

#include "sdkconfig.h"

static const char* TAG = "wordclock";

const char* letters =
"ITLISOTWENTYRONETWOETENMTHIRTEENFIVEMELEVENIFOURTHREEPNINETEENSUFOURTEENMIDNIGHTSIXTEENDEIGHTEENSEVENTEENOTWELVEHALFELQUARTEROTOPASTRONESATW"
"OSIXTWELVETFOURAFIVESEVENMEIGHTENINETENTTHREECELEVENINOTHENAFTERNOONMORNINGSATENIGHTEVENINGCANDTCOLDCOOLETWARMURAHOT";

#ifdef CONFIG_BASE_CLOCK_TYPE_WORDCLOCK

#if defined(CONFIG_WORDCLOCK_LED_IS_RGBW) && CONFIG_WORDCLOCK_LED_IS_RGBW
static bool is_rgbw = true;
#else
static bool is_rgbw = false;
#endif

uint8_t bits[256] = { 0 };
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
    if (!word_buffer_to_bits(wordclock_words_buffer, letters, bits, 256)) {
        return;
    }

    // Use static mask buffer to avoid heap allocation on every update
    static std::vector<uint8_t> mask_buffer;
    if (mask_buffer.empty()) {
        mask_buffer.resize(256);
    }
    std::copy(bits, bits + 256, mask_buffer.begin());
    PixelDriver::getMainChannel()->setMask(mask_buffer);
}

// Update the display with current time
static void update_display(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGD(TAG, "Updating display: %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    setTime(timeinfo.tm_hour, timeinfo.tm_min);
}

// Event handler for clock events
static void clock_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == CLOCK_EVENTS) {
        switch (id) {
            case CLOCK_EVENT_MINUTE_TICK:
            case CLOCK_EVENT_CONFIG_CHANGED:
            case CLOCK_EVENT_FORCE_REFRESH:
                update_display();
                break;
            default:
                break;
        }
    }
}

// Event handler for NTP sync
static void ntp_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == KD_NTP_EVENT_SYNC_COMPLETE) {
        ESP_LOGI(TAG, "NTP synced, loading display settings");
        PixelDriver::getMainChannel()->loadFromNVS();
        update_display();
    }
}

void wordclock_clock_task(void* pvParameter) {
    ESP_LOGI(TAG, "Wordclock task started");

    // Show syncing animation
    PixelDriver::getMainChannel()->setColor(PixelColor(255, 255, 0)); // Yellow during sync
    PixelDriver::getMainChannel()->setEffectByID("CYCLIC");

    // Register for clock events
    esp_event_handler_register(CLOCK_EVENTS, ESP_EVENT_ANY_ID, clock_event_handler, nullptr);
    esp_event_handler_register(KD_NTP_EVENTS, KD_NTP_EVENT_SYNC_COMPLETE, ntp_event_handler, nullptr);

    // If already synced, switch to display mode immediately
    if (kd_common_ntp_is_synced()) {
        ESP_LOGI(TAG, "Already synced, starting display");
        PixelDriver::getMainChannel()->loadFromNVS();
        update_display();
    }

    // Task sleeps indefinitely - all updates come from events
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}

void wordclock_clock_init() {
    PixelDriver::initialize(60);
    PixelDriver::setCurrentLimit(2000); // 600mA limit for Nixie LEDs
    PixelDriver::addChannel(ChannelConfig((gpio_num_t)CONFIG_WORDCLOCK_LED_DATA_PIN, 256, is_rgbw ? PixelFormat::RGBW : PixelFormat::RGB, "Word Clock"));
    PixelDriver::start();

    PixelDriver::getMainChannel()->setColor(PixelColor(0, 255, 255));
    PixelDriver::getMainChannel()->setEffectByID("BREATHE");
}

#endif