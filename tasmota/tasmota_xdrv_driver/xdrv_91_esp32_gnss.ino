/*
  xdrv_91_esp32_gnss.ino - GNDSS with NMEA support
  Copyright (C) 2025 Catalin Sanda
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#ifdef USE_GNSS

#include <TinyGPSPlus.h>

#define XDRV_91             91
#define D_CMND_GNSS "GNSS"

struct {
  uint32_t baudrate;
  uint8_t serial_config;
  uint8_t valid;
  float latitude;
  float longitude;
  float altitude;
  uint8_t satellites;
  float hdop;
  char time[32];
  char date[32];
} GNSSData;

TinyGPSPlus gps;
HardwareSerial *GNSSSerial = nullptr;

const char kGNSSCommands[] PROGMEM = D_CMND_GNSS "|"
  "Baudrate|"        // Set UART baudrate
  "SerialConfig";    // Set UART configuration

void CmndGNSSBaudrate(void) {
  if ((XdrvMailbox.payload >= 1200) && (XdrvMailbox.payload <= 115200)) {
    GNSSData.baudrate = XdrvMailbox.payload;
    GNSSInit();  // Reinitialize with new baudrate
  }
  ResponseCmndNumber(GNSSData.baudrate);
}

void CmndGNSSSerialConfig(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 23)) {
    GNSSData.serial_config = XdrvMailbox.payload;
    GNSSInit();  // Reinitialize with new config
  }
  ResponseCmndNumber(GNSSData.serial_config);
}

void (* const GNSSCommand[])(void) PROGMEM = {
  &CmndGNSSBaudrate,
  &CmndGNSSSerialConfig
};

/*********************************************************************************************/

void GNSSInit(void) {
  if (!GNSSData.baudrate) {
    GNSSData.baudrate = 9600;  // Default baudrate for NMEA
  }
  GNSSData.valid = 0;
  
  GNSSSerial = new HardwareSerial(1);
  if (PinUsed(GPIO_GNSS_NMEA_RX) && PinUsed(GPIO_GNSS_NMEA_TX)) {
    GNSSSerial->begin(GNSSData.baudrate, SERIAL_8N1, Pin(GPIO_GNSS_NMEA_RX), Pin(GPIO_GNSS_NMEA_TX));
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Serial initialized at %d baud"), GNSSData.baudrate);
  }
}

void GNSSEvery100ms(void) {
  while (GNSSSerial && GNSSSerial->available() > 0) {
    if (gps.encode(GNSSSerial->read())) {
      GNSSData.valid = gps.location.isValid() && gps.date.isValid() && gps.time.isValid();
      
      if (GNSSData.valid) {
        GNSSData.latitude = gps.location.lat();
        GNSSData.longitude = gps.location.lng();
        GNSSData.altitude = gps.altitude.meters();
        GNSSData.satellites = gps.satellites.value();
        GNSSData.hdop = gps.hdop.hdop();
        
        snprintf_P(GNSSData.time, sizeof(GNSSData.time), PSTR("%02d:%02d:%02d"),
          gps.time.hour(), gps.time.minute(), gps.time.second());
        snprintf_P(GNSSData.date, sizeof(GNSSData.date), PSTR("%04d-%02d-%02d"),
          gps.date.year(), gps.date.month(), gps.date.day());
          
        // Update Tasmota's internal time if we have a valid fix
        if (gps.time.isValid() && gps.date.isValid()) {
          TIME_T tm;
          tm.year = gps.date.year() - 1970;
          tm.month = gps.date.month();
          tm.day_of_month = gps.date.day();
          tm.hour = gps.time.hour();
          tm.minute = gps.time.minute();
          tm.second = gps.time.second();
          uint32_t epoch = MakeTime(tm);
          RtcSetTime(epoch);
          AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: Time sync %s"), GNSSData.time);
        }
      }
    }
  }
}

void GNSSShow(bool json) {
  if (json) {
    ResponseAppend_P(PSTR(",\"GNSS\":{\"Valid\":%d,\"DateTime\":\"%s %s\",\"Lat\":%f,\"Lon\":%f,\"Alt\":%f,\"Sat\":%d,\"HDOP\":%f}"),
      GNSSData.valid,
      GNSSData.date, GNSSData.time,
      GNSSData.latitude,
      GNSSData.longitude,
      GNSSData.altitude,
      GNSSData.satellites,
      GNSSData.hdop);
#ifdef USE_WEBSERVER
  } else {
    WSContentSend_PD(PSTR("{s}GNSS Fix{m}%s{e}"), GNSSData.valid ? PSTR("Yes") : PSTR("No"));
    if (GNSSData.valid) {
      WSContentSend_PD(PSTR("{s}GNSS DateTime{m}%s %s{e}"), GNSSData.date, GNSSData.time);
      WSContentSend_PD(PSTR("{s}GNSS Location{m}%f,%f{e}"), GNSSData.latitude, GNSSData.longitude);
      WSContentSend_PD(PSTR("{s}GNSS Altitude{m}%f m{e}"), GNSSData.altitude);
      WSContentSend_PD(PSTR("{s}GNSS Satellites{m}%d{e}"), GNSSData.satellites);
      WSContentSend_PD(PSTR("{s}GNSS HDOP{m}%f{e}"), GNSSData.hdop);
    }
#endif  // USE_WEBSERVER
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv91(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      GNSSInit();
      break;
    case FUNC_EVERY_100_MSECOND:
      GNSSEvery100ms();
      break;
    case FUNC_JSON_APPEND:
      GNSSShow(1);
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
      GNSSShow(0);
      break;
#endif  // USE_WEBSERVER
    case FUNC_COMMAND:
      result = DecodeCommand(kGNSSCommands, GNSSCommand);
      break;
  }
  return result;
}

#endif  // USE_GNSS
#endif  // ESP32
