SLEEP APNEA MONITORING DEVICE - USER MANUAL

======================================================================
NOTICE
======================================================================

This device is a school project for investigating whether a low-cost
embedded system can detect events associated with sleep apnea. It is
not a clinically validated medical device and must not be used for
diagnosis or treatment decisions.

======================================================================
1. OVERVIEW
======================================================================

The device collects breathing, SpO2 and heart rate data during sleep
and sends it to a server over Wi-Fi using MQTT. It is controlled with
two buttons (SW1 and SW2), two LEDs (green and yellow), and optionally
with commands sent from the server over MQTT.

======================================================================
2. BEFORE YOU START
======================================================================

1. Make sure the device has power.
2. Take a comfortable sleeping position.
3. Place the MAX30102 sensor module on your finger using its sock.
4. Place the mask on your head like any other clinical mask.

======================================================================
3. CONTROLS AND INDICATORS
======================================================================

Buttons
  SW1  Starts, pauses, continues and ends sessions.
  SW2  Opens Wi-Fi configuration, or ends a paused session.

LEDs
  Green LED   Shows the device is ready or a session is running.
  Yellow LED  Shows the device is waiting for Wi-Fi configuration or
              that a session is paused.

LED summary
  Yellow blinking                  Waiting for Wi-Fi configuration
  Green on (steady)                Ready, waiting for a session
  Green blinking every 0.5 s       Session running
  Green and yellow both blinking   Session paused
  Green blinks 5 times (1 s each)  Session is being finalized

======================================================================
4. STARTING THE DEVICE
======================================================================

1. Power on the device.
2. The device loads its software and checks whether it has stored
   Wi-Fi credentials.
3. If credentials are stored, the device tries to connect to Wi-Fi.
   - If the connection succeeds, the device initializes itself
     (see Section 6).
   - If the connection fails, or no credentials are stored, the
     device starts Wi-Fi configuration (see Section 5).

======================================================================
5. WI-FI CONFIGURATION
======================================================================

The device enters Wi-Fi configuration when:
  - no Wi-Fi credentials are stored,
  - the stored credentials do not work, or
  - you hold SW2 for 3 seconds while the device is in the ready state.

While the device is waiting for Wi-Fi configuration, the yellow LED
blinks.

To configure Wi-Fi:
1. Wait until the yellow LED is blinking.
2. The device creates its own temporary Wi-Fi network (SoftAP).
   Connect your phone or computer to this network.
   Network name: [SoftAP name]
   Password:     [SoftAP password, if any]
3. Enter your Wi-Fi network name and password.
   Address/page: [add configuration page address]
4. The device tries to connect to your Wi-Fi network using the new
   credentials. It makes up to five attempts.
5. If one of the attempts succeeds, the device continues to
   initialization (see Section 6).
6. If all five attempts fail, the device goes back to waiting for new
   credentials (yellow LED blinking). Repeat from step 2 and check
   that the name and password are correct.

======================================================================
6. INITIALIZATION AND ERROR MODE
======================================================================

After connecting to Wi-Fi, the device initializes the air pump and the
sensors. It also reads test data from the sensors to check that they
are working correctly.

If initialization is successful, the device goes to the ready state
(see Section 7.1).

If initialization fails, the device enters error mode and sends an
error message to the server through MQTT, where the error can be seen.

======================================================================
7. USING THE DEVICE
======================================================================

7.1 Ready state
  The green LED is on. The device is waiting for a session to start.

  To start a session:
    - Hold SW1 for more than 3 seconds, or
    - Send the start command from the server through MQTT.

  To change the Wi-Fi settings:
    - Hold SW2 for 3 seconds. The device goes to Wi-Fi configuration
      (see Section 5).

7.2 Session
  During a session the device receives data from the sensors, runs the
  air pump, and sends the collected data to the server through MQTT.
  The green LED blinks at 0.5-second intervals.

  To pause the session:
    - Press SW1, or
    - Send the pause command from the server through MQTT.

  To end the session:
    - Hold SW1 for more than 3 seconds. The device starts finalizing
      the session (see Section 7.4).

7.3 Pause mode
  The green and yellow LEDs both blink. The session is paused.

  To continue the session:
    - Press SW1, or
    - Send the continue command from the server through MQTT.

  To end the session:
    - Press SW2. The device starts finalizing the session
      (see Section 7.4).

7.4 Finalizing the session
  When a session ends, the device finalizes the testing and sends the
  remaining data to the server. The green LED blinks five times, each
  blink lasting one second. After this, the device returns to the
  ready state (see Section 7.1).

======================================================================
8. QUICK REFERENCE
======================================================================

Ready state
  Hold SW1 for 3 s   Start session (release when green LED blinks)
  Hold SW2 for 3 s   Wi-Fi configuration
  MQTT start         Start session

Session running
  Press SW1          Pause
  Hold SW1 (>3 s)    End session
  MQTT pause         Pause

Pause mode
  Press SW1          Continue
  Press SW2          End session
  MQTT continue      Continue