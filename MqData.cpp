
#include <Arduino.h>
#include <stdio.h>
#include <stdint.h>
#include <ArduinoJson.h>

#include <WiFi.h>
#include <PubSubClient.h>

#include "MqData.hpp"

WiFiClient net;
PubSubClient mqtt_client;


MqData::MqData() {
	bool sts;

	mqtt_client.setClient(net);

	sts = mqtt_client.setBufferSize(MQTT_BUFFER);
	if (sts != true) {
		log_e("unable to alloate MQTT buffer");
	}
}

MqData::~MqData() {}

void MqData::setDeviceToken(const String &dev_token) {
    _device_token = dev_token;
    _public = true;
    log_d("%s", _device_token.c_str());
}


void MqData::setConfig(const char *username, const char *password, const char *host, 
                       int port, const char *topic_prefix, const char *mac) {
	int c;

	_username = username;
	_password = password;
	_host = host;
	_port = port;

	/* calculate clientid string */
	if (_clientid) { free(_clientid); }
	const char *clientid_prefix="ClientID-";
	c=strlen(clientid_prefix) + strlen(mac) + 1;
	_clientid=(char *)malloc(c);
	snprintf(_clientid, c, "%s%s", clientid_prefix, mac);
	_clientid[c]='\0';

	/* calculate topic string */
	if (_topic) { free(_topic); }
	c=strlen(topic_prefix) + 1 + strlen(mac) + 1;
	_topic=(char *)malloc(c);
	snprintf(_topic, c, "%s/%s", topic_prefix, mac);
	_topic[c]='\0';

	log_i("MQTT Config: user=%s, pass=%s, host=%s, port=%d",
		_username, _password, _host, _port);
	log_i("MQTT Config: clientid=[%s]", _clientid);
	log_i("MQTT Config: topic=[%s]", _topic);

	return;
}


void MqData::setConfig2(const char *username, const char *password, const char *host, 
                       int port) {
	int c;

	_username2 = username;
	_password2 = password;
	_host2 = host;
	_port2 = port;

	return;
}

bool MqData::connect() {
	uint8_t c=0;
	if (!WiFi.isConnected()) {
		log_e("WiFi not connected");
		return false;
	}

	mqtt_client.setServer(_host, _port);
	log_d("MQTT trying to connect to %s:%d [%s/%s]...", _host, _port, _username, _password);
	log_d("  ClientID: %s", _clientid);
	while ((c++ < 3) && (!mqtt_client.connect(_clientid, _username, _password))) {
		delay(1000);
		log_d("MQTT re-trying to connect...");
	}

	// fallback to second MQTT server
	if (!mqtt_client.connected() && _host2 && (strlen(_host2) > 0)) {
		log_d("switching to 2nd MQTT server");
		mqtt_client.setServer(_host2, _port2);

		log_d("MQTT trying to connect to %s:%d [%s/%s]...", _host2, _port2, _username2, _password2);
		log_d("  ClientID: %s", _clientid);
		c=0;
		while ((c++ < 3) && (!mqtt_client.connect(_clientid, _username2, _password2))) {
			delay(1000);
			log_d("MQTT re-trying to connect...");
		}
	}

	if (!mqtt_client.connected()) {
		log_e("MQTT connecion failed");
		return false;
	}

	log_i("Connection established");
	return true;
}


bool MqData::publish(char *buf) {
	bool sts;
	log_d("MQTT Publish: [%s] => %s", _topic, buf);
	log_d("buffersize=%d", strlen(buf));
	sts = mqtt_client.publish(_topic, buf);
	return  sts;
}


bool MqData::disconnect() {
	log_i("MQTT disconnecting...");
	mqtt_client.disconnect();
	return true;
}





