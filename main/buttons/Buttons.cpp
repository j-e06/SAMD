// buttons/Buttons.cpp
#include "Buttons.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "utilities.h"

static const char *TAG = "Buttons";

static constexpr int     POLL_MS        = 20;
static constexpr int     DEBOUNCE_POLLS = 2;          // level must differ for 2 polls (40 ms)
static constexpr int64_t LONG_PRESS_US  = 3'000'000;

esp_err_t Buttons::begin() {
    if (task_) return ESP_ERR_INVALID_STATE;

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << SW1_BTN) | (1ULL << SW2_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "button gpio config");

    if (xTaskCreate(&Buttons::taskEntry, "Buttons", 3072, this, 6, &task_) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Buttons::taskEntry(void *arg) {
    static_cast<Buttons *>(arg)->run();
}
void Buttons::post(int index, bool long_press) {
    auto e = make_entry(EventType::Button);
    e.button = { static_cast<uint8_t>(index + 1), long_press };
    if (!post_event(queue_, e)) ESP_LOGW(TAG, "button event dropped, queue full");
}

void Buttons::run() {
    const gpio_num_t pins[2] = { SW1_BTN, SW2_BTN };
    bool pressed[2]  = { false, false };
    bool longSent[2] = { false, false };
    int  differs[2]  = { 0, 0 };          // consecutive polls that disagree with `pressed`
    int64_t since[2] = { 0, 0 };

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        const int64_t now = esp_timer_get_time();

        for (int i = 0; i < 2; i++) {
            const bool raw = gpio_get_level(pins[i]) == 0;   // active low
            if (raw != pressed[i]) {
                if (++differs[i] < DEBOUNCE_POLLS) continue;
                differs[i] = 0;
                pressed[i] = raw;
                if (raw) {
                    since[i] = now;
                    longSent[i] = false;
                } else if (!longSent[i]) {
                    post(i, false);                           // short press, reported on release
                }
            } else {
                differs[i] = 0;
                if (pressed[i] && !longSent[i] && now - since[i] >= LONG_PRESS_US) {
                    longSent[i] = true;
                    post(i, true);
                }
            }
        }
    }
}