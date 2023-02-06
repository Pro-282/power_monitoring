#pragma once

#include <Wire.h>

// DS1307 RTC I2C address
#define DS1307_I2C_ADDRESS    (0xD0 >> 1)

bool readBuffer(uint8_t reg, void *bufferRead, uint8_t readLen)
{
    Wire.beginTransmission((uint8_t)DS1307_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        memset(bufferRead, 0, readLen);
        return false;
    }
    Wire.requestFrom((uint8_t)DS1307_I2C_ADDRESS, readLen);
    for (uint8_t i = 0; i < readLen; i++) {
        ((uint8_t *)bufferRead)[i] = (uint8_t)Wire.read();
    }
    return true;
}

bool writeBuffer(uint8_t reg, const void *bufferWrite, uint8_t writeLen)
{
    Wire.beginTransmission(DS1307_I2C_ADDRESS);
    Wire.write(reg);
    for (uint8_t i = 0; i < writeLen; i++) {
        Wire.write(((uint8_t *)bufferWrite)[i]);
    }
    if (Wire.endTransmission() != 0) {
        return false;
    }
    return true;
}

uint8_t bcdToDec(uint8_t bcd)
{
    return (uint8_t)(10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
}

uint8_t decToBcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

void setDateTime(uint8_t hour, uint8_t min, uint8_t sec,
                 uint8_t mday, uint8_t mon, uint16_t year,
                 uint8_t wday)
{
    uint8_t buffer[7];

    Serial.print(F("Set date/time..."));

    // Encode date time from decimal to BCD
    buffer[0] = decToBcd(sec) & 0x7F; // CH bit cleared in seconds register
    buffer[1] = decToBcd(min) & 0x7F;
    buffer[2] = decToBcd(hour) & 0x3F;
    buffer[3] = decToBcd(wday) & 0x07;
    buffer[4] = decToBcd(mday) & 0x3F;
    buffer[5] = decToBcd(mon) & 0x1F;
    buffer[6] = decToBcd(year % 100);

    // Write BCD encoded buffer to RTC registers
    if (!writeBuffer(0x00, buffer, sizeof(buffer))) {
        Serial.println(F("Failed"));
    } else {
        Serial.println(F("OK"));
    }
}

bool getDateTime(uint8_t *hour, uint8_t *min, uint8_t *sec,
                 uint8_t *mday, uint8_t *mon, uint16_t *year,
                 uint8_t *wday)
{
    uint8_t buffer[7];

    // Read time registers
    if (!readBuffer(0x00, buffer, sizeof(buffer))) {
        Serial.println(F("Get date/time failed"));
        return false;
    }

    // BCD decode time registers
    *sec = bcdToDec(buffer[0] & 0x7F);
    *min = bcdToDec(buffer[1] & 0x7F);
    *hour = bcdToDec(buffer[2] & 0x3F);
    *wday = bcdToDec(buffer[3] & 0x07);
    *mday = bcdToDec(buffer[4] & 0x3F);
    *mon = bcdToDec(buffer[5] & 0x1f);
    *year = bcdToDec(buffer[6]) + 100; // 2000-1900

    // Check buffer for valid data
    if ((*sec > 59) || (*min > 59) || (*hour > 23) ||
        (*mday < 1) || (*mday > 31) || (*mon > 12) || (*year > 199) ||
        (*wday > 7))
    { return false; }

    return true;
}

void date_time_to_string(std::string *date_time_string)
{
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t mday;
    uint8_t mon;
    uint16_t year;
    uint8_t wday;

    if (!getDateTime(&hour, &min, &sec, &mday, &mon, &year, &wday)) {
        Serial.println(F("Read date/time failed"));
        return;
    }
    *date_time_string = std::to_string(year + 1900) + "-";
    if(mon < 10) *date_time_string += "0";
    *date_time_string += std::to_string(mon) + "-";
    if(mday < 10) *date_time_string += "0";
    *date_time_string += std::to_string(mday) + " ";
    if(hour < 10) *date_time_string += "0";
    *date_time_string += std::to_string(hour) + ":";
    if(min < 10) *date_time_string += "0";
    *date_time_string += std::to_string(min);
}

void get_current_month_year(uint8_t *mm, uint16_t *yy)
{
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t mday;
    uint8_t mon;
    uint16_t year;
    uint8_t wday;

    if (!getDateTime(&hour, &min, &sec, &mday, &mon, &year, &wday)) {
        Serial.println(F("Read date/time failed"));
        return;
    }
    *mm = mon; *yy = year;
}