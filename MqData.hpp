#ifndef _MQDATA_H_
#define _MQDATA_H_

#include <WString.h>
#include <type_traits>

# define MQTT_BUFFER	1024

class MqData
{
public:
	MqData();
	~MqData();

	void setDeviceToken(const String &dev_token);

	void setConfig(const char *username, const char *password, const char *host, 
	               int port, const char *topic_prefix, const char *mac);
	void setConfig2(const char *username, const char *password, const char *host, 
	               int port);

	bool connect();
	bool publish(char *buf);
	bool disconnect();

private:
	bool _public = false;
	String _device_token = "";

	const char *_username = NULL;
	const char *_password = NULL;
	const char *_host = NULL;
	int   _port = 0;

	const char *_username2 = NULL;
	const char *_password2 = NULL;
	const char *_host2 = NULL;
	int   _port2 = 0;

	char *_topic = NULL;
	char *_clientid = NULL;
};

bool login(const String &loginName, const String &password, String &deviceToken);
bool disconnect(void);
bool transmit(void);



#endif
