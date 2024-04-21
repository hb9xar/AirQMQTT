#pragma once

#include <stdint.h>
#include "I2C_BM8563.h"
#include <SensirionI2CScd4x.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2cSht4x.h>
#include <SparkFunBME280.h>


class Sensor
{
public:
    Sensor(SensirionI2CScd4x &scd4x, 
           SensirionI2CSen5x &sen5x, 
           SensirionI2cSht4x &sht40, 
           BME280 &bmp280,
           I2C_BM8563 &bm8563):
        _scd4x(scd4x),
        _sen5x(sen5x),
        _sht40(sht40),
        _bmp280(bmp280),
        _bm8563(bm8563) {}

    ~Sensor() {}

    bool getSCD40MeasurementResult();
    bool getSEN55MeasurementResult();
    bool getSHT40MeasurementResult();
    bool getBMP280MeasurementResult();
    void getBatteryVoltageRaw();
    void getTimeString();
    void getDateString();

public:
   struct {
        float massConcentrationPm1p0;
        float massConcentrationPm2p5;
        float massConcentrationPm4p0;
        float massConcentrationPm10p0;
        float ambientHumidity;
        float ambientTemperature;
        float vocIndex;
        float noxIndex;
    } sen55;

    struct {
        uint16_t co2;
        float temperature;
        float humidity;
    } scd40;

    struct {
        bool is_present;
        bool is_valid;
        float temperature;
        float humidity;
    } sht40;

    struct {
        bool is_present;
        bool is_valid;
        float temperature;
        float pressure;		// real pressure (not normalized @0m asl)
    } bmp280;

    struct {
        uint32_t raw;
    } battery;

    struct {
        char time[10];
        char date[17];
    } time;

private:
    SensirionI2CScd4x &_scd4x;
    SensirionI2CSen5x &_sen5x;
    SensirionI2cSht4x &_sht40;
    BME280 &_bmp280;
    I2C_BM8563 &_bm8563;

    char _errorMessage[256];
};

extern Sensor sensor;
