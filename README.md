# Sleep Apnea Monitoring Device: User Manual

> [!WARNING]
> This device is a school project for investigating whether a low-cost embedded system can detect events associated with sleep apnea. It is **not** a clinically validated medical device and must not be used for diagnosis or treatment decisions.

## Contents

1. [Overview](#1-overview)
2. [Before you start](#2-before-you-start)
3. [Controls and indicators](#3-controls-and-indicators)
4. [Starting the device](#4-starting-the-device)
5. [Wi-Fi configuration](#5-wi-fi-configuration)
6. [Initialization and error mode](#6-initialization-and-error-mode)
7. [Using the device](#7-using-the-device)
8. [Quick reference](#8-quick-reference)

## 1. Overview

The device collects breathing-related temperature, SpO2 and heart rate data during sleep and sends it to a server over Wi-Fi using MQTT. It is controlled with two buttons (`SW1` and `SW2`), three LEDs (green, yellow and red), and optionally with commands sent from the server over MQTT.

## 2. Before you start

1. Make sure the device has power.
2. Take a comfortable sleeping position.
3. Place the MAX30102 sensor module on your finger using its sock.
4. Place the mask on your head as instructed for the project.

## 3. Controls and indicators

### Buttons

| Button | Function                                            |
| ------ | --------------------------------------------------- |
| `SW1`  | Starts, pauses, continues and ends sessions         |
| `SW2`  | Opens Wi-Fi configuration, or ends a paused session |

### LEDs

| LED    | Meaning                                                      |
| ------ | ------------------------------------------------------------ |
| Green  | The device is ready or a session is running                  |
| Yellow | The device is initializing, waiting for Wi-Fi configuration, or a session is paused |
| Red    | The device is in error mode                                  |

### LED summary

| LED behavior                          | Device state                    |
| ------------------------------------- | ------------------------------- |
| Yellow blinking                       | Waiting for Wi-Fi configuration |
| Yellow on (steady)                    | Initializing the device         |
| Green on (steady)                     | Ready, waiting for a session    |
| Green blinking every 0.5 s            | Session running                 |
| Green on (steady) and yellow blinking | Session paused                  |
| Green blinks 5 times (over 5 s)       | Session is being finalized      |
| Red blinking                          | Error mode                      |

## 4. Starting the device

1. Power on the device.
2. The device loads its software and checks whether it has stored Wi-Fi credentials.
3. If credentials are stored, the device tries to connect to Wi-Fi.
   - If the connection succeeds, the device continues to initialization (see [Section 6](#6-initialization-and-error-mode)).
   - If the connection does not succeed within about 15 seconds, the device keeps trying in the background and continues to initialization. It does not automatically enter Wi-Fi configuration.
4. If no credentials are stored, the device starts Wi-Fi configuration (see [Section 5](#5-wi-fi-configuration)).

If the device restarts during a session, it returns to pause mode after successful initialization, with the pump turned off.

## 5. Wi-Fi configuration

The device enters Wi-Fi configuration when:

- no Wi-Fi credentials are stored,
- the stored credentials cannot be read correctly, or
- you hold `SW2` for 3 seconds while the device is in the ready or error state.

Holding `SW2` restarts the device into Wi-Fi configuration. While the device is waiting for Wi-Fi configuration, the yellow LED blinks.

To configure Wi-Fi:

1. Wait until the yellow LED is blinking.
2. The device creates its own temporary Wi-Fi network (SoftAP). Connect your phone or computer to this network.
   - Network name: SAMD-Setup
   - Password: The password configured in the firmware (default: CHANGEME123)
3. Enter your Wi-Fi network name and password.
   - Address/page: `http://192.168.4.1/`
   - You can also enter the MQTT server address (optional). Leaving it empty keeps the previous or default server address.
4. Press Save. The device tries to connect to your Wi-Fi network using the new credentials. It allows up to five retries and has a 30-second timeout.
5. If the connection succeeds, the device saves the new credentials and restarts. It then continues to initialization (see [Section 6](#6-initialization-and-error-mode)).
6. If the connection fails or times out, the new credentials are not saved. The device stays in Wi-Fi configuration (yellow LED blinking). Repeat from step 3 and check that the name and password are correct.

## 6. Initialization and error mode

After startup in normal mode, the device initializes the air pump and the sensors. It also checks that both sensors provide recent readings within about 15 seconds.

- **Initialization successful:** the device goes to the ready state (see [Section 7.1](#71-ready-state)), or to pause mode if a session was interrupted.
- **Initialization fails:** the device enters error mode. The red LED blinks, the pump is turned off, and an error message is sent to the server through MQTT if connected, where the error can be seen.

The device also enters error mode if sensor readings stop arriving for about 3 seconds during a session or pause mode, or if the pump cannot start. If both sensors provide recent readings again, the device returns to the ready state automatically after at least 5 seconds in error mode.

## 7. Using the device

MQTT commands are sent as non-retained plain text messages to `sleep/cmd`. The device sends its state to `sleep/status` when connected to the server. The MQTT state for pause mode is `pause`.

### 7.1 Ready state

The green LED is on. The device is waiting for a session to start.

**To start a session:**

- Hold `SW1` for more than 3 seconds, or
- Send the start command from the server through MQTT.

When the green LED starts blinking, release `SW1`.

**To change the Wi-Fi settings:**

- Hold `SW2` for 3 seconds. The device restarts into Wi-Fi configuration (see [Section 5](#5-wi-fi-configuration)).

### 7.2 Session

During a session the device receives data from the sensors, runs the air pump, and tries to send the collected data to the server through MQTT. The green LED blinks at 0.5-second intervals. Data is sent to `sleep/data` when new temperature readings arrive or after about 5 seconds. Unavailable pulse readings may be sent as `null`, and timestamps are milliseconds since startup. Data is not saved for later if the MQTT connection is lost.

**To pause the session:**

- Press `SW1`, or
- Send the pause command from the server through MQTT.

**To end the session:**

- Hold `SW1` for more than 3 seconds, or
- Send the stop command from the server through MQTT. The device starts finalizing the session (see [Section 7.4](#74-finalizing-the-session)).

### 7.3 Pause mode

The green LED stays on and the yellow LED blinks. The session is paused. The pump is off and session data is not sent to the server.

**To continue the session:**

- Press `SW1`, or
- Send the continue command from the server through MQTT.

**To end the session:**

- Press `SW2`, or
- Send the stop command from the server through MQTT. The device starts finalizing the session (see [Section 7.4](#74-finalizing-the-session)).

### 7.4 Finalizing the session

When a session ends, the device turns off the pump and sends a finalizing status to the server if MQTT is connected. It does not explicitly send any remaining unsent sensor data. The green LED blinks five times over five seconds. After this, the device returns to the ready state (see [Section 7.1](#71-ready-state)).

## 8. Quick reference

### Ready state

| Action                       | Result                                            |
| ---------------------------- | ------------------------------------------------- |
| Hold `SW1` for more than 3 s | Start session (release when the green LED blinks) |
| Hold `SW2` for 3 s           | Wi-Fi configuration (device restarts)             |
| MQTT start command           | Start session                                     |

### Session running

| Action                       | Result      |
| ---------------------------- | ----------- |
| Press `SW1`                  | Pause       |
| Hold `SW1` for more than 3 s | End session |
| MQTT pause command           | Pause       |
| MQTT stop command            | End session |

### Pause mode

| Action                | Result      |
| --------------------- | ----------- |
| Press `SW1`           | Continue    |
| Press `SW2`           | End session |
| MQTT continue command | Continue    |
| MQTT stop command     | End session |

### Error mode

| Action                                  | Result                                |
| --------------------------------------- | ------------------------------------- |
| Hold `SW2` for 3 s                      | Wi-Fi configuration (device restarts) |
| Both sensors recover after at least 5 s | Return to ready state                 |