/*
  xdrv_92_esp32_gnss.ino - GNSS with NMEA support and async web server
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
#include <ESPAsyncWebServer.h>
#include <vector>
#include <algorithm>

#ifdef USE_NTRIP
extern void ProcessRTCMMessage(const uint8_t *data, size_t length);
#endif

#define XDRV_92 92
#define D_CMND_GNSS "GNSS"
#define RTC_UPDATE_INTERVAL (30 * 60) // Every 30 minutes
#define INVALIDATE_AFTER 30           // 30 seconds
#define SERIAL_INPUT_BUFFER_SIZE 2048
#define SERIAL_STREAM_BUFFER_SIZE 4096
#define MAX_STREAMING_CLIENTS 10 // Maximum number of simultaneous streaming clients
#define CLIENT_TIMEOUT 30000     // 30 seconds timeout for streaming clients

void UpdateGNSSData(uint32_t current_time);
void ProcessGNSSMessage(const GNSSParser::Message &msg, uint32_t current_time);
void UpdateRTCIfNeeded(uint32_t current_time);
void cleanupInactiveClients();
void broadcastSerialData(const uint8_t *buffer, size_t length);
void handleGNSSSerialRequest(AsyncWebServerRequest *request);
#ifdef USE_NTRIP
void handleRTCMRequest(AsyncWebServerRequest *request);
#endif

struct StreamClient
{
  AsyncClient *client;
  uint32_t lastDataTime;
  bool isActive;
};

AsyncWebServer *asyncServer = nullptr;
std::vector<StreamClient> streamClients;

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
  uint32_t serial_config;
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
  uint32_t stream_clients;
} GNSSData;

struct SerialConfigMap
{
  const char *name;
  uint32_t config;
};

const SerialConfigMap kSerialConfigs[] = {
    {PSTR("SERIAL_5N1"), SERIAL_5N1},
    {PSTR("SERIAL_6N1"), SERIAL_6N1},
    {PSTR("SERIAL_7N1"), SERIAL_7N1},
    {PSTR("SERIAL_8N1"), SERIAL_8N1},
    {PSTR("SERIAL_5N2"), SERIAL_5N2},
    {PSTR("SERIAL_6N2"), SERIAL_6N2},
    {PSTR("SERIAL_7N2"), SERIAL_7N2},
    {PSTR("SERIAL_8N2"), SERIAL_8N2},
    {PSTR("SERIAL_5E1"), SERIAL_5E1},
    {PSTR("SERIAL_6E1"), SERIAL_6E1},
    {PSTR("SERIAL_7E1"), SERIAL_7E1},
    {PSTR("SERIAL_8E1"), SERIAL_8E1},
    {PSTR("SERIAL_5E2"), SERIAL_5E2},
    {PSTR("SERIAL_6E2"), SERIAL_6E2},
    {PSTR("SERIAL_7E2"), SERIAL_7E2},
    {PSTR("SERIAL_8E2"), SERIAL_8E2},
    {PSTR("SERIAL_5O1"), SERIAL_5O1},
    {PSTR("SERIAL_6O1"), SERIAL_6O1},
    {PSTR("SERIAL_7O1"), SERIAL_7O1},
    {PSTR("SERIAL_8O1"), SERIAL_8O1},
    {PSTR("SERIAL_5O2"), SERIAL_5O2},
    {PSTR("SERIAL_6O2"), SERIAL_6O2},
    {PSTR("SERIAL_7O2"), SERIAL_7O2},
    {PSTR("SERIAL_8O2"), SERIAL_8O2}};

typedef struct
{
  uint32_t crc32;
  uint32_t version;
  uint32_t baudrate;
  uint8_t serial_config;
} gnss_settings_t;

gnss_settings_t GNSSSettings;

void CmndGNSSBaudrate(void);
void CmndGNSSSerialConfig(void);
void CmndGNSSSend(void);

void (*const GNSSCommand[])(void) PROGMEM = {
    &CmndGNSSBaudrate,
    &CmndGNSSSerialConfig,
    &CmndGNSSSend};

const char kGNSSCommands[] PROGMEM = D_CMND_GNSS "|"
                                                 "Baudrate|"     // Set UART baudrate
                                                 "SerialConfig|" // Set UART configuration
                                                 "Send";         // Send data to GNSS module

TinyGPSPlus gps;
GNSSParser gnssParser;
HardwareSerial *gnssSerial = nullptr;
uint32_t last_rtc_sync = 0;
uint32_t chars_received = 0;

class GNSSAsyncStreamResponse : public AsyncWebServerResponse
{
private:
  AsyncClient *_client = nullptr;

public:
  GNSSAsyncStreamResponse()
  {
    _code = 200;
    _contentLength = 0;
    _contentType = "application/octet-stream";
    _sendContentLength = false;
    _chunked = true;
  }

  ~GNSSAsyncStreamResponse()
  {
    if (_client)
    {
      handleDisconnect(_client);
    }
  }

  bool _sourceValid() const override
  {
    return true;
  }

  void _respond(AsyncWebServerRequest *request) override
  {
    _client = request->client();
    String header;
    _assembleHead(header, request->version());
    _client->write(header.c_str(), header.length());
    _state = RESPONSE_CONTENT;
  }

  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override
  {
    return len;
  }

  static void handleDisconnect(AsyncClient *client)
  {
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Client disconnected"));
    for (auto &cli : streamClients)
    {
      if (cli.client == client)
      {
        cli.isActive = false;
        break;
      }
    }
  }
};

const char *SerialConfigToString(uint32_t config)
{
  for (const auto &mapping : kSerialConfigs)
  {
    if (mapping.config == config)
    {
      return mapping.name;
    }
  }
  return "UNKNOWN";
}

uint32_t StringToSerialConfig(const char *name)
{
  for (const auto &mapping : kSerialConfigs)
  {
    if (strcasecmp(mapping.name, name) == 0)
    {
      return mapping.config;
    }
  }
  return SERIAL_8N1;
}

void GNSSSettingsSave(void)
{
  uint32_t crc32 = GetCfgCrc32((uint8_t *)&GNSSSettings + 4, sizeof(GNSSSettings) - 4);

  if (crc32 != GNSSSettings.crc32)
  {
    GNSSSettings.crc32 = crc32;

    char filename[20];
    snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_92);

    AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: Saving settings to file %s"), filename);

#ifdef USE_UFILESYS
    if (!TfsSaveFile(filename, (const uint8_t *)&GNSSSettings, sizeof(GNSSSettings)))
    {
      AddLog(LOG_LEVEL_INFO, D_ERROR_FILE_NOT_FOUND);
    }
#else
    AddLog(LOG_LEVEL_INFO, D_ERROR_FILESYSTEM_NOT_READY);
#endif // USE_UFILESYS
  }
}

void GNSSSettingsLoad(void)
{
  char filename[20];
  snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_92);

#ifdef USE_UFILESYS
  if (TfsLoadFile(filename, (uint8_t *)&GNSSSettings, sizeof(GNSSSettings)))
  {
    uint32_t crc32 = GetCfgCrc32((uint8_t *)&GNSSSettings + 4, sizeof(GNSSSettings) - 4);
    if (crc32 != GNSSSettings.crc32)
    {
      memset(&GNSSSettings, 0, sizeof(GNSSSettings));
      GNSSSettings.version = 1;
      GNSSSettings.baudrate = 115200;
      GNSSSettings.serial_config = SERIAL_8N1;
    }
  }
  else
  {
    memset(&GNSSSettings, 0, sizeof(GNSSSettings));
    GNSSSettings.version = 1;
    GNSSSettings.baudrate = 115200;
    GNSSSettings.serial_config = SERIAL_8N1;
  }
#endif // USE_UFILESYS

  GNSSData.baudrate = GNSSSettings.baudrate;
  GNSSData.serial_config = GNSSSettings.serial_config;
}

// Handler for GET "/gnss/serial"
void handleGNSSSerialRequest(AsyncWebServerRequest *request)
{
  if (!HttpCheckPriviledgedAccess())
  {
    request->send(403, "text/plain", "Forbidden");
    return;
  }

  if (streamClients.size() >= MAX_STREAMING_CLIENTS)
  {
    request->send(503, "text/plain", "Maximum number of clients reached");
    return;
  }

  AsyncWebServerResponse *response = new GNSSAsyncStreamResponse();

  StreamClient newClient = {request->client(), millis(), true};
  streamClients.push_back(newClient);

  request->client()->setNoDelay(true);
  response->addHeader("Content-Type", "application/octet-stream");
  response->addHeader("Content-Disposition", "attachment; filename=serial_dump.txt");
  response->addHeader("Cache-Control", "no-cache");
  response->addHeader("Connection", "keep-alive");
  response->addHeader("Keep-Alive", "timeout=60");
  response->addHeader("Transfer-Encoding", "chunked");
  response->addHeader("X-Content-Type-Options", "nosniff");

  request->send(response);
}

#ifdef USE_NTRIP
// Handler for GET "/rtcm"
void handleRTCMRequest(AsyncWebServerRequest *request)
{
  if (!HttpCheckPriviledgedAccess())
  {
    request->send(403, "text/plain", "Forbidden");
    return;
  }
  // TODO: Implement NTRIP caster streaming
  request->send(501, "text/plain", "Not Implemented");
}
#endif

// Initializes the async server and registers endpoints.
void initAsyncServer()
{
  if (!asyncServer)
  {
    asyncServer = new AsyncWebServer(8080);

    asyncServer->on("/gnss/serial", HTTP_GET, handleGNSSSerialRequest);

#ifdef USE_NTRIP
    asyncServer->on("/rtcm", HTTP_GET, handleRTCMRequest);
#endif

    asyncServer->begin();
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Async server started on port 8080"));
  }
}

void cleanupInactiveClients()
{
  uint32_t currentTime = millis();
  streamClients.erase(
      std::remove_if(streamClients.begin(), streamClients.end(),
                     [currentTime](const StreamClient &client) -> bool
                     {
                       return !client.isActive ||
                              ((currentTime - client.lastDataTime) > CLIENT_TIMEOUT) ||
                              !client.client->connected();
                     }),
      streamClients.end());

  GNSSData.stream_clients = streamClients.size();
}

void broadcastSerialData(const uint8_t *buffer, size_t length)
{
  if (length == 0 || streamClients.empty())
  {
    return;
  }

  cleanupInactiveClients();

  for (auto &client : streamClients)
  {
    if (client.isActive && client.client->connected() && client.client->canSend())
    {
      char header[10];
      snprintf(header, sizeof(header), "%X\r\n", length);
      client.client->write(header, strlen(header));
      client.client->write(reinterpret_cast<const char *>(buffer), length);
      client.client->write("\r\n", 2);
      client.lastDataTime = millis();
    }
  }
}

// *****************************************************************
// GNSS command functions
// *****************************************************************

void CmndGNSSBaudrate(void)
{
  if (XdrvMailbox.data_len > 0)
  {
    GNSSData.baudrate = XdrvMailbox.payload;
    GNSSSettings.baudrate = XdrvMailbox.payload;
    GNSSSettingsSave();
    GNSSInit();
  }
  ResponseCmndNumber(GNSSData.baudrate);
}

void CmndGNSSSerialConfig(void)
{
  if (XdrvMailbox.data_len > 0)
  {
    if (XdrvMailbox.data[0] >= '0' && XdrvMailbox.data[0] <= '9')
    {
      // Handle numeric input
      uint32_t config = (uint32_t)XdrvMailbox.payload;
      if (config >= SERIAL_5N1 && config <= SERIAL_8O2)
      {
        GNSSData.serial_config = config;
        GNSSSettings.serial_config = config;
        GNSSSettingsSave();
        GNSSInit();
      }
    }
    else
    {
      // Handle string input
      uint32_t config = StringToSerialConfig(XdrvMailbox.data);
      GNSSData.serial_config = config;
      GNSSSettings.serial_config = config;
      GNSSSettingsSave();
      GNSSInit();
    }
  }
  ResponseCmndChar(SerialConfigToString(GNSSData.serial_config));
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

void GNSSInit(void)
{
  if (!GNSSSettings.baudrate)
  {
    GNSSSettingsLoad();
  }

  if (gnssSerial)
  {
    delete gnssSerial;
  }

  gnssSerial = new HardwareSerial(1);
  gnssSerial->setRxBufferSize(SERIAL_INPUT_BUFFER_SIZE);

  if (gnssSerial && PinUsed(GPIO_GNSS_NMEA_RX) && PinUsed(GPIO_GNSS_NMEA_TX))
  {
    gnssSerial->begin(GNSSData.baudrate, GNSSData.serial_config, Pin(GPIO_GNSS_NMEA_RX), Pin(GPIO_GNSS_NMEA_TX));
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Serial initialized at %d baud with config %s"),
           GNSSData.baudrate, SerialConfigToString(GNSSData.serial_config));
  }

  GNSSData.valid = 0;
}

void UpdateNMEACounter(const char *sentence, uint8_t length)
{
  GNSSData.total_nmea_sentences++;

  if (length > 6)
  {
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
             strncmp(sentence, "$GBGSV", 6) == 0 || strncmp(sentence, "$GAGSV", 6) == 0 ||
             strncmp(sentence, "$GLGSV", 6) == 0)
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
}

bool ProcessNMEAMessage(const char *nmea_message, uint8_t length)
{
  char log_buffer[256];
  size_t copy_length = std::min((unsigned int)length, sizeof(log_buffer) - 1);
  memcpy(log_buffer, nmea_message, copy_length);
  log_buffer[copy_length] = '\0';

  AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: NMEA %s"), log_buffer);

  UpdateNMEACounter(nmea_message, length);

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
    GNSSData.latitude = gps.location.lat();
    GNSSData.longitude = gps.location.lng();
    GNSSData.altitude = gps.altitude.meters();
    GNSSData.satellites = gps.satellites.value();
    GNSSData.hdop = gps.hdop.hdop();

    snprintf_P(GNSSData.time, sizeof(GNSSData.time), PSTR("%02d:%02d:%02d"),
               gps.time.hour(), gps.time.minute(), gps.time.second());
    snprintf_P(GNSSData.date, sizeof(GNSSData.date), PSTR("%04d-%02d-%02d"),
               gps.date.year(), gps.date.month(), gps.date.day());

    UpdateRTCIfNeeded(current_time);
  }
}

void UpdateRTCIfNeeded(uint32_t current_time)
{
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
    snprintf_P(GNSSData.last_valid_time, sizeof(GNSSData.last_valid_time),
               PSTR("%dT%02d:%02d:%02d"),
               last_valid_time_t.days, last_valid_time_t.hour,
               last_valid_time_t.minute, last_valid_time_t.second);
  }
  else
  {
    strlcpy(GNSSData.last_valid_time, PSTR("N/A"), sizeof(GNSSData.last_valid_time));
  }
}

void ProcessGNSSMessage(const GNSSParser::Message &msg, uint32_t current_time)
{
  switch (msg.type)
  {
  case GNSSParser::Message::Type::NMEA:
  {
    if (ProcessNMEAMessage((char *)msg.data, msg.length))
    {
      UpdateGNSSData(current_time);
    }
    break;
  }
#ifdef USE_NTRIP
  case GNSSParser::Message::Type::RTCM3:
    ProcessRTCMMessage(msg.data, msg.length);
    break;
#endif
  default:
    break;
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

    broadcastSerialData(read_buffer, bytes_read);

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
          ProcessGNSSMessage(msg, current_time);
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
        ProcessGNSSMessage(msg, current_time);
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
    if (GNSSData.valid)
    {
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GNSS DateTime{m}%s %s{e}"), GNSSData.date, GNSSData.time);
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GNSS Location{m}%f,%f{e}"), GNSSData.latitude, GNSSData.longitude);
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GNSS Altitude{m}%f m{e}"), GNSSData.altitude);
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GNSS Satellites{m}%d{e}"), GNSSData.satellites);
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GNSS HDOP{m}%f{e}"), GNSSData.hdop);
    }

    WSContentSend_PD(PSTR("{s}Total NMEA Sentences{m}%d{e}"), GNSSData.total_nmea_sentences);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GGA Messages{m}%d{e}"), GNSSData.nmea_counters.gga);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• RMC Messages{m}%d{e}"), GNSSData.nmea_counters.rmc);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GLL Messages{m}%d{e}"), GNSSData.nmea_counters.gll);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GSA Messages{m}%d{e}"), GNSSData.nmea_counters.gsa);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• GSV Messages{m}%d{e}"), GNSSData.nmea_counters.gsv);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• VTG Messages{m}%d{e}"), GNSSData.nmea_counters.vtg);
    WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• Other NMEA Messages{m}%d{e}"), GNSSData.nmea_counters.other);

    WSContentSend_PD(PSTR("{s}Characters received over serial{m}%d{e}"), chars_received);
    WSContentSend_PD(PSTR("{s}Active Streaming Clients{m}%d{e}"), GNSSData.stream_clients);
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
    initAsyncServer(); // Initialize the async server
    break;
  case FUNC_EVERY_100_MSECOND:
    GNSSEvery100ms();
    break;
  case FUNC_JSON_APPEND:
    GNSSShow(true);
    break;
#ifdef USE_WEBSERVER
  case FUNC_WEB_SENSOR:
    GNSSShow(false);
    break;
#endif // USE_WEBSERVER
  case FUNC_COMMAND:
    result = DecodeCommand(kGNSSCommands, GNSSCommand);
    break;
  case FUNC_SAVE_SETTINGS:
    GNSSSettingsSave();
    break;
  }
  return result;
}

#endif // USE_GNSS
#endif // ESP32
