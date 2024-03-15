#include "MqData.hpp"

#include <Arduino.h>
#include <stdint.h>
#include <ArduinoJson.h>


MqData::MqData() {}

MqData::~MqData() {}

void MqData::setDeviceToken(const String &dev_token) {
    _device_token = dev_token;
    _public = true;
    log_d("%s", _device_token.c_str());
}


void MqData::setConfig(const char *username, const char *password, const char *host, int port) {
	log_i("MQTT Config: user=%s, pass=%s, host=%s, port=%d",
		username, password, host, port);
	_username = username;
	_password = password;
	_host = host;
	_port = port;
}


bool MqData::connect() {


	return true;
}

bool MqData::publish() {
	return true;
}

bool MqData::disconnect() {
	return true;
}



bool MqData::setValue(char *key, float value) {
    if (_public) {
        return false;
    }
    char buf[256] = { 0 };
    snprintf(
        buf,
        sizeof(buf) - 1,
        "{\"dataType\": \"double\", \"name\": \"%s\", \"permissions\": \"1\", \"value\": %f}",
        key,
        value
    );
    //return _set((uint8_t *)buf, strlen(buf));
    return true;
}



// connect to broker

// publish data

// disconnect from broker

