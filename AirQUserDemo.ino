#include <Arduino.h>

#include <Wire.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_GDEW0154D67.hpp>
#include <WiFi.h>
#include <esp_sntp.h>
#include <freertos/queue.h>
#include <FastLED.h>

#include "I2C_BM8563.h"
#include <SensirionI2CScd4x.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2cSht4x.h>
#include <SparkFunBME280.h>
#include <OneButton.h>
#include <cJSON.h>

#include "MqData.hpp"
#include "config.h"
#include "misc.h"
#include "DataBase.hpp"
#include "MainAppView.hpp"
#include "Sensor.hpp"
#include "AppWeb.hpp"

CRGB leds[NUM_LEDS];

class AirQ_GFX : public lgfx::LGFX_Device {
    lgfx::Panel_GDEW0154D67 _panel_instance;
    lgfx::Bus_SPI           _spi_bus_instance;

   public:
    AirQ_GFX(void) {
        {
            auto cfg = _spi_bus_instance.config();

            cfg.pin_mosi   = EPD_MOSI;
            cfg.pin_miso   = EPD_MISO;
            cfg.pin_sclk   = EPD_SCLK;
            cfg.pin_dc     = EPD_DC;
            cfg.freq_write = EPD_FREQ;

            _spi_bus_instance.config(cfg);
            _panel_instance.setBus(&_spi_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();

            cfg.invert       = false;
            cfg.pin_cs       = EPD_CS;
            cfg.pin_rst      = EPD_RST;
            cfg.pin_busy     = EPD_BUSY;
            cfg.panel_width  = 200;
            cfg.panel_height = 200;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;

            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
    bool begin(void) { return init_impl(true , false); };
};


typedef enum RunMode_t {
    E_RUN_MODE_FACTORY = 0,
    E_RUN_MODE_MAIN,
    E_RUN_MODE_SETTING,
    E_RUN_MODE_APSETTING,
} RunMode_t;


typedef enum FactoryState_t {
    E_FACTORY_STATE_INIT = 0,
    E_FACTORY_STATE_COUNTDOWN,
} FactoryState_t;


typedef enum SettingState_t {
    E_SETTING_STATE_INIT = 0,
    E_SETTING_STATE_AP,
    E_SETTING_STATE_WEB,
    E_SETTING_STATE_DONE,
} SettingState_t;


typedef enum APSettingState_t {
    E_AP_SETTING_STATE_INIT = 0,
    E_AP_SETTING_STATE_AP,
    E_AP_SETTING_STATE_WEB,
    E_AP_SETTING_STATE_WAIT,
    E_AP_SETTING_STATE_DONE,
} APSettingState_t;


typedef enum ButtonID_t {
    E_BUTTON_NONE,
    E_BUTTON_A,
    E_BUTTON_B,
    E_BUTTON_POWER,
} ButtonID_t;


typedef enum ButtonClickType_t {
    E_BUTTON_CLICK_TYPE_NONE,
    E_BUTTON_CLICK_TYPE_SINGLE,
    E_BUTTON_CLICK_TYPE_PRESS,
} ButtonClickType_t;


typedef struct ButtonEvent_t {
    ButtonID_t id;
    ButtonClickType_t type;
} ButtonEvent_t;

typedef struct NetworkStatusMsgEvent_t {
    char title[16];
    char content[16];
} NetworkStatusMsgEvent_t;

typedef struct WiFiStatusEvent_t {
    WiFiEvent_t event;
    WiFiEventInfo_t info;
} WiFiStatusEvent_t;


RunMode_t runMode = E_RUN_MODE_MAIN;

AirQ_GFX lcd;
M5Canvas mainCanvas(&lcd);
StatusView statusView(&lcd, &mainCanvas);

SensirionI2CScd4x scd4x;
SensirionI2CSen5x sen5x;
SensirionI2cSht4x sht40;
BME280 bmp280;
I2C_BM8563 bm8563(I2C_BM8563_DEFAULT_ADDRESS, Wire);
Sensor sensor(scd4x, sen5x, sht40, bmp280, bm8563);

MqData mqdata;
int64_t dataTransferTimestamp;
int64_t SensorInitTimestamp;

OneButton btnA = OneButton(
    USER_BTN_A,  // Input pin for the button
    true,        // Button is active LOW
    true         // Enable internal pull-up resistor
);

OneButton btnB = OneButton(
    USER_BTN_B,  // Input pin for the button
    true,        // Button is active LOW
    true         // Enable internal pull-up resistor
);

OneButton btnPower = OneButton(
    USER_BUTTON_POWER,  // Input pin for the button
    true,        // Button is active LOW
    false         // Enable internal pull-up resistor
);

QueueHandle_t buttonEventQueue;
QueueHandle_t networkStatusMsgEventQueue;
QueueHandle_t wifiStatusEventQueue;

String mac;
String apSSID;

void listDirectory(fs::FS &fs, const char * dirname, uint8_t levels);
void splitLongString(String &text, int32_t maxWidth, const lgfx::IFont* font);

void setup() {
    Serial.begin(115200);

    // for debugging: give USB some time to detect the serial port
    delay(1000);

    log_i("Project name: AirQ demo");
    log_i("Build: %s", GIT_COMMIT);
    log_i("Version: %s", APP_VERSION);

    log_i("Turn on main power");
    pinMode(POWER_HOLD, OUTPUT);
    digitalWrite(POWER_HOLD, HIGH);

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // gpio_hold_dis((gpio_num_t)SEN55_POWER_EN);
        // gpio_deep_sleep_hold_dis();
    } else {
        log_i("Turn on SEN55 power");
        pinMode(SEN55_POWER_EN, OUTPUT);
        digitalWrite(SEN55_POWER_EN, LOW);
    }

    log_i("Initialize RGB LED");
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB::Red;
    FastLED.show();
    delay(100);
    leds[0] = CRGB::Green;
    FastLED.show();
    delay(100);
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(100);
    FastLED.clear();
    FastLED.show();

    log_i("LittleFS init");
    if (FORMAT_FILESYSTEM) {
        FILESYSTEM.format();
    }
    FILESYSTEM.begin();
    listDirectory(FILESYSTEM, "/", 1);
    db.loadFromFile();
    db.dump();

    /** Start Beep */
    if (db.buzzer.onoff == true) {
        ledcAttachPin(BUZZER_PIN, 0);
    } else {
        ledcDetachPin(BUZZER_PIN);
    }
    BUTTON_TONE();

    log_i("Screen init");
    lcd.begin();
    lcd.setEpdMode(epd_mode_t::epd_fastest);
    // lcd.sleep();

    statusView.begin();

    /* initialize network (WiFi) */
    networkStatusMsgEventQueue = xQueueCreate(16, sizeof(NetworkStatusMsgEvent_t));
    wifiStatusEventQueue = xQueueCreate(8, sizeof(WiFiStatusEvent_t));
    wifiAPSTASetup();
    appWebServer();

    /* apply sane defaults for missing/emty values */
    if (db.nickname.length() == 0) {
        String tmp = mac;
        tmp.toLowerCase();
        db.nickname = "Q-" + tmp.substring(6, 12);
    }

    /* Initialize MQTT module */
    log_i("MqData init");
    mqdata.setConfig(db.mqdata.username.c_str(), db.mqdata.password.c_str(), 
                     db.mqdata.server.c_str(), db.mqdata.port, 
                     db.mqdata.topicPrefix.c_str(), mac.c_str());

    if (db.mqdata2.server && (db.mqdata2.server.length() > 0)) {
        log_i("MqData init - have 2nd MQTT server present");
        mqdata.setConfig2(db.mqdata2.username.c_str(), db.mqdata2.password.c_str(), 
                          db.mqdata2.server.c_str(), db.mqdata2.port);
    }


    log_i("I2C init");
    //pinMode(GROVE_SDA, OUTPUT);
    //pinMode(GROVE_SCL, OUTPUT);
    // internal I2C bus
    Wire.begin(I2C1_SDA_PIN, I2C1_SCL_PIN);
    // external Grove I2C bus
    Wire1.begin(GROVE_SDA, GROVE_SCL);

    log_i("RTC(BM8563) init");
    bm8563.begin();
    bm8563.clearIRQ();

    // wait here util WLAN is connected
    uint8_t count=0;
    while ((WiFi.isConnected() != true) && (count++ < 20)) {
        log_i("WiFi not yet conected, waiting (%i)...", count);
        statusView.updateNetworkStatus("WIFI", "con...");
        statusView.load();
        delay(1000);
    }
    if (WiFi.isConnected() == true) {
        char rssi[10];
        log_i("WiFi is connected (count=%i)", count);
        snprintf(rssi, sizeof(rssi)-1, "%d dBm", WiFi.RSSI());
        rssi[sizeof(rssi)-1]='\0';
        statusView.updateNetworkStatus("WIFI", rssi);
        statusView.load();
    } else {
        log_i("WiFi NOT CONNECTED (count=%i)", count);
    }

    log_i("NTP init");
    sntp_servermode_dhcp(1);
    configTzTime(
        db.ntp.tz.c_str(),
        db.ntp.ntpServer0.c_str(),
        db.ntp.ntpServer1.c_str(),
        "pool.ntp.org"
    );

    // optional sensors on external GROVE bus
    log_i("SHT40 sensor init");
    char errorMessage[256];
    uint16_t error;
    sht40.begin(Wire1, SHT40_I2C_ADDR_44);
    uint32_t serialNumber = 0;
    error = sht40.serialNumber(serialNumber);
    if (error) {
        errorToString(error, errorMessage, sizeof errorMessage);
        log_w("Error trying to execute serialNumber(): %s", errorMessage);
        sensor.sht40.is_present=0;
    } else {
        sensor.sht40.is_present=1;
        log_i("SHT40 serialNumber: %d", serialNumber);
    }

    log_i("BMP280 sensor init");
    bmp280.setI2CAddress(0x76);
    if (bmp280.beginI2C(Wire1)) {
        uint32_t ChipID = 0;
        ChipID = bmp280.readRegister(BME280_CHIP_ID_REG);
        if ((ChipID == 0x58) || (ChipID == 0x60)) {
            log_i("BMP280 - 0x%x -> %s Sensor", ChipID, (ChipID == 0x58)? "BMP" : "BME");
            sensor.bmp280.is_present=1;
        } else {
            log_w("BMP280 - unknown ChipID: 0x%x", ChipID);
            sensor.bmp280.is_present=0;
        }
    } else {
        log_w("BMP280 init failed");
        sensor.bmp280.is_present=0;
    }
    bmp280.setPressureOverSample(8);
  
    // internal sensors on external I2C bus
    log_i("SCD40 sensor init");
    scd4x.begin(Wire);
    /** stop potentially previously started measurement */
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        errorToString(error, errorMessage, 256);
        log_w("Error trying to execute stopPeriodicMeasurement(): %s", errorMessage);
    }
    /** Start Measurement */
    error = scd4x.startPeriodicMeasurement();
    if (error) {
        errorToString(error, errorMessage, 256);
        log_w("Error trying to execute startPeriodicMeasurement(): %s", errorMessage);
    }
    log_i("Waiting for first measurement... (5 sec)");

    /** Init SEN55 */
    log_i("SEN55 sensor init");
    sen5x.begin(Wire);
    if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
        error = sen5x.deviceReset();
        if (error) {
            errorToString(error, errorMessage, 256);
            log_w("Error trying to execute deviceReset(): %s", errorMessage);
        }
        float tempOffset = 0.0;
        error = sen5x.setTemperatureOffsetSimple(tempOffset);
        if (error) {
            errorToString(error, errorMessage, 256);
            log_w("Error trying to execute setTemperatureOffsetSimple(): %s", errorMessage);
        } else {
            log_i("Temperature Offset set to %f deg. Celsius (SEN54/SEN55 only)", tempOffset);
        }
    }

    /** Start Measurement */
    // do this always, as SEN55 is put into idle mode upon shutdown() when USB powered
    log_i("Command SEN55 to continuous measurement mode");
    error = sen5x.startMeasurement();
    if (error) {
        errorToString(error, errorMessage, 256);
        log_w("Error trying to execute startMeasurement(): %s", errorMessage);
    }

    log_i("Sensors initialized.");

    dataTransferTimestamp = 0;
    SensorInitTimestamp = esp_timer_get_time() / 1000;

    /** fixme: 超时处理 */
    bool isDataReady = false;
    do {
        error = scd4x.getDataReadyFlag(isDataReady);
        if (error) {
            errorToString(error, errorMessage, 256);
            log_w("Error trying to execute getDataReadyFlag(): %s", errorMessage);
            return ;
        }
    } while (!isDataReady);

    if (db.factoryState) {
        log_i("in factor state -> E_RUN_MODE_FACTORY");
        FAIL_TONE();
        runMode = E_RUN_MODE_FACTORY;
    }

    btnA.attachClick(btnAClickEvent);
    btnA.attachLongPressStart(btnALongPressStartEvent);
    btnA.setPressMs(5000);
    btnB.attachClick(btnBClickEvent);
    btnB.attachLongPressStart(btnBLongPressStartEvent);
    btnB.setPressMs(5000);
    // btnPower.attachClick(btnPowerClickEvent);
    buttonEventQueue = xQueueCreate(16, sizeof(ButtonEvent_t));

    xTaskCreatePinnedToCore(buttonTask, "Button Task", 4096, NULL, 5, NULL, APP_CPU_NUM);

    log_d("leaving setup()");
}


void loop() {
    log_d("entering loop()");

    ButtonEvent_t buttonEvent = {
        .id = E_BUTTON_NONE,
        .type = E_BUTTON_CLICK_TYPE_NONE
    };
    xQueueReceive(buttonEventQueue, &buttonEvent, (TickType_t)10);

    switch (runMode) {
        case E_RUN_MODE_FACTORY: {
            factoryApp(&buttonEvent);
        }
        break;

        case E_RUN_MODE_MAIN: {
            mainApp(&buttonEvent);
        }
        break;

        case E_RUN_MODE_SETTING: {
            settingApp(&buttonEvent);
        }
        break;

        case E_RUN_MODE_APSETTING: {
            apSettingApp(&buttonEvent);
        }
        break;

        default: break;
    }
    networkStatusUpdateServiceTask();
    mqdataServiceTask();
    countdownServiceTask();
    // shutdownServiceTask(&buttonEvent);
    buttonEvent.id = E_BUTTON_NONE;
    buttonEvent.type = E_BUTTON_CLICK_TYPE_NONE;
    delay(10);
}


void factoryApp(ButtonEvent_t *buttonEvent) {
    static bool refresh = true;
    static FactoryState_t factoryState = E_FACTORY_STATE_INIT;
    static int64_t lastCountDownUpdate = esp_timer_get_time() / 1000;
    static int64_t lastCountDown = 5;

    int64_t currentMillisecond = esp_timer_get_time() / 1000;

    log_i("entering factoryApp()");

    if (refresh) {
        switch (factoryState) {
            case E_FACTORY_STATE_INIT:
                refresh = false;
                lcd.clear(TFT_BLACK);
                lcd.waitDisplay();
                lcd.clear(TFT_WHITE);
                lcd.waitDisplay();
                lcd.drawJpgFile(FILESYSTEM, "/init.jpg", 0, 0);
                lcd.drawString(String(lastCountDown), 86, 173, &fonts::FreeSansBold12pt7b);
                lcd.waitDisplay();

                factoryState = E_FACTORY_STATE_COUNTDOWN;
                refresh = true;
                break;

            case E_FACTORY_STATE_COUNTDOWN:
                if (currentMillisecond - lastCountDownUpdate > 1000) {
                    lastCountDown--;
                    lcd.drawString(String(lastCountDown), 86, 173, &fonts::FreeSansBold12pt7b);
                    lcd.waitDisplay();
                    if (lastCountDown == 0) {
                        lastCountDown = 5;
                        // The countdown is over, enter the main application
                        runMode = E_RUN_MODE_MAIN;
                    }
                    lastCountDownUpdate = currentMillisecond;
                }
                if (
                    buttonEvent->id == E_BUTTON_B
                    && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
                ) {
                    runMode = E_RUN_MODE_APSETTING;
                    factoryState = E_FACTORY_STATE_INIT;
                    refresh = true;
                }
            break;

            default:
                break;
        }
    }

}


void mainApp(ButtonEvent_t *buttonEvent) {
    static bool refresh = true;
    static int64_t lastMillisecond = esp_timer_get_time() / 1000;
    static int64_t lastCountDownUpdate = lastMillisecond;
    static int64_t lastCountDown = db.rtc.sleepInterval;
    static bool runingMqDataUpload = false;
    static int  mqDataUploadCount = MQDATA_UPLOAD_RETRY_COUNT;

    int64_t currentMillisecond = esp_timer_get_time() / 1000;

    if (
        (
            buttonEvent->id == E_BUTTON_A
            && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
        )
        || (
            buttonEvent->id == E_BUTTON_B
            && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
        )
    ) {
        runMode = (buttonEvent->id == E_BUTTON_A)
                  ? E_RUN_MODE_SETTING
                  : E_RUN_MODE_SETTING;
        refresh = true;
        return ;
    }

    if (
        lastCountDown == db.rtc.sleepInterval
        && WiFi.isConnected()
    ) {
        mqDataUploadCount = MQDATA_UPLOAD_RETRY_COUNT;
        runingMqDataUpload = true;
    }

    // Heat-up phase has passed, refresh data from sensors
    log_d("Heat-up time: %lld ms", (currentMillisecond - SensorInitTimestamp));
    if (runingMqDataUpload 
        && mqDataUploadCount == MQDATA_UPLOAD_RETRY_COUNT
        && dataTransferTimestamp == 0
        && (currentMillisecond - SensorInitTimestamp) / 1000 >= AIRQ_HEATUP_DELAY) {
        refresh = true;
    }

    if (refresh) {
        refresh = false;
        log_d("refresh");
        sensor.getSCD40MeasurementResult();
        sensor.getSEN55MeasurementResult();
        sensor.getSHT40MeasurementResult();
        sensor.getBMP280MeasurementResult();
        sensor.getBatteryVoltageRaw();
        sensor.getTimeString();
        sensor.getDateString();

        if (sensor.sht40.is_present) {
            statusView.updateSCD40(
                sensor.scd40.co2,
                sensor.sht40.temperature,
                sensor.sht40.humidity
            );
        } else {
            statusView.updateSCD40(
                sensor.scd40.co2,
                sensor.scd40.temperature,
                sensor.scd40.humidity
            );
        }
        statusView.updatePower(sensor.battery.raw);
        statusView.updateCountdown(db.rtc.sleepInterval);
        statusView.updateSEN55(
            sensor.sen55.massConcentrationPm1p0,
            sensor.sen55.massConcentrationPm2p5,
            sensor.sen55.massConcentrationPm4p0,
            sensor.sen55.massConcentrationPm10p0,
            sensor.sen55.ambientHumidity,
            sensor.sen55.ambientTemperature,
            sensor.sen55.vocIndex,
            sensor.sen55.noxIndex
        );
        statusView.updateTime(sensor.time.time, sensor.time.date);

        statusView.load();
        lastMillisecond = currentMillisecond;

    }

    if (currentMillisecond - lastCountDownUpdate > 1000) {
        lastCountDown--;
        statusView.displayCountdown(lastCountDown);
        if (lastCountDown == 0) {
            lastCountDown = db.rtc.sleepInterval;
            refresh = true;
        }
        lastCountDownUpdate = currentMillisecond;
    }

    // initiate data upload, if
    // - upload is pending (triggered further above)
    // - uptime is at least HEATUP_DELAY seconds (SEN55 warm-up)
    log_d("millis() = %lld", currentMillisecond);
    if (runingMqDataUpload
        && (currentMillisecond - SensorInitTimestamp) / 1000 > AIRQ_HEATUP_DELAY) {
        // and
        // - WiFi is connected
        // - retry counter has not exceeded
        if (WiFi.isConnected() 
            && mqDataUploadCount-- > 0) {
            log_d("triggering MQTT data publish");
            BUTTON_TONE();
            if (uploadSensorRawData()) {
                log_i("MQTT publish success");
                String msg = "OK";
                statusView.displayNetworkStatus("Upload", msg.c_str());
                SUCCESS_TONE();
                runingMqDataUpload = false;
                dataTransferTimestamp = currentMillisecond;
            } else {
                log_w("MQTT publish failed");
                String msg = "FAIL";
                statusView.displayNetworkStatus("Upload", msg.c_str());
                FAIL_TONE();
            }
        } else {
            log_d("MQTT publish failed after retries");
            dataTransferTimestamp = currentMillisecond;
            runingMqDataUpload = false;
        }
    }
}


void settingApp(ButtonEvent_t *buttonEvent) {
    static bool refresh = true;

    if (
        buttonEvent->id == E_BUTTON_A
        && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
    ) {
        runMode = E_RUN_MODE_MAIN;
        refresh = true;
        return;
    }

    if (
        buttonEvent->id == E_BUTTON_A
        && buttonEvent->type == E_BUTTON_CLICK_TYPE_PRESS
    ) {
        if (db.buzzer.onoff == true) {
            db.buzzer.onoff = false;
            ledcDetachPin(BUZZER_PIN);
        } else {
            db.buzzer.onoff = true;
            ledcAttachPin(BUZZER_PIN, 0);
            BUTTON_TONE();
        }
        refresh = true;
    }

    if (
        buttonEvent->id == E_BUTTON_B
        && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
    ) {
        runMode = E_RUN_MODE_APSETTING;
        refresh = true;
        return;
    }

    if (
        buttonEvent->id == E_BUTTON_B
        && buttonEvent->type == E_BUTTON_CLICK_TYPE_PRESS
    ) {
        factoryReset();
    }

    if (refresh) {
        if (db.buzzer.onoff == true) {
            lcd.clear(TFT_BLACK);
            lcd.waitDisplay();
            lcd.clear(TFT_WHITE);
            lcd.waitDisplay();
            lcd.drawJpgFile(FILESYSTEM, "/settings1.jpg", 0, 0);
            lcd.waitDisplay();
        } else {
            lcd.clear(TFT_BLACK);
            lcd.waitDisplay();
            lcd.clear(TFT_WHITE);
            lcd.waitDisplay();
            lcd.drawJpgFile(FILESYSTEM, "/settings.jpg", 0, 0);
            lcd.waitDisplay();
        }

        // Show additional information
#define YPOS_ID    131
#define YPOS_SWVER 145
#define YPOS_SSID  159
#define YPOS_IP    173
#define YPOS_INT   187
        // clear text window
        lcd.fillRect(0, YPOS_ID, 200, 200-YPOS_ID, TFT_WHITE);
        //
        // ID / MAC
        lcd.drawString("ID: " + mac, 0, YPOS_ID, &fonts::efontCN_14);
        lcd.waitDisplay();
        //
        // Software version
        lcd.drawString("SW: " GIT_COMMIT, 0, YPOS_SWVER, &fonts::efontCN_14);
        lcd.waitDisplay();
        //
        // SSID
        String ssid;
        if (db.wifi.ssid.length() == 0) {
            ssid="SSID: NOT SET";
        } else {
            ssid = "SSID: " + db.wifi.ssid;
        }
        splitLongString(ssid, 150, &fonts::efontCN_14);
        lcd.drawString(ssid, 0, YPOS_SSID, &fonts::efontCN_14);
        lcd.waitDisplay();
        //
        // Current IP address
        lcd.drawString("IP: " + WiFi.localIP().toString(), 0, YPOS_IP, &fonts::efontCN_14);
        lcd.waitDisplay();
        //
        // Interval
        String intervalString;
        _ctime(db.rtc.sleepInterval, intervalString);
        lcd.drawString("Interval: " + intervalString, 0, YPOS_INT, &fonts::efontCN_14);
        lcd.waitDisplay();

        refresh = false;
    }
}


void apSettingApp(ButtonEvent_t *buttonEvent) {
    static bool refresh = true;
    static APSettingState_t settingState = E_AP_SETTING_STATE_INIT;
    static int64_t lastMillisecond = esp_timer_get_time() / 1000;
    WiFiStatusEvent_t wifiStatusEvent;
    memset(&wifiStatusEvent, 0, sizeof(WiFiStatusEvent_t));

    String apQrcode = "WIFI:T:nopass;S:" + apSSID + ";P:;H:false;;";

    if (
        buttonEvent->id == E_BUTTON_A
        && buttonEvent->type == E_BUTTON_CLICK_TYPE_SINGLE
    ) {
        if (
            settingState == E_AP_SETTING_STATE_AP
            || settingState == E_AP_SETTING_STATE_WEB
        ) {
            WiFi.softAPdisconnect();
            // appWebServerClose();
            if (WiFi.isConnected() != true) {
                WiFi.disconnect();
                delay(200);
                WiFi.begin(db.wifi.ssid.c_str(), db.wifi.password.c_str());
            }
        }
        runMode = E_RUN_MODE_MAIN;
        settingState = E_AP_SETTING_STATE_INIT;
        refresh = true;
        return;
    }

    switch (settingState) {
        case E_AP_SETTING_STATE_INIT:
                wifiStartAP();
                // appWebServer();
                settingState = E_AP_SETTING_STATE_AP;
                db.isConfigState = false;
                refresh = true;
        break;

        case E_AP_SETTING_STATE_AP: {
            if (WiFi.softAPgetStationNum() > 0) {
                settingState = E_AP_SETTING_STATE_WEB;
                refresh = true;
            }
        }
        break;

        case E_AP_SETTING_STATE_WEB: {
            if (db.isConfigState == false && WiFi.softAPgetStationNum() == 0) {
                settingState = E_AP_SETTING_STATE_AP;
                refresh = true;
            }
            if (WiFi.isConnected() == true) {
                settingState = E_AP_SETTING_STATE_DONE;
                lastMillisecond = esp_timer_get_time() / 1000;
                refresh = true;
            } else if (WiFi.isConnected() == false && db.isConfigState == true) {
                settingState = E_AP_SETTING_STATE_WAIT;
                lastMillisecond = esp_timer_get_time() / 1000;
            }
        }
        break;

        case E_AP_SETTING_STATE_WAIT: {
            if (xQueueReceive(wifiStatusEventQueue, &wifiStatusEvent, (TickType_t)10) == pdTRUE) {
                if (
                    wifiStatusEvent.info.wifi_sta_disconnected.reason == 201
                    || wifiStatusEvent.info.wifi_sta_disconnected.reason == 15
                ) {
                    settingState = E_AP_SETTING_STATE_DONE;
                    lastMillisecond = esp_timer_get_time() / 1000;
                    refresh = true;
                    log_d("wifiStatusEventQueue receive success");
                    log_d("settingState set to E_AP_SETTING_STATE_DONE");
                }
            } else if (WiFi.isConnected() == true) {
                settingState = E_AP_SETTING_STATE_DONE;
                lastMillisecond = esp_timer_get_time() / 1000;
                refresh = true;
            } else if ((esp_timer_get_time() / 1000 - lastMillisecond) > WIFI_CONNECT_TIMEOUT * 1000) {
                settingState = E_AP_SETTING_STATE_DONE;
                lastMillisecond = esp_timer_get_time() / 1000;
                refresh = true;
                log_d("wifiStatusEventQueue receive timeout");
                log_d("settingState set to E_AP_SETTING_STATE_DONE");
            }
        }
        break;

        case E_AP_SETTING_STATE_DONE: {
            if (esp_timer_get_time() / 1000 - lastMillisecond > 1000) {
                runMode = E_RUN_MODE_MAIN;
                settingState = E_AP_SETTING_STATE_INIT;
                refresh = true;
                WiFi.softAPdisconnect();
                WiFi.begin(db.wifi.ssid.c_str(), db.wifi.password.c_str());
                // appWebServerClose();
                db.isConfigState = false;
                return ;
            }
        }
        break;

        default:
            break;
    }

    if (refresh) {
        switch (settingState)
        {
            case E_AP_SETTING_STATE_INIT:
            break;

            case E_AP_SETTING_STATE_AP:
                apQrcode = "WIFI:T:nopass;S:" + apSSID + ";P:;H:false;;";
                SUCCESS_TONE();
                lcd.clear(TFT_BLACK);
                lcd.waitDisplay();
                lcd.clear(TFT_WHITE);
                lcd.waitDisplay();
                lcd.drawJpgFile(FILESYSTEM, "/ap.jpg", 0, 0);
                lcd.qrcode(apQrcode, 35, 35, 130);
                lcd.drawString(apSSID, 66, 175, &fonts::FreeSansBold9pt7b);
                lcd.waitDisplay();
            break;

            case E_AP_SETTING_STATE_WEB:
                SUCCESS_TONE();
                lcd.clear(TFT_BLACK);
                lcd.waitDisplay();
                lcd.clear(TFT_WHITE);
                lcd.waitDisplay();
                lcd.drawJpgFile(FILESYSTEM, "/web.jpg", 0, 0);
                lcd.qrcode("http://192.168.4.1", 35, 35, 130);
                lcd.waitDisplay();
            break;

            case E_AP_SETTING_STATE_DONE:
                SUCCESS_TONE();
                lcd.clear(TFT_BLACK);
                lcd.waitDisplay();
                lcd.clear(TFT_WHITE);
                lcd.waitDisplay();
                lcd.drawJpgFile(FILESYSTEM, "/done.jpg", 0, 0);
                lcd.waitDisplay();

                lastMillisecond = esp_timer_get_time() / 1000;
            break;

            default:
            break;
        }
        refresh = false;
    }
}


void mqdataServiceTask() {
    static int64_t lastMillisecond = esp_timer_get_time() / 1000;

    if (
        WiFi.isConnected() == false
        || (esp_timer_get_time() / 1000) - lastMillisecond < 1000
    ) {
        return ;
    }

    log_i("mqdataServiceTask() ...");
    lastMillisecond = esp_timer_get_time() / 1000;

}

void networkStatusUpdateServiceTask() {

    NetworkStatusMsgEvent_t networkStatusMsgEvent;
    memset(&networkStatusMsgEvent, 0, sizeof(NetworkStatusMsgEvent_t));
    static String nickname = "";

    if (xQueueReceive(networkStatusMsgEventQueue, &networkStatusMsgEvent, (TickType_t)10) == pdTRUE) {
        // if (runMode == E_RUN_MODE_MAIN) {
        //     statusView.displayNetworkStatus(
        //         networkStatusMsgEvent.title, 
        //         networkStatusMsgEvent.content
        //     );
        // } else {
            statusView.updateNetworkStatus(
                networkStatusMsgEvent.title,
                networkStatusMsgEvent.content
            );
        // }
    }
    if (runMode == E_RUN_MODE_MAIN && nickname != db.nickname) {
        nickname = db.nickname;
        if (nickname.length() == 0) {
            nickname = "AirQ";
        }
        log_d("%s", db.nickname.c_str());
        statusView.displayNickname(nickname);
        nickname = db.nickname;
    }

}


void shutdownServiceTask(ButtonEvent_t *buttonEvent) {

    if (buttonEvent->id == E_BUTTON_POWER) {
        shutdown();
    }

}


void buttonTask(void *) {

    for (;;) {
        btnA.tick();
        btnB.tick();
        btnPower.tick();
        delay(10);
    }

    vTaskDelete(NULL);
}


void btnAClickEvent() {
    log_d("btnAClickEvent");

    BUTTON_TONE();

    ButtonEvent_t buttonEvent = { .id = E_BUTTON_A, .type = E_BUTTON_CLICK_TYPE_SINGLE };
    if (xQueueSendToBack(buttonEventQueue, &buttonEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("buttonEventQueue send Failed");
    }
}


void btnALongPressStartEvent() {
    log_d("btnALongPressStartEvent");

    BUTTON_TONE();

    ButtonEvent_t buttonEvent = { .id = E_BUTTON_A, .type = E_BUTTON_CLICK_TYPE_PRESS };
    if (xQueueSendToBack(buttonEventQueue, &buttonEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("buttonEventQueue send Failed");
    }
}


void btnBClickEvent() {
    log_d("btnBClickEvent");

    BUTTON_TONE();

    ButtonEvent_t buttonEvent = { .id = E_BUTTON_B, .type = E_BUTTON_CLICK_TYPE_SINGLE };
    if (xQueueSendToBack(buttonEventQueue, &buttonEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("buttonEventQueue send Failed");
    }
}


void btnBLongPressStartEvent() {
    log_d("btnBLongPressStartEvent");

    BUTTON_TONE();

    ButtonEvent_t buttonEvent = { .id = E_BUTTON_B, .type = E_BUTTON_CLICK_TYPE_PRESS };
    if (xQueueSendToBack(buttonEventQueue, &buttonEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("buttonEventQueue send Failed");
    }
}


void btnPowerClickEvent() {
    log_d("btnPowerClickEvent");

    BUTTON_TONE();

    ButtonEvent_t buttonEvent = { .id = E_BUTTON_POWER, .type = E_BUTTON_CLICK_TYPE_SINGLE };
    if (xQueueSendToBack(buttonEventQueue, &buttonEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("buttonEventQueue send Failed");
    }
}


void listDirectory(fs::FS &fs, const char * dirname, uint8_t levels) {
    log_i("Listing directory: %s", dirname);

    File root = fs.open(dirname);
    if(!root) {
        log_w("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        log_w(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            log_i("  DIR : %s", file.name());
            if(levels){
                listDirectory(fs, file.path(), levels -1);
            }
        } else {
            log_i("  FILE : %s\tSIZE: %d", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}


void wifiAPSTASetup() {
    log_i("WiFi setup...");

    WiFi.disconnect();
    delay(1000);

    log_i("WiFi: Set mode to WIFI_AP_STA");
    WiFi.mode(WIFI_AP_STA);
    WiFi.onEvent(onWiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (db.wifi.ssid.length() == 0) {
        log_w("SSID missing");
        statusView.updateNetworkStatus("WIFI", "no set");
    } else {
        statusView.updateNetworkStatus("WIFI", "......");
    }

    WiFi.begin(db.wifi.ssid.c_str(), db.wifi.password.c_str());

    log_i("Waiting for WiFi");

    mac = WiFi.macAddress();
    mac.toUpperCase();
    mac.replace(":", "");
    apSSID = "AirQ-" + mac.substring(6, 12);
    log_i("softAP MAC: %s", mac.c_str());
}


void wifiStartAP() {
    WiFi.softAPdisconnect();
    WiFi.disconnect();
    delay(200);
    if (WiFi.softAP(apSSID.c_str()) != true) {
        log_i("WiFi: failed to create softAP");
        return;
    }

    log_i("WiFi: softAP has been established");
    log_i("WiFi: please connect to the %s\r\n", apSSID.c_str());
}


void onWiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
    log_i("WiFi connected");
    log_i("IP address: %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());

    db.pskStatus = true;
    db.factoryState = false;
    db.saveToFile();

    sntp_servermode_dhcp(1);
    configTzTime(
        db.ntp.tz.c_str(),
        db.ntp.ntpServer0.c_str(),
        db.ntp.ntpServer1.c_str(),
        "pool.ntp.org"
    );

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1000)) {
        I2C_BM8563_TimeTypeDef I2C_BM8563_TimeStruct;
        I2C_BM8563_TimeStruct.hours = timeinfo.tm_hour;
        I2C_BM8563_TimeStruct.minutes = timeinfo.tm_min;
        I2C_BM8563_TimeStruct.seconds = timeinfo.tm_sec;
        bm8563.setTime(&I2C_BM8563_TimeStruct);

        I2C_BM8563_DateTypeDef I2C_BM8563_DateStruct;
        I2C_BM8563_DateStruct.year = 1900 + timeinfo.tm_year;
        I2C_BM8563_DateStruct.month = timeinfo.tm_mon + 1;
        I2C_BM8563_DateStruct.date = timeinfo.tm_mday;
        I2C_BM8563_DateStruct.weekDay = timeinfo.tm_wday;
        bm8563.setDate(&I2C_BM8563_DateStruct);
    }

    NetworkStatusMsgEvent_t networkStatusMsgEvent;
    memset(&networkStatusMsgEvent, 0, sizeof(NetworkStatusMsgEvent_t));
    memcpy(networkStatusMsgEvent.title, "WiFi", strlen("WiFi"));
    memcpy(networkStatusMsgEvent.content, "connect", strlen("connect"));

    if (xQueueSendToBack(networkStatusMsgEventQueue, &networkStatusMsgEvent, ( TickType_t ) 10 ) != pdPASS) {
        log_w("networkStatusMsgEventQueue send Failed");
    }
}


void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    log_w("WiFi lost connection. Reason: %d", info.wifi_sta_disconnected.reason);

    db.pskStatus = true;
    NetworkStatusMsgEvent_t networkStatusMsgEvent;
    memset(&networkStatusMsgEvent, 0, sizeof(NetworkStatusMsgEvent_t));

    WiFiStatusEvent_t wifiStatusEvent;
    memset(&wifiStatusEvent, 0, sizeof(WiFiStatusEvent_t));
    memcpy(&wifiStatusEvent.event, &event, sizeof(WiFiEvent_t));
    memcpy(&wifiStatusEvent.info, &info, sizeof(WiFiEventInfo_t));

    if (db.wifi.ssid.length() == 0) {
        memcpy(networkStatusMsgEvent.title, "WiFi", strlen("WiFi"));
        memcpy(networkStatusMsgEvent.content, "no set", strlen("no set"));
        if (xQueueSendToBack(networkStatusMsgEventQueue, &networkStatusMsgEvent, (TickType_t)10) != pdPASS) {
            log_w("networkStatusMsgEventQueue send Failed");
        }
    } else {
        if (info.wifi_sta_disconnected.reason == 201) {
            memcpy(networkStatusMsgEvent.title, "WiFi", strlen("WiFi"));
            memcpy(networkStatusMsgEvent.content, "no wifi", strlen("no wifi"));
            if (xQueueSendToBack(networkStatusMsgEventQueue, &networkStatusMsgEvent, (TickType_t)10) != pdPASS) {
                log_w("networkStatusMsgEventQueue send Failed");
            }
        } else if (info.wifi_sta_disconnected.reason == 15) {
            memcpy(networkStatusMsgEvent.title, "WiFi", strlen("WiFi"));
            memcpy(networkStatusMsgEvent.content, "pass ng", strlen("pass ng"));
            if (xQueueSendToBack(networkStatusMsgEventQueue, &networkStatusMsgEvent, (TickType_t)10) != pdPASS) {
                log_w("networkStatusMsgEventQueue send Failed");
            }
            db.pskStatus = false;
        }
        if (
            db.isConfigState == true
            && (
                info.wifi_sta_disconnected.reason == 201 // NO AP FOUND
                || info.wifi_sta_disconnected.reason == 15 // PSK ERROR
            )
        ) {
            if (xQueueSendToBack(wifiStatusEventQueue, &wifiStatusEvent, (TickType_t)10) != pdPASS) {
                log_w("wifiStatusEventQueue send Failed");
            }
        }
    }
}


bool uploadSensorRawData(void) {
    bool ret = false;
    cJSON *rspObject = NULL;
    cJSON *sen55Object = NULL;
    cJSON *scd40Object = NULL;
    cJSON *sht40Object = NULL;
    cJSON *bmp280Object = NULL;
    cJSON *rtcObject = NULL;
    cJSON *profileObject = NULL;
    cJSON *datetimeObject = NULL;

    char *buf = NULL;
    String data;

    time_t t = 0;

    /* build JSON structure */
    rspObject = cJSON_CreateObject();
    if (rspObject == NULL) {
        goto OUT1;
    }

    sen55Object = cJSON_CreateObject();
    if (sen55Object == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rspObject, "sen55", sen55Object);

    scd40Object = cJSON_CreateObject();
    if (scd40Object == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rspObject, "scd40", scd40Object);

    if (sensor.sht40.is_present && sensor.sht40.is_valid) {
        sht40Object = cJSON_CreateObject();
        if (sht40Object == NULL) {
            goto OUT;
        }
        cJSON_AddItemToObject(rspObject, "sht40", sht40Object);
        }

    if (sensor.bmp280.is_present && sensor.bmp280.is_valid) {
        bmp280Object = cJSON_CreateObject();
        if (bmp280Object == NULL) {
            goto OUT;
        }
        cJSON_AddItemToObject(rspObject, "bmp280", bmp280Object);
    }

    rtcObject = cJSON_CreateObject();
    if (rtcObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rspObject, "rtc", rtcObject);

    profileObject = cJSON_CreateObject();
    if (profileObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rspObject, "profile", profileObject);

    datetimeObject = cJSON_CreateObject();
    if (datetimeObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rspObject, "datetime", datetimeObject);


    /* fill data into structure */
    cJSON_AddNumberToObject(sen55Object, "pm1.0", sensor.sen55.massConcentrationPm1p0);
    cJSON_AddNumberToObject(sen55Object, "pm2.5", sensor.sen55.massConcentrationPm2p5);
    cJSON_AddNumberToObject(sen55Object, "pm4.0", sensor.sen55.massConcentrationPm4p0);
    cJSON_AddNumberToObject(sen55Object, "pm10.0", sensor.sen55.massConcentrationPm10p0);
    cJSON_AddNumberToObject(sen55Object, "humidity", sensor.sen55.ambientHumidity);
    cJSON_AddNumberToObject(sen55Object, "temperature", sensor.sen55.ambientTemperature);
    cJSON_AddNumberToObject(sen55Object, "voc", sensor.sen55.vocIndex);
    cJSON_AddNumberToObject(sen55Object, "nox", sensor.sen55.noxIndex);

    cJSON_AddNumberToObject(scd40Object, "co2", sensor.scd40.co2);
    cJSON_AddNumberToObject(scd40Object, "humidity", sensor.scd40.humidity);
    cJSON_AddNumberToObject(scd40Object, "temperature", sensor.scd40.temperature);

    if (sensor.sht40.is_present && sensor.sht40.is_valid) {
        cJSON_AddNumberToObject(sht40Object, "humidity", sensor.sht40.humidity);
        cJSON_AddNumberToObject(sht40Object, "temperature", sensor.sht40.temperature);
    }

    if (sensor.bmp280.is_present && sensor.bmp280.is_valid) {
        cJSON_AddNumberToObject(bmp280Object, "pressure", sensor.bmp280.pressure);
        cJSON_AddNumberToObject(bmp280Object, "temperature", sensor.bmp280.temperature);
    }

    cJSON_AddNumberToObject(rtcObject, "sleep_interval", db.rtc.sleepInterval);

    cJSON_AddStringToObject(profileObject, "nickname", db.nickname.c_str());
    cJSON_AddStringToObject(profileObject, "mac", mac.c_str());
    cJSON_AddStringToObject(profileObject, "wlan", WiFi.SSID().c_str());
    cJSON_AddNumberToObject(profileObject, "rssi", WiFi.RSSI());
    cJSON_AddStringToObject(profileObject, "IP",   WiFi.localIP().toString().c_str());
    cJSON_AddNumberToObject(profileObject, "Vbat", ((float)sensor.battery.raw / 1000) * 2);
    cJSON_AddStringToObject(profileObject, "version", APP_VERSION " " GIT_COMMIT);


    t = bm8563ToTime(bm8563);
    cJSON_AddNumberToObject(datetimeObject, "ts", t);

    buf = cJSON_PrintUnformatted(rspObject);
    data = buf;
    data.replace("\"", "\\\"");

    // publish to MQTT
    mqdata.connect();
    ret=mqdata.publish(buf);
    mqdata.disconnect();

OUT:
    free(buf);
    cJSON_Delete(rspObject);
OUT1:
    return ret;
}


time_t bm8563ToTime(I2C_BM8563 &bm8563) {
    I2C_BM8563_TimeTypeDef I2C_BM8563_TimeStruct;
    bm8563.getTime(&I2C_BM8563_TimeStruct);
    I2C_BM8563_DateTypeDef I2C_BM8563_DateStruct;
    bm8563.getDate(&I2C_BM8563_DateStruct);
    struct tm tm = {
        .tm_sec = I2C_BM8563_TimeStruct.seconds,
        .tm_min = I2C_BM8563_TimeStruct.minutes,
        .tm_hour = I2C_BM8563_TimeStruct.hours,
        .tm_mday = I2C_BM8563_DateStruct.date,
        .tm_mon = I2C_BM8563_DateStruct.month - 1,
        .tm_year = (int)(I2C_BM8563_DateStruct.year - 1900),
    };

    /**
     * The time obtained by BM8563 is the time in the current time zone.
     * Use the mktime function to convert the time into a timestamp, and you
     * need to eliminate the difference caused by the time zone.
     */
    configTzTime(
        "GMT0",
        db.ntp.ntpServer0.c_str(),
        db.ntp.ntpServer1.c_str(),
        "pool.ntp.org"
    );
    time_t time = mktime(&tm);
    configTzTime(
        db.ntp.tz.c_str(),
        db.ntp.ntpServer0.c_str(),
        db.ntp.ntpServer1.c_str(),
        "pool.ntp.org"
    );
    return time;
}


void countdownServiceTask() {
    static uint32_t cur = esp_timer_get_time() / 1000;

    // don't shutdown until measurement has been taken and transmitted
    log_d("dataTransferTimestamp = %lld", dataTransferTimestamp);

    if (runMode == E_RUN_MODE_MAIN) {
//        if (esp_timer_get_time() / 1000 - cur > AIRQ_SHUTDOWN_TIMEOUT * 1000) {
        log_d("now - dataTransferTimestamp = %lld", esp_timer_get_time() / 1000 - dataTransferTimestamp);

        if (dataTransferTimestamp > 0
            && esp_timer_get_time() / 1000 - dataTransferTimestamp > AIRQ_SHUTDOWN_TIMEOUT * 1000) {
            shutdown();
        }

        if (dataTransferTimestamp == 0
            && esp_timer_get_time() / 1000 - cur > (2*AIRQ_SHUTDOWN_TIMEOUT + AIRQ_HEATUP_DELAY) * 1000) {
            shutdown();
        }
    } else {
        cur = esp_timer_get_time() / 1000;
    }

}


void shutdown() {
    int64_t wakeup_interval;
    int64_t millis_exec_time;
    time_t timestamp = bm8563ToTime(bm8563);
    log_i("BM8653 timestamp: %ld", timestamp);

    // scd4x.powerDown();
    // digitalWrite(SEN55_POWER_EN, HIGH);
    // lcd.powerSaveOn();
    lcd.sleep();
    lcd.waitDisplay();
    // delay(2000);
    if (db.factoryState == false) {
        bm8563.clearIRQ();
        bm8563.SetAlarmIRQ(db.rtc.sleepInterval);
    }

    log_i("shutdown");
    delay(10);
    digitalWrite(POWER_HOLD, LOW);

    // stop SEN55 -> got to idle mode
    log_i("Command SEN55 to idle mode (turn off Fan)");
    //uint16_t error = sen5x.stopMeasurement();
    // maybe better: should keep NOx and VOC measurements going.
    // otherwise they will be 0 when coming out of idle mode
    uint16_t error = sen5x.startMeasurementWithoutPm();
    if (error) {
        char errorMessage[256];
        errorToString(error, errorMessage, 256);
        log_w("Error trying to execute stopMeasurement(): %s", errorMessage);
    }

    lcd.wakeup();
    lcd.waitDisplay();
    log_i("USB powered, continue to operate");
    digitalWrite(POWER_HOLD, HIGH);
    delay(10);
    gpio_hold_en((gpio_num_t)SEN55_POWER_EN);
    gpio_deep_sleep_hold_en();

    // properly calculate wakeup time (subtract the execution time)
    millis_exec_time=esp_timer_get_time() / 1000;
    wakeup_interval = (db.rtc.sleepInterval * 1000) - millis_exec_time;
    if (wakeup_interval < 20000) { wakeup_interval = 20000; }
    if (wakeup_interval > (db.rtc.sleepInterval * 1000)) { wakeup_interval = (db.rtc.sleepInterval * 1000); }
    log_i("calculated wakeup interval: %lld ms (exec time: %lld ms)", wakeup_interval, millis_exec_time);
    //esp_sleep_enable_timer_wakeup(db.rtc.sleepInterval * 1000000);
    esp_sleep_enable_timer_wakeup(wakeup_interval * 1000);
    esp_deep_sleep_start();
}


void splitLongString(String &text, int32_t maxWidth, const lgfx::IFont* font) {
    int32_t w = lcd.textWidth(text, font);
    int32_t start = 1;
    int32_t end = 0;
    if (w < maxWidth) {
        return ;
    }

    w = lcd.textWidth("...", font);
    for (;;) {
        int32_t ww = lcd.textWidth(text.substring(0, end), font);
        ww = lcd.textWidth(text.substring(0, end), font);
        if (ww > (maxWidth / 2 - w)) {
            end -= 1;
            break;
        }
        end += 1;
    }

    start = end;
    for (;;) {
        int32_t ww = lcd.textWidth(text.substring(start, -1), font);
        if (ww < (maxWidth / 2 - w)) {
            start += 1;
            break;
        }
        start += 1;
    }

    text = text.substring(0, end) + "..." + text.substring(start);
}


void factoryReset() {
    log_i("factory reset ...");
    File sourceFile = FILESYSTEM.open("/db.backup", "r");
    File targetFile = FILESYSTEM.open("/db.json", "w");

    while (sourceFile.available()) {
        char data = sourceFile.read();
        targetFile.write(data);
    }

    sourceFile.close();
    targetFile.close();

    lcd.clear(TFT_BLACK);
    lcd.waitDisplay();
    lcd.clear(TFT_WHITE);
    lcd.waitDisplay();
    lcd.sleep();
    lcd.waitDisplay();

    ESP.restart();
}


void _ctime(uint32_t seconds, String &text) {
    int remainingSeconds = 0;
    uint32_t h = 0;
    uint32_t m = 0;
    uint32_t s = 0;

    h = seconds / 3600;
    remainingSeconds = seconds % 3600;
    m = remainingSeconds / 60;
    s = remainingSeconds % 60;
    text = "";
    if (h != 0) {
        text += String(h) + "H";
    }
    if (h != 0 || m != 0 || (h != 0 && s != 0)) {
        text += String(m) + "M";
    }
    if (s != 0) {
        text += String(s) + "S";
    }
}
