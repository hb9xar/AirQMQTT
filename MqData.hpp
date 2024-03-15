#ifndef _MQDATA_H_
#define _MQDATA_H_

#include <WString.h>
#include <type_traits>

class MqData
{
public:
	MqData();
	~MqData();

	void setDeviceToken(const String &dev_token);

	void setConfig(const char *username, const char *password, const char *host, int port);
	bool connect();
	bool publish();
	bool disconnect();

	bool setValue(char *key, float value);
//	bool setValue(char *key, double value);
//	bool setValue(char *key, int value);

private:
	bool _public = false;
	String _device_token = "";
	const char *_username;
	const char *_password;
	const char *_host;
	int   _port;
	//&&&String _key = "";
};

bool login(const String &loginName, const String &password, String &deviceToken);
bool disconnect(void);
bool transmit(void);



#endif
