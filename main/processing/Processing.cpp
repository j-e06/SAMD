// processing/Processing.cpp
#include "Processing.h"

#include <inttypes.h>
#include <initializer_list>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "mqtt/MQTTHandler.h"
#include "pump/pump.h"

static const char *TAG = "Processing";

static constexpr int64_t SENSOR_STALE_US    = 3'000'000;   // we need sensor data every sec, if 3s has passed since we got data, sensor dead
static constexpr int64_t INIT_TIMEOUT_US    = 15'000'000;  // both sensors must have reported by this point
static constexpr int64_t ERROR_RETRY_US     = 5'000'000;   // minimum time in error before checking again
static constexpr int64_t PUBLISH_MAX_GAP_US = 5'000'000;   // during a session, publish at least this frequently
static constexpr int64_t PUMP_REARM_US      = 10'000'000;
static constexpr int64_t FINALIZE_US        = 5'000'000;   // 5 blinks of 1 s
static constexpr int64_t BLINK_HALF_US      = 500'000;
static constexpr TickType_t WAKE_TICKS      = pdMS_TO_TICKS(100);

static bool fresh(int64_t ts, int64_t now) {
    return ts != 0 && now - ts < SENSOR_STALE_US;
}

static const char *stateName(Processing::State s) {
    switch (s) {
        case Processing::State::Provisioning: return "provisioning";
        case Processing::State::Init:         return "init";
        case Processing::State::Ready:        return "ready";
        case Processing::State::Session:      return "session";
        case Processing::State::Paused:       return "pause";
        case Processing::State::Finalizing:   return "finalizing";
        case Processing::State::Error:        return "error";
    }
    return "unknown";
}

Processing::Processing(QueueHandle_t queue, MQTTHandler *mqtt, Pump *pump)
    : queue_(queue), mqtt_(mqtt), pump_(pump) {}

esp_err_t Processing::begin(State initial) {
    if (task_) return ESP_ERR_INVALID_STATE;

    const gpio_config_t leds = {
        .pin_bit_mask = (1ULL << GREEN_LED) | (1ULL << AMBER_LED) | (1ULL << RED_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&leds), TAG, "led gpio config");
    for (gpio_num_t p : {GREEN_LED, AMBER_LED, RED_LED}) gpio_set_level(p, 0);

    pump_->off();
    state_ = initial;
    state_since_ = esp_timer_get_time();
    if (mqtt_) mqtt_->publishStatus(stateName(state_));   // sent after the broker connection comes up

    if (xTaskCreate(&Processing::taskEntry, "Processing", 6144, this, 5, &task_) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Processing::taskEntry(void *arg) {
    static_cast<Processing *>(arg)->run();
}

void Processing::run() {
    for (;;) {
        // checks for dead sensors even if we have no events going on, also drives the leds
        queue_entry e;
        if (xQueueReceive(queue_, &e, WAKE_TICKS) == pdTRUE) handle(e);

        const int64_t now = esp_timer_get_time();
        tick(now);
        updateLeds(now);
    }
}
static const char *missingSensors(bool ntc_ok, bool pulse_ok)
{
    if (!ntc_ok && !pulse_ok) return "ntc,pulse";
    if (!ntc_ok) return "ntc";
    if (!pulse_ok) return "pulse";
    return nullptr;
}

void Processing::tick(int64_t now)
{
    const bool ntc_ok = fresh(ntc_ts_, now);
    const bool pulse_ok = fresh(pulse_ts_, now);

    switch (state_) {
    case State::Init:
        if (ntc_ok && pulse_ok) {
            const bool resume = resume_;
            resume_ = false;

            // sessions that got interrupted go to Pause, they don't restart pump cuz its so bad and can't be truested
            setState(resume ? State::Paused : State::Ready,
                     now,
                     resume ? "recovered" : nullptr);
        }
        else if (now - state_since_ > INIT_TIMEOUT_US) {
            resume_ = false;
            setState(State::Error, now, missingSensors(ntc_ok, pulse_ok));
        }
        break;

    case State::Error:
        if (now - state_since_ > ERROR_RETRY_US &&
            ntc_ok &&
            pulse_ok) {
            setState(State::Ready, now);
            }
        break;

    case State::Session:
        if (!ntc_ok || !pulse_ok) {
            setState(State::Error, now, missingSensors(ntc_ok, pulse_ok));
            break;
        }

        if (now >= pump_rearm_at_) {
            if (pump_->on() != ESP_OK) {
                setState(State::Error, now, "pump");
                break;
            }
            pump_rearm_at_ = now + PUMP_REARM_US;
        }

        if (ntc_new_ ||
            now - last_publish_ >= PUBLISH_MAX_GAP_US) {
            publish(now);
            }
        break;

    case State::Paused:
        if (!ntc_ok || !pulse_ok) {
            setState(State::Error, now, missingSensors(ntc_ok, pulse_ok));
        }
        break;

    case State::Finalizing:
        if (now - state_since_ >= FINALIZE_US) {
            setState(State::Ready, now);
        }
        break;

    default:
        break;
    }
}

void Processing::handle(const queue_entry &e) {
    switch (e.type) {
        case EventType::Ntc:
            ntc_ = e.ntc;
            ntc_ts_ = e.ts_us;
            ntc_new_ = true;
            ESP_LOGD(TAG, "NTC: %.2f C", e.ntc.temperature);
            break;
        case EventType::Pulse:
            pulse_ = e.pulse;
            pulse_ts_ = e.ts_us;
            ESP_LOGD(TAG, "Pulse: hr %" PRId32 " | spo2 %" PRId32, e.pulse.heart_rate, e.pulse.spo2);
            break;
        case EventType::Command:
            onCommand(e.command.command, esp_timer_get_time());
            break;
        case EventType::Button:
            onButton(e.button.id, e.button.long_press, esp_timer_get_time());
            break;
    }
}
void Processing::onCommand(Command cmd, int64_t now) {
    switch (cmd) {
    case Command::Start:
        if (state_ == State::Ready)   { setState(State::Session, now); return; }
        break;
    case Command::Pause:
        if (state_ == State::Session) { setState(State::Paused, now); return; }
        break;
    case Command::Continue:
        if (state_ == State::Paused)  { setState(State::Session, now); return; }
        break;
    case Command::Stop:
        if (state_ == State::Session || state_ == State::Paused) { setState(State::Finalizing, now); return; }
        break;
    }
    ESP_LOGW(TAG, "command %d ignored in state %s", static_cast<int>(cmd), stateName(state_));
}


// button & mqtt logic basically
void Processing::onButton(uint8_t id, bool long_press, int64_t now) {
    if (id == 1 && long_press) {                       // SW1 held 3 s: start / finish a session
        if (state_ == State::Ready)        onCommand(Command::Start, now);
        else if (state_ == State::Session) onCommand(Command::Stop, now);
    } else if (id == 1) {                              // SW1 press: pause / continue
        if (state_ == State::Session)      onCommand(Command::Pause, now);
        else if (state_ == State::Paused)  onCommand(Command::Continue, now);
    } else if (id == 2 && !long_press) {               // SW2 press while paused: end the session
        if (state_ == State::Paused)       onCommand(Command::Stop, now);
    } else if (id == 2 && long_press) {                // SW2 held 3 s: wifi and server configuration
        if ((state_ == State::Ready || state_ == State::Error) && wifiConfigCb_) wifiConfigCb_();
    }
}
void Processing::setState(State next, int64_t now, const char *detail) {
    if (next == state_) return;

    // pumpa runa only ina session yesa?, if it cant start we errored
    if (next == State::Session && pump_->on() != ESP_OK) {
        ESP_LOGE(TAG, "pump failed to start");
        next = State::Error;
        detail = "pump";
    }
    if (next != State::Session) pump_->off();

    // session needs to be retained across reboots, stored only on change
    const bool active = (next == State::Session || next == State::Paused);
    if (active != sessionPersisted_) {
        sessionPersisted_ = active;
        if (sessionCb_) sessionCb_(active);
    }

    ESP_LOGI(TAG, "state %s -> %s", stateName(state_), stateName(next));
    state_ = next;
    state_since_ = now;
    pump_rearm_at_ = now + PUMP_REARM_US;
    last_publish_ = now;
    ntc_new_ = false;
    if (mqtt_) mqtt_->publishStatus(stateName(next), detail);
}
void Processing::publish(int64_t now) {
    // check for valid data and recency of them
    combined_data d;
    if (fresh(ntc_ts_, now))
        d.temp = ntc_.temperature;
    if (fresh(pulse_ts_, now)) {
        if (pulse_.heart_rate > 0) d.hr   = pulse_.heart_rate;   // sensor reports -1 0 for no reading
        if (pulse_.spo2 > 0)       d.spo2 = pulse_.spo2;
    }

    if (mqtt_) mqtt_->publish(d);

    ntc_new_ = false;
    last_publish_ = now;
}

void Processing::updateLeds(int64_t now) {
    const bool blink = (((now - state_since_) / BLINK_HALF_US) & 1) == 0;
    bool green = false, amber = false, red = false;

    switch (state_) {
        case State::Provisioning: amber = blink;                 break;
        case State::Init:         amber = true;                  break;
        case State::Ready:        green = true;                  break;
        case State::Session:      green = blink;                 break;
        case State::Paused:       green = true; amber = blink;   break;
        case State::Finalizing:   green = blink;                 break;
        case State::Error:        red = blink;                   break;
    }
    gpio_set_level(GREEN_LED, green);
    gpio_set_level(AMBER_LED, amber);
    gpio_set_level(RED_LED,   red);
}