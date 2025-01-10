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

#define XDRV_92                 92
#define D_CMND_GNSS             "GNSS"
#define RTC_UPDATE_INTERVAL     (30 * 60)   // Every 30 minutes
#define INVALIDATE_AFTER        30          // 30 seconds

class GNSSParser {
public:
   static constexpr size_t BUFFER_SIZE = 2048;
   static constexpr size_t MAX_MESSAGES = 10;

   struct Message {
       enum Type {
           UNKNOWN,
           NMEA,
           RTCM3,
           INVALID
       };

       Type type;
       const uint8_t* data;
       size_t length;
       bool valid;
       const char* error;
   };

   GNSSParser() = default;
   ~GNSSParser() = default;

   bool encode(uint8_t byte);
   bool available() const;
   Message getMessage();
   void clear();

private:
   struct StoredMessage {
       Message::Type type;
       size_t start;
       size_t length;
       bool valid;
       const char* error;
   };

   std::array<uint8_t, BUFFER_SIZE> buffer_{};
   size_t write_pos_ = 0;
   size_t read_pos_ = 0;
   size_t bytes_available_ = 0;
   std::queue<StoredMessage> message_queue_;

   void addMessageToQueue(Message::Type type, size_t start, size_t length, 
                         bool valid = true, const char* error = nullptr);
   void scanBuffer();
};

struct {
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
  char nmea_buffer[128];
  uint8_t nmea_index;
} GNSSData;

TinyGPSPlus gps;
HardwareSerial *gnssSerial = nullptr;
uint32_t last_rtc_sync = 0;

const char kGNSSCommands[] PROGMEM = D_CMND_GNSS "|"
  "Baudrate|"     // Set UART baudrate
  "SerialConfig|" // Set UART configuration
  "Send";         // Send data to GNSS module

void CmndGNSSBaudrate(void) {
  if (XdrvMailbox.data_len > 0) {
    GNSSData.baudrate = XdrvMailbox.payload;
    GNSSInit();
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

void CmndGNSSSend(void) {
  if (gnssSerial && XdrvMailbox.data_len > 0) {
    gnssSerial->write(XdrvMailbox.data, XdrvMailbox.data_len);
    gnssSerial->write("\r\n");
    ResponseCmndChar(PSTR("Sent serial command"));
  } else {
      ResponseCmndError();
  }
}

void (* const GNSSCommand[])(void) PROGMEM = {
  &CmndGNSSBaudrate,
  &CmndGNSSSerialConfig,
  &CmndGNSSSend
};

/*********************************************************************************************/

void GNSSInit(void) {
  if (!GNSSData.baudrate) {
    GNSSData.baudrate = 115200;
  }

  if (!GNSSData.serial_config) {
    GNSSData.serial_config = SERIAL_8N1;
  }

  GNSSData.valid = 0;
  
  gnssSerial = new HardwareSerial(1);
  if (gnssSerial && PinUsed(GPIO_GNSS_NMEA_RX) && PinUsed(GPIO_GNSS_NMEA_TX)) {
    gnssSerial->begin(GNSSData.baudrate, GNSSData.serial_config, Pin(GPIO_GNSS_NMEA_RX), Pin(GPIO_GNSS_NMEA_TX));
    AddLog(LOG_LEVEL_INFO, PSTR("GNSS: Serial initialized at %d baud"), GNSSData.baudrate);
  }
}

void GNSSEvery100ms(void) {
  bool newData = false;
  uint32_t current_time = Rtc.utc_time;
  
  while (gnssSerial && gnssSerial->available() > 0) {
    char c = gnssSerial->read();
    
    // Store character in NMEA buffer
    if (c == '$') {  // Start of NMEA sentence
      GNSSData.nmea_index = 0;
    }
    if (GNSSData.nmea_index < sizeof(GNSSData.nmea_buffer) - 1) {
      GNSSData.nmea_buffer[GNSSData.nmea_index++] = c;
    }
    if (c == '\n') {  // End of NMEA sentence
      GNSSData.nmea_buffer[GNSSData.nmea_index] = '\0';
      // Log the complete NMEA sentence if debug is enabled
      AddLog(LOG_LEVEL_DEBUG, PSTR("GNSS: NMEA %s"), GNSSData.nmea_buffer);
    }
    
    if (gps.encode(c)) {
      newData = true;
    }
  }
  
  if (newData) {
    GNSSData.valid = gps.location.isValid() && gps.date.isValid() && gps.time.isValid();

    if (GNSSData.valid)
      GNSSData.last_valid = current_time;

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
      if (gps.time.isValid() && gps.date.isValid() && (current_time - last_rtc_sync) > RTC_UPDATE_INTERVAL) {
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
  } else {
    if ((current_time - GNSSData.last_valid) > INVALIDATE_AFTER) {
      GNSSData.valid = false;
    }
  }

  if (GNSSData.last_valid > 0) {
    TIME_T last_valid_time_t;
    BreakTime(current_time - GNSSData.last_valid, last_valid_time_t);
    snprintf_P(GNSSData.last_valid_time, sizeof(GNSSData.last_valid_time), PSTR("%dT%02d:%02d:%02d"),
      last_valid_time_t.days,  last_valid_time_t.hour, last_valid_time_t.minute, last_valid_time_t.second);
  } else {
    strlcpy(GNSSData.last_valid_time, PSTR("N/A"), sizeof(GNSSData.last_valid_time));
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
    WSContentSend_PD(PSTR("{s}Last GNSS Fix{m}%s{e}"), GNSSData.last_valid_time);
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

bool Xdrv92(uint32_t function) {
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


void GNSSParser::addMessageToQueue(Message::Type type, size_t start, size_t length, 
                      bool valid, const char* error) {
    if (message_queue_.size() >= MAX_MESSAGES) {
        // Remove oldest message
        StoredMessage& oldest = message_queue_.front();
        size_t bytes_to_remove = oldest.length;
        read_pos_ = (read_pos_ + bytes_to_remove) % BUFFER_SIZE;
        bytes_available_ -= bytes_to_remove;
        message_queue_.pop();
    }
    
    message_queue_.push({type, start, length, valid, error});
}

void GNSSParser::scanBuffer() {
    if (bytes_available_ < 3) return;  // Need at least 3 bytes for any message

    size_t scan_pos = read_pos_;
    size_t bytes_to_scan = bytes_available_;

    while (bytes_to_scan >= 3) {
        // Check for RTCM3 message
        if (buffer_[scan_pos] == 0xD3) {
            if (bytes_to_scan < 6) break;  // Need at least header + CRC

            // Extract message length
            uint16_t msg_length = ((buffer_[(scan_pos + 1) % BUFFER_SIZE] & 0x03) << 8) | 
                                  buffer_[(scan_pos + 2) % BUFFER_SIZE];
            size_t total_length = msg_length + 6;  // header(3) + payload + crc(3)

            if (msg_length > 1023) {  // Invalid length
                scan_pos = (scan_pos + 1) % BUFFER_SIZE;
                bytes_to_scan--;
                continue;
            }

            if (bytes_to_scan < total_length) break;  // Incomplete message

            // Verify CRC here
            bool valid = true;  // TODO: Add CRC check
            
            addMessageToQueue(Message::Type::RTCM3, scan_pos, total_length, valid);
            scan_pos = (scan_pos + total_length) % BUFFER_SIZE;
            bytes_to_scan -= total_length;
            continue;
        }

        // Check for NMEA message
        if (buffer_[scan_pos] == '$' || buffer_[scan_pos] == '!') {
            bool found_end = false;
            size_t msg_length = 0;

            for (size_t i = 0; i < bytes_to_scan && i < 82; i++) {
                size_t pos = (scan_pos + i) % BUFFER_SIZE;
                if (buffer_[pos] == '\n') {
                    msg_length = i + 1;
                    found_end = true;
                    break;
                }
            }

            if (!found_end) break;  // Incomplete message

            // Verify checksum here
            bool valid = true;  // TODO: Add checksum verification

            addMessageToQueue(Message::Type::NMEA, scan_pos, msg_length, valid);
            scan_pos = (scan_pos + msg_length) % BUFFER_SIZE;
            bytes_to_scan -= msg_length;
            continue;
        }

        scan_pos = (scan_pos + 1) % BUFFER_SIZE;
        bytes_to_scan--;
    }
}

bool GNSSParser::encode(uint8_t byte) {
    // Add byte to buffer
    buffer_[write_pos_] = byte;
    write_pos_ = (write_pos_ + 1) % BUFFER_SIZE;
    bytes_available_++;

    // Handle buffer overflow
    if (bytes_available_ > BUFFER_SIZE) {
        read_pos_ = (read_pos_ + 1) % BUFFER_SIZE;
        bytes_available_ = BUFFER_SIZE;
    }

    // Scan for complete messages
    scanBuffer();

    return !message_queue_.empty();
}

bool GNSSParser::available() const {
    return !message_queue_.empty();
}

GNSSParser::Message GNSSParser::getMessage() {
    if (message_queue_.empty()) {
        return {GNSSParser::Message::Type::UNKNOWN, nullptr, 0, false, "No message available"};
    }

    StoredMessage msg = message_queue_.front();
    message_queue_.pop();

    // Create contiguous buffer for message
    static uint8_t msg_buffer[BUFFER_SIZE];  // Static to avoid stack allocation
    
    // Copy message data, handling wrap-around
    for (size_t i = 0; i < msg.length; i++) {
        size_t pos = (msg.start + i) % BUFFER_SIZE;
        msg_buffer[i] = buffer_[pos];
    }

    return {msg.type, msg_buffer, msg.length, msg.valid, msg.error};
}

void GNSSParser::clear() {
    while (!message_queue_.empty()) {
        message_queue_.pop();
    }
    write_pos_ = 0;
    read_pos_ = 0;
    bytes_available_ = 0;
}

#endif  // USE_GNSS
#endif  // ESP32
