// processing/Processing.h
#ifndef BLINK_PROCESSING_H
#define BLINK_PROCESSING_H

#include "esp_err.h"
#include "utilities.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class MQTTHandler;
class Pump;

// this is the end point for sensor data, keeps sensor values, runs pump, drives leds and sends data to server
class Processing {
public:
    enum class State : uint8_t { Provisioning, Init, Ready, Session, Paused, Finalizing, Error };

    // mqtt nullptr = provisioning mode
    Processing(QueueHandle_t queue, MQTTHandler *mqtt, Pump *pump);

    // sw2 held in ready or error state goes here
    void onWifiConfigRequest(void (*cb)()) { wifiConfigCb_ = cb; }

    // called prior to begin(), handles potential reboot during active session
    // goes to Ready after
    void onSessionActive(void (*cb)(bool active)) { sessionCb_ = cb; }

    // called before begin(), prev session cut by reset, after init is done goes to Session and continues dat sending etc
    void resumeSessionAfterInit() { resume_ = true; sessionPersisted_ = true; }

    // configs leds & starts main task
    esp_err_t begin(State initial);
    Processing(const Processing &) = delete;
    Processing &operator=(const Processing &) = delete;

private:
    static void taskEntry(void *arg);
    void run();
    void tick(int64_t now);
    void handle(const queue_entry &e);
    void onCommand(Command cmd, int64_t now);
    void onButton(uint8_t id, bool long_press, int64_t now);
    void setState(State next, int64_t now, const char *detail = nullptr);
    void publish(int64_t now);
    void updateLeds(int64_t now);

    QueueHandle_t queue_;
    MQTTHandler  *mqtt_;
    Pump         *pump_;
    TaskHandle_t  task_ = nullptr;
    void (*wifiConfigCb_)() = nullptr;
    void (*sessionCb_)(bool active) = nullptr;

    State   state_ = State::Init;
    int64_t state_since_ = 0;
    int64_t pump_rearm_at_ = 0;
    int64_t last_publish_ = 0;

    bool resume_ = false;             // resume a cut session once init is done
    bool sessionPersisted_ = false;   // what the session callback last stored

    ntc_data   ntc_{};                  // last got samples, checked if usable in publish
    pulse_data pulse_{-1, -1};
    int64_t ntc_ts_ = 0, pulse_ts_ = 0; // sensor data timestamp, 0 = no data ever
    bool ntc_new_ = false;              // got ntc data since last pub?
};

#endif //BLINK_PROCESSING_H