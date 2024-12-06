#pragma once

#include <WString.h>


class DataBase {
public:
    DataBase() {}
    ~DataBase() {}

    void saveToFile();
    void dump();
    void loadFromFile();

public:
    bool factoryState;
    struct {
        String ssid;
        String password;
    } wifi;
    struct {
        int sleepInterval;
    } rtc;
    struct {
        String ntpServer0;
        String ntpServer1;
        String tz;
    } ntp;
    struct {
        String server;
        int port;
        String username;
        String password;
        String topicPrefix;
    } mqdata;
    struct {
        String server;
        int port;
        String username;
        String password;
    } mqdata2;
    struct {
        bool onoff;
    } buzzer;

    String nickname;

    // 不需要保存到文件
    bool isConfigState;
    bool pskStatus = true;
};


extern DataBase db;
