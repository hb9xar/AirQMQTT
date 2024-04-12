#include "DataBase.hpp"
#include "config.h"

#include <cJSON.h>
#include <LittleFS.h>


void DataBase::saveToFile() {
    cJSON *rootObject = NULL;
    cJSON *configObject = NULL;
    cJSON *wifiObject = NULL;
    cJSON *rtcObject = NULL;
    cJSON *ntpObject = NULL;
    cJSON *mqdataObject = NULL;
    cJSON *buzzerObject = NULL;
    File configfile;
    char *str = NULL;

    rootObject = cJSON_CreateObject();
    if (rootObject == NULL) {
        goto OUT;
    }

    configObject = cJSON_CreateObject();
    if (configObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(rootObject, "config", configObject);

    cJSON_AddBoolToObject(configObject, "factory_state", factoryState);

    wifiObject = cJSON_CreateObject();
    if (wifiObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(configObject, "wifi", wifiObject);
    cJSON_AddStringToObject(wifiObject, "ssid", wifi.ssid.c_str());
    cJSON_AddStringToObject(wifiObject, "password", wifi.password.c_str());

    rtcObject = cJSON_CreateObject();
    if (rtcObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(configObject, "rtc", rtcObject);
    cJSON_AddNumberToObject(rtcObject, "sleep_interval", rtc.sleepInterval);

    ntpObject = cJSON_CreateObject();
    if (ntpObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(configObject, "ntp", ntpObject);
    cJSON_AddStringToObject(ntpObject, "server_0", ntp.ntpServer0.c_str());
    cJSON_AddStringToObject(ntpObject, "server_1", ntp.ntpServer1.c_str());
    cJSON_AddStringToObject(ntpObject, "tz", ntp.tz.c_str());

    mqdataObject = cJSON_CreateObject();
    if (mqdataObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(configObject, "mqdata", mqdataObject);
    cJSON_AddStringToObject(mqdataObject, "server", mqdata.server.c_str());
    cJSON_AddNumberToObject(mqdataObject, "port", mqdata.port);
    cJSON_AddStringToObject(mqdataObject, "username", mqdata.username.c_str());
    cJSON_AddStringToObject(mqdataObject, "password", mqdata.password.c_str());
    cJSON_AddStringToObject(mqdataObject, "topicPrefix", mqdata.topicPrefix.c_str());

    buzzerObject = cJSON_CreateObject();
    if (buzzerObject == NULL) {
        goto OUT;
    }
    cJSON_AddItemToObject(configObject, "buzzer", buzzerObject);
    cJSON_AddBoolToObject(buzzerObject, "mute", buzzer.onoff);

    cJSON_AddStringToObject(configObject, "nickname", nickname.c_str());

    configfile = FILESYSTEM.open("/db.json", FILE_WRITE);
    str = cJSON_Print(rootObject);
    configfile.write((const uint8_t *)str, strlen(str));
    configfile.close();

OUT:
    free(str);
    cJSON_Delete(rootObject);
    return;
}


void DataBase::dump() {
    log_d("config:");
    log_d("  factory_state: %d", factoryState);

    log_d("  wifi:");
    log_d("    ssid: %s", wifi.ssid.c_str());
    log_d("    password: %s", wifi.password.c_str());

    log_d("  rtc:");
    log_d("    sleep_interval: %d", rtc.sleepInterval);

    log_d("  ntp:");
    log_d("    server_0: %s", ntp.ntpServer0.c_str());
    log_d("    server_1: %s", ntp.ntpServer1.c_str());
    log_d("    tz: %s", ntp.tz.c_str());

    struct {
        String server;
        int port;
        String username;
        String password;
        String topicPrefix;
    } mqdata;
    log_d("  mqdata:");
    log_d("    server: %s:%d", mqdata.server.c_str(), mqdata.port);
    log_d("    username: %s", mqdata.username.c_str());
    log_d("    password: %s", mqdata.password.c_str());
    log_d("    topicPrefix: %s", mqdata.topicPrefix.c_str());

    log_d("  buzzer:");
    log_d("    onoff: %d", buzzer.onoff);

    log_d("  nickname: %s", nickname.c_str());
}


void DataBase::loadFromFile(void) {
    log_i("Load DateBase...");

    File dbfile = FILESYSTEM.open("/db.json", "r");
    if (!dbfile) {
        log_i("Error opening file.");
        return;
    }

    char *buffer = (char *)malloc(dbfile.size());
    size_t buffer_len = dbfile.size();

    while (dbfile.available()) {
        dbfile.readBytes(buffer, dbfile.size());
    }
    dbfile.close();

    cJSON *rootObject = cJSON_ParseWithLength(buffer, buffer_len);
    if (rootObject == NULL) {
        log_i("Error opening file.");
        return;
    }

    cJSON *configObject = cJSON_GetObjectItem(rootObject, "config");
    cJSON *wifiObject = cJSON_GetObjectItem(configObject, "wifi");
    cJSON *ssidObject = cJSON_GetObjectItem(wifiObject, "ssid");
    cJSON *pskObject = cJSON_GetObjectItem(wifiObject, "password");
    wifi.ssid = String(ssidObject->valuestring);
    wifi.password = String(pskObject->valuestring);

    cJSON *factoryStateObject = cJSON_GetObjectItem(configObject, "factory_state");
    factoryState = cJSON_IsTrue(factoryStateObject);

    cJSON *rtcObject = cJSON_GetObjectItem(configObject, "rtc");
    cJSON *sleepIntervalObject = cJSON_GetObjectItem(rtcObject, "sleep_interval");
    rtc.sleepInterval = sleepIntervalObject->valueint;

    cJSON *ntpObject = cJSON_GetObjectItem(configObject, "ntp");
    cJSON *server0Object = cJSON_GetObjectItem(ntpObject, "server_0");
    cJSON *server1Object = cJSON_GetObjectItem(ntpObject, "server_1");
    cJSON *tzObject = cJSON_GetObjectItem(ntpObject, "tz");
    ntp.ntpServer0 = String(server0Object->valuestring);
    ntp.ntpServer1 = String(server1Object->valuestring);
    ntp.tz = String(tzObject->valuestring);

    cJSON *mqdataObject = cJSON_GetObjectItem(configObject, "mqdata");
    cJSON *serverObject = cJSON_GetObjectItem(mqdataObject, "server");
    cJSON *portObject = cJSON_GetObjectItem(mqdataObject, "port");
    cJSON *usernameObject = cJSON_GetObjectItem(mqdataObject, "username");
    cJSON *passwordObject = cJSON_GetObjectItem(mqdataObject, "password");
    cJSON *topicPrefixObject = cJSON_GetObjectItem(mqdataObject, "topicPrefix");
    mqdata.server = String(serverObject->valuestring);
    mqdata.port = portObject->valueint;
    mqdata.username = String(usernameObject->valuestring);
    mqdata.password = String(passwordObject->valuestring);
    mqdata.topicPrefix = String(topicPrefixObject->valuestring);

    cJSON *buzzerObject = cJSON_GetObjectItem(configObject, "buzzer");
    if (cJSON_IsTrue(cJSON_GetObjectItem(buzzerObject, "mute"))) {
        buzzer.onoff = true;
    } else {
        buzzer.onoff = false;
    }

    cJSON *nicknameObject = cJSON_GetObjectItem(configObject, "nickname");
    if (nicknameObject) {
        nickname = String(nicknameObject->valuestring);
    }

    cJSON_Delete(rootObject);
    free(buffer);
}


DataBase db;
