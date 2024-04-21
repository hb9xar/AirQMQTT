#ifndef _MQDATA_H_
#define _MQDATA_H_

#include <WString.h>
#include <type_traits>

# define MQTT_BUFFER	768

class MqData
{
public:
	MqData();
	~MqData();

	void setDeviceToken(const String &dev_token);

	void setConfig(const char *username, const char *password, const char *host, 
	               int port, const char *topic_prefix, const char *mac);
	bool connect();
	bool publish(char *buf);
	bool disconnect();

private:

	bool _public = false;
	String _device_token = "";
	const char *_username;
	const char *_password;
	const char *_host;
	int   _port;
	char *_topic;
};

bool login(const String &loginName, const String &password, String &deviceToken);
bool disconnect(void);
bool transmit(void);



#endif
