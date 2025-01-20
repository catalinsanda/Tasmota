/*
  xdrv_92_esp32_gnss.ino - GNDSS with NMEA support
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

#include <queue>
#include <array>
#include <cstring>
#include <TinyGPSPlus.h>
#include <GNSSParser.h>

#define XDRV_92 92
#define D_CMND_GNSS "GNSS"
#define RTC_UPDATE_INTERVAL (30 * 60) // Every 30 minutes
#define INVALIDATE_AFTER 30           // 30 seconds
#define SERIAL_INPUT_BUFFER_SIZE 2048

struct NMEACounters
{
  uint32_t gga;   // Global Positioning System Fix Data
  uint32_t rmc;   // Recommended Minimum Navigation Information
  uint32_t gll;   // Geographic Position - Latitude/Longitude
  uint32_t gsa;   // GNSS DOP and Active Satellites
  uint32_t gsv;   // GNSS Satellites in View
  uint32_t vtg;   // Course Over Ground and Ground Speed
  uint32_t other; // Any other NMEA sentences
};

struct
{
  uint32_t baudrate;
  uint8_t serial_config;
  uint8_t valid;
  uint32_t last_valid;
  float latitude;
  float longitude;
  float altitude;
  uint8_t satellites;
  float hdop;
  char time[32];
  char date[32];
  char last_valid_time[32];
  NMEACounters nmea_counters;
  uint32_t total_nmea_sentences;
} GNSSData;

TinyGPSPlus gps;
GNSSParser gnssParser;
HardwareSerial *gnssSerial = nullptr;
uint32_t last_rtc_sync = 0;
uint32_t chars_received = 0;

const char kGNSSCommands[] PROGMEM = D_CMND_GNSS "|"
                                                 "Baudrate|"     // Set UART baudrate
                                                 "SerialConfig|" // Set UART configuration
                                                 "Send";         // Send data to GNSS module

void CmndGNSSBaudrate(void)
{
  if (XdrvMailbox.data_len > 0)
  {
    GNSSData.baudrate = XdrvMailbox.payload;
    GNSSInit();
  }
  ResponseCmndNumber(GNSSData.baudrate);
}

void CmndGNSSSerialConfig(void)
{
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 23))
  {
    GNSSData.serial_config = XdrvMailbox.payload;
    GNSSInit(); // Reinitialize with new config
  }
  ResponseCmndNumber(GNSSData.serial_config);
}

void CmndGNSSSend(void)
{
  if (gnssSerial && XdrvMailbox.data_len > 0)
  {
    gnssSerial->write(XdrvMailbox.data, XdrvMailbox.data_len);
    gnssSerial->write("\r\n");
    ResponseCmndChar(PSTR("Sent serial command"));
  }
  else
  {
    ResponseCmndError();
  }
}

void (*const GNSSCommand[])(void) PROGMEM = {
    &CmndGNSSBaudrate,
    &CmndGNSSSerialConfig,
    &CmndGNSSSend};

/*********************************************************************************************/

void GNSSInit(void)
{
  if (!GNSSData.baudrate)
  {
    GNSSData.baudrate = 115200;
  }

  if (!GNSSData.serial_config)
  {
    GNSSData.serial_config = SERIAL_8N1;
  }

  GNSSData.valid = 0;

  gnssSerial = new HardwareSerial(1);
  gnssSerial->setRxBufferSize(SERIAL_INPUT_BUFFER_SIZE);

  if (gnssSerial && PinUsed(GPIO_GNSS_NMEA_RX) && PinUsed(GPIO_GNSS_NMEA_TX))
  {
    gnssSerial->begin(GNSSData.baudrate, GNSSData.serial_config, Pin(GPIO_GNSS_NMEA_RX), Pin(GPIO_GNSS_NMEA_TX));
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Serial initialized at %d baud"), GNSSData.baudrate);
  }
}

void UpdateNMEACounter(const char *sentence)
{
  GNSSData.total_nmea_sentences++;

  if (strncmp(sentence, "$GPGGA", 6) == 0 || strncmp(sentence, "$GNGGA", 6) == 0)
  {
    GNSSData.nmea_counters.gga++;
  }
  else if (strncmp(sentence, "$GPRMC", 6) == 0 || strncmp(sentence, "$GNRMC", 6) == 0)
  {
    GNSSData.nmea_counters.rmc++;
  }
  else if (strncmp(sentence, "$GPGLL", 6) == 0 || strncmp(sentence, "$GNGLL", 6) == 0)
  {
    GNSSData.nmea_counters.gll++;
  }
  else if (strncmp(sentence, "$GPGSA", 6) == 0 || strncmp(sentence, "$GNGSA", 6) == 0)
  {
    GNSSData.nmea_counters.gsa++;
  }
  else if (strncmp(sentence, "$GPGSV", 6) == 0 || strncmp(sentence, "$GNGSV", 6) == 0 ||
           strncmp(sentence, "$GBGSV", 6) || strncmp(sentence, "$GAGSV", 6) || strncmp(sentence, "$GLGSV", 6))
  {
    GNSSData.nmea_counters.gsv++;
  }
  else if (strncmp(sentence, "$GPVTG", 6) == 0 || strncmp(sentence, "$GNVTG", 6) == 0)
  {
    GNSSData.nmea_counters.vtg++;
  }
  else
  {
    GNSSData.nmea_counters.other++;
  }
}

bool ProcessNMEAMessage(const char *nmea_message, uint8_t length)
{
  AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: NMEA %s"), nmea_message);

  UpdateNMEACounter(nmea_message);

  bool newData = false;
  for (int i = 0; i < length; i++)
  {
    if (gps.encode(nmea_message[i]))
    {
      newData = true;
    }
  }
  return newData;
}

void UpdateGNSSData(uint32_t current_time)
{
  GNSSData.valid = gps.location.isValid() && gps.date.isValid() && gps.time.isValid();

  if (GNSSData.valid)
  {
    GNSSData.last_valid = current_time;

    // Update location data
    GNSSData.latitude = gps.location.lat();
    GNSSData.longitude = gps.location.lng();
    GNSSData.altitude = gps.altitude.meters();
    GNSSData.satellites = gps.satellites.value();
    GNSSData.hdop = gps.hdop.hdop();

    // Update time and date
    snprintf_P(GNSSData.time, sizeof(GNSSData.time), PSTR("%02d:%02d:%02d"),
               gps.time.hour(), gps.time.minute(), gps.time.second());
    snprintf_P(GNSSData.date, sizeof(GNSSData.date), PSTR("%04d-%02d-%02d"),
               gps.date.year(), gps.date.month(), gps.date.day());

    UpdateRTCIfNeeded(current_time);
  }
}

void UpdateRTCIfNeeded(uint32_t current_time)
{
  // Update Tasmota's internal time if we have a valid fix
  if (gps.time.isValid() && gps.date.isValid() &&
      (current_time - last_rtc_sync) > RTC_UPDATE_INTERVAL)
  {
    TIME_T tm;
    tm.year = gps.date.year() - 1970;
    tm.month = gps.date.month();
    tm.day_of_month = gps.date.day();
    tm.hour = gps.time.hour();
    tm.minute = gps.time.minute();
    tm.second = gps.time.second();
    tm.nanos = gps.time.centisecond() * 10000;

    uint32_t epoch = MakeTime(tm);
    RtcSetTime(epoch);
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: RTC Time sync %s"), GNSSData.time);
    last_rtc_sync = current_time;
  }
}

void UpdateLastValidTime(uint32_t current_time)
{
  if (GNSSData.last_valid > 0)
  {
    TIME_T last_valid_time_t;
    BreakTime(current_time - GNSSData.last_valid, last_valid_time_t);
    snprintf_P(GNSSData.last_valid_time, sizeof(GNSSData.last_valid_time), PSTR("%dT%02d:%02d:%02d"),
               last_valid_time_t.days, last_valid_time_t.hour,
               last_valid_time_t.minute, last_valid_time_t.second);
  }
  else
  {
    strlcpy(GNSSData.last_valid_time, PSTR("N/A"), sizeof(GNSSData.last_valid_time));
  }
}

void LogRawBuffer(const uint8_t *buffer, size_t length)
{
  if (!length)
    return;

  const size_t BYTES_PER_LINE = 16;
  char hex_line[BYTES_PER_LINE * 3 + 1]; // 2 chars per byte + space + null
  char ascii_line[BYTES_PER_LINE + 1];   // 1 char per byte + null

  for (size_t offset = 0; offset < length; offset += BYTES_PER_LINE)
  {
    size_t line_length = std::min(BYTES_PER_LINE, length - offset);

    memset(hex_line, ' ', sizeof(hex_line));
    memset(ascii_line, ' ', sizeof(ascii_line));
    hex_line[sizeof(hex_line) - 1] = 0;
    ascii_line[sizeof(ascii_line) - 1] = 0;

    for (size_t i = 0; i < line_length; i++)
    {
      uint8_t byte = buffer[offset + i];

      snprintf(&hex_line[i * 3], 4, "%02X ", byte);

      ascii_line[i] = (byte >= 32 && byte <= 126) ? byte : '.';
    }

    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("GNSS Raw [%04X]: %-48s  |%s|"),
           offset, hex_line, ascii_line);
  }
}

void GNSSEvery100ms(void)
{
  uint32_t current_time = Rtc.utc_time;
  static uint8_t read_buffer[SERIAL_INPUT_BUFFER_SIZE];

  if (gnssSerial && gnssSerial->available() > 0)
  {
    size_t bytes_available = gnssSerial->available();
    size_t bytes_to_read = std::min(bytes_available, sizeof(read_buffer));
    size_t bytes_read = gnssSerial->readBytes(read_buffer, bytes_to_read);

    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("GNSS: Read %d bytes from serial"), bytes_read);
    LogRawBuffer(read_buffer, bytes_read);

    size_t processed = 0;
    while (processed < bytes_read)
    {
      size_t parser_space = gnssParser.available_write_space();

      if (parser_space == 0)
      {
        while (gnssParser.available())
        {
          auto msg = gnssParser.getMessage();
          if (msg.type == GNSSParser::Message::Type::NMEA)
          {
            char nmea_message[128];
            uint8_t length = std::min(sizeof(nmea_message) - 1, msg.length);
            strncpy(nmea_message, (char *)msg.data, length);
            nmea_message[length] = 0;

            if (ProcessNMEAMessage(nmea_message, length))
            {
              UpdateGNSSData(current_time);
            }
          }
        }

        parser_space = gnssParser.available_write_space();

        if (parser_space == 0 && !gnssParser.available())
        {
          AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: No valid messages found and no space available, clearing parser"));
          gnssParser.clear();
          parser_space = gnssParser.available_write_space();
        }

        if (parser_space == 0)
        {
          AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: Parser buffer full, skipping data"));
          break;
        }
      }

      size_t chunk_size = std::min(parser_space, bytes_read - processed);

      if (!gnssParser.encode(read_buffer + processed, chunk_size))
      {
        AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: Failed to encode chunk of %d bytes"), chunk_size);
        break;
      }

      processed += chunk_size;

      while (gnssParser.available())
      {
        auto msg = gnssParser.getMessage();
        if (msg.type == GNSSParser::Message::Type::NMEA)
        {
          char nmea_message[128];
          uint8_t length = std::min(sizeof(nmea_message) - 1, msg.length);
          strncpy(nmea_message, (char *)msg.data, length);
          nmea_message[length] = 0;

          if (ProcessNMEAMessage(nmea_message, length))
          {
            UpdateGNSSData(current_time);
          }
        }
      }
    }

    chars_received += bytes_read;

    if ((current_time - GNSSData.last_valid) > INVALIDATE_AFTER)
    {
      GNSSData.valid = false;
    }

    UpdateLastValidTime(current_time);
  }
}

void GNSSShow(bool json)
{
  if (json)
  {
    ResponseAppend_P(PSTR(",\"GNSS\":{\"Valid\":%d,\"DateTime\":\"%s %s\",\"Lat\":%f,\"Lon\":%f,\"Alt\":%f,\"Sat\":%d,\"HDOP\":%f,\"NMEA\":{\"Total\":%d,\"GGA\":%d,\"RMC\":%d,\"GLL\":%d,\"GSA\":%d,\"GSV\":%d,\"VTG\":%d,\"Other\":%d}}"),
                     GNSSData.valid,
                     GNSSData.date, GNSSData.time,
                     GNSSData.latitude,
                     GNSSData.longitude,
                     GNSSData.altitude,
                     GNSSData.satellites,
                     GNSSData.hdop,
                     GNSSData.total_nmea_sentences,
                     GNSSData.nmea_counters.gga,
                     GNSSData.nmea_counters.rmc,
                     GNSSData.nmea_counters.gll,
                     GNSSData.nmea_counters.gsa,
                     GNSSData.nmea_counters.gsv,
                     GNSSData.nmea_counters.vtg,
                     GNSSData.nmea_counters.other);
#ifdef USE_WEBSERVER
  }
  else
  {
    WSContentSend_PD(PSTR("{s}GNSS Fix{m}%s{e}"), GNSSData.valid ? PSTR("Yes") : PSTR("No"));
    WSContentSend_PD(PSTR("{s}Last GNSS Fix{m}%s{e}"), GNSSData.last_valid_time);
    WSContentSend_PD(PSTR("{s}Characters received over serial{m}%d{e}"), chars_received);
    if (GNSSData.valid)
    {
      WSContentSend_PD(PSTR("{s}GNSS DateTime{m}%s %s{e}"), GNSSData.date, GNSSData.time);
      WSContentSend_PD(PSTR("{s}GNSS Location{m}%f,%f{e}"), GNSSData.latitude, GNSSData.longitude);
      WSContentSend_PD(PSTR("{s}GNSS Altitude{m}%f m{e}"), GNSSData.altitude);
      WSContentSend_PD(PSTR("{s}GNSS Satellites{m}%d{e}"), GNSSData.satellites);
      WSContentSend_PD(PSTR("{s}GNSS HDOP{m}%f{e}"), GNSSData.hdop);
    }

    WSContentSend_PD(PSTR("{s}Total NMEA Sentences{m}%d{e}"), GNSSData.total_nmea_sentences);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GGA Messages{m}%d{e}"), GNSSData.nmea_counters.gga);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• RMC Messages{m}%d{e}"), GNSSData.nmea_counters.rmc);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GLL Messages{m}%d{e}"), GNSSData.nmea_counters.gll);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GSA Messages{m}%d{e}"), GNSSData.nmea_counters.gsa);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GSV Messages{m}%d{e}"), GNSSData.nmea_counters.gsv);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• VTG Messages{m}%d{e}"), GNSSData.nmea_counters.vtg);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• Other NMEA Messages{m}%d{e}"), GNSSData.nmea_counters.other);
#endif // USE_WEBSERVER
  }
}

bool Xdrv92(uint32_t function)
{
  bool result = false;

  switch (function)
  {
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
#endif // USE_WEBSERVER
  case FUNC_COMMAND:
    result = DecodeCommand(kGNSSCommands, GNSSCommand);
    break;
  }
  return result;
}

#endif // USE_GNSS
#endif // ESP32