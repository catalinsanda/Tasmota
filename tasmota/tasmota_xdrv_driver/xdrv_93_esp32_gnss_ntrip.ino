/*
  xdrv_92_esp32_gnss_ntrip.ino - NTRIP Server and Caster
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
#ifdef USE_NTRIP

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <base64.hpp>
#include <algorithm>
#include <vector>

#define XDRV_93 93
#define MESSAGE_STATISTICS_COUNT 8

struct RemoteNTRIPClient
{
  AsyncClient *client;
  uint32_t lastDataTime;
  bool isActive;
  String mountpoint;
};

std::vector<RemoteNTRIPClient> remoteNtripClients;
uint32_t remoteNtripClientsTotal = 0;
uint32_t rtcmBytesForwarded = 0;

void rtcmInitializeCasterEndpoint(AsyncWebServer *ntrip_web_server);
void handleCasterRequest(AsyncWebServerRequest *request);

const char NTRIP_SOURCE_TABLE[] PROGMEM =
    "SOURCETABLE 200 OK\r\n"
    "Server: NTRIP ESP32Caster\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "STR;%s;RTCM 3.2;1004(1),1005/1007(5),1074(1),1077(1),1084(1),1087(1),1094(1),1097(1),1124(1),1127(1);2;GPS+GLO+GAL+BDS;ESP32;ESP;0;;none;B;N;0;\r\n"
    "ENDSOURCETABLE\r\n";

#ifdef USE_WEBSERVER

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

const char HTTP_BTN_MENU_NTRIP[] PROGMEM =
    "<p><form action='ntrip' method='get'><button>" D_CONFIGURE_NTRIP "</button></form></p>";

const char HTTP_FORM_NTRIP[] PROGMEM =
    "<fieldset style='min-width:530px;background:#444;color:white;padding:20px;'>"
    "<legend style='text-align:left;'><b>&nbsp;" D_NTRIP_PARAMETERS "&nbsp;</b></legend>"
    "<style>"
    ".ntrip-input:disabled {"
    "background-color: #222 !important;"
    "border-color: #444 !important;"
    "color: #666 !important;"
    "cursor: not-allowed;"
    "opacity: 0.7;"
    "}"
    ".ntrip-input:not(:disabled):hover {"
    "border-color: #777;"
    "background-color: #3a3a3a;"
    "}"
    "</style>"
    "<form method='post' action='ntrip'>"
    "%s"
    "<button name='save' type='submit' class='button bgrn'>" D_SAVE "</button>"
    "</form>"
    "</fieldset>";

const char HTTP_NTRIP_SCRIPT[] PROGMEM =
    "<script>"
    "function toggleNtripFields(checkbox, fields) {"
    "fields.forEach(field => {"
    "const element = document.getElementById(field);"
    "if (element) {"
    "element.disabled = !checkbox.checked;"
    "if (!checkbox.checked) {"
    "element.value = '';"
    "}"
    "}"
    "});"
    "}"
    "</script>";

const char HTTP_NTRIP_SCRIPT_ONLOAD[] PROGMEM =
    "<script>"
    "window.addEventListener('load', function() { toggleNtripServer(%d); });"
    "</script>";

const char HTTP_FORM_NTRIP_SERVER[] PROGMEM =
    "<fieldset style='margin:10px 0;padding:15px;border:1px solid #666;'>"
    "<legend style='text-align:left; display:flex; align-items:center; gap:8px;'>"
    "<span style='color:white;'>%s</span>" // Server title
    "<input type='checkbox' id='ntrip_server_enable_%d' name='ntrip_server_enable_%d' "
    "onchange='toggleNtripFields(this, [\"ntrip_serv_host_%d\",\"ntrip_serv_port_%d\",\"ntrip_serv_mount_%d\",\"ntrip_serv_user_%d\",\"ntrip_serv_pass_%d\"])' "
    "%s " // 'checked' if enabled
    "style='margin:0;'/>"
    "</legend>"
    "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
    "<label for='ntrip_serv_host_%d' style='color:white;'>" D_NTRIP_HOST "</label>"
    "<input type='text' id='ntrip_serv_host_%d' name='ntrip_serv_host_%d' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"

    "<label for='ntrip_serv_port_%d' style='color:white;'>" D_NTRIP_PORT "</label>"
    "<input type='number' id='ntrip_serv_port_%d' name='ntrip_serv_port_%d' value='%d' %s min='1' max='65535' placeholder='2101' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"

    "<label for='ntrip_serv_mount_%d' style='color:white;'>" D_NTRIP_MOUNT "</label>"
    "<input type='text' id='ntrip_serv_mount_%d' name='ntrip_serv_mount_%d' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
    "</div>"
    "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
    "<label for='ntrip_serv_user_%d' style='color:white;'>" D_NTRIP_USERNAME "</label>"
    "<input type='text' id='ntrip_serv_user_%d' name='ntrip_serv_user_%d' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"

    "<label for='ntrip_serv_pass_%d' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
    "<input type='text' id='ntrip_serv_pass_%d' name='ntrip_serv_pass_%d' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
    "</div>"
    "</fieldset>";

const char HTTP_FORM_NTRIP_CASTER[] PROGMEM =
    "<fieldset style='margin:10px 0;padding:15px;border:1px solid #666;'>"
    "<legend style='text-align:left; display:flex; align-items:center; gap:8px;'>"
    "<span style='color:white;'>" D_NTRIP_CASTER "</span>"
    "<input type='checkbox' id='ntrip_caster_enable' name='ntrip_caster_enable' "
    "onchange='toggleNtripFields(this, [\"ntrip_cast_port\",\"ntrip_cast_mount\",\"ntrip_cast_user\",\"ntrip_cast_pass\"])' "
    "%s " // 'checked' if enabled
    "style='margin:0;'/>"
    "</legend>"
    "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
    "<label for='ntrip_cast_port' style='color:white;'>" D_NTRIP_PORT "</label>"
    "<input type='number' id='ntrip_cast_port' name='ntrip_cast_port' value='%d' %s min='1' max='65535' placeholder='2101' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"

    "<label for='ntrip_cast_mount' style='color:white;'>" D_NTRIP_MOUNT "</label>"
    "<input type='text' id='ntrip_cast_mount' name='ntrip_cast_mount' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
    "</div>"
    "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
    "<label for='ntrip_cast_user' style='color:white;'>" D_NTRIP_USERNAME "</label>"
    "<input type='text' id='ntrip_cast_user' name='ntrip_cast_user' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"

    "<label for='ntrip_cast_pass' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
    "<input type='text' id='ntrip_cast_pass' name='ntrip_cast_pass' value='%s' %s maxlength='32' "
    "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
    "</div>"
    "<br>"
    "</fieldset>";

typedef struct __message_statistics_entry
{
  uint16_t message_type;
  uint32_t message_count;
} message_statistics_entry;

message_statistics_entry message_statistics[MESSAGE_STATISTICS_COUNT];

void InitializeMessageStatistics();
void UpdateMessageStatistics(uint16_t msg_type);
void RTCMShowJSON();
void RTCMShowWebSensor();

bool ntrip_settings_initialized = false;
AsyncWebServer *web_server = nullptr;
AsyncWebHandler *caster_handler = nullptr;
char caster_mountpoint[34];

typedef struct __ntrip_server_settings
{
  uint16_t port;
  bool enabled;
  char host[33];
  char mountpoint[33];
  char username[33];
  char password[33];
} ntrip_server_settings_t;

typedef struct __ntrip_caster_settings
{
  uint16_t port;
  bool enabled;
  char mountpoint[33];
  char username[33];
  char password[33];
} ntrip_caster_settings_t;

typedef struct __ntrip_settings
{
  uint32_t crc32;
  uint32_t version;
  ntrip_server_settings_t server_settings[2];
  ntrip_caster_settings_t caster_settings;
} ntrip_settings_t;

ntrip_settings_t NtripSettings;

void NtripSettingsSave(void)
{
  // Calculate CRC32 of everything except the CRC itself
  uint32_t crc32 = GetCfgCrc32((uint8_t *)&NtripSettings + 4, sizeof(NtripSettings) - 4);

  if (crc32 != NtripSettings.crc32)
  {
    NtripSettings.crc32 = crc32;

    char filename[20];
    snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_93);

    AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: Saving settings to file %s"), filename);

#ifdef USE_UFILESYS
    if (!TfsSaveFile(filename, (const uint8_t *)&NtripSettings, sizeof(NtripSettings)))
    {
      AddLog(LOG_LEVEL_INFO, D_ERROR_FILE_NOT_FOUND);
    }
#else
    AddLog(LOG_LEVEL_INFO, D_ERROR_FILESYSTEM_NOT_READY);
#endif // USE_UFILESYS
  }
}

void NtripSettingsLoad(void)
{
  char filename[20];
  snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_93);

#ifdef USE_UFILESYS
  if (TfsLoadFile(filename, (uint8_t *)&NtripSettings, sizeof(NtripSettings)))
  {
    uint32_t crc32 = GetCfgCrc32((uint8_t *)&NtripSettings + 4, sizeof(NtripSettings) - 4);
    if (crc32 != NtripSettings.crc32)
    {
      // CRC mismatch - initialize with defaults
      memset(&NtripSettings, 0, sizeof(NtripSettings));
      NtripSettings.version = 1;
    }
  }
  else
  {
    // File not found - initialize with defaults
    memset(&NtripSettings, 0, sizeof(NtripSettings));
    NtripSettings.version = 1;
  }
#endif // USE_UFILESYS
}

void Ntrip_Save_Settings(void)
{
  String stmp;
  bool needs_save = false;
  const uint8_t NUM_SERVERS = 2;

  // Server settings
  for (uint8_t server_idx = 0; server_idx < NUM_SERVERS; server_idx++)
  {
    char field_name[32];
    uint8_t server_num = server_idx + 1;

    snprintf(field_name, sizeof(field_name), "ntrip_server_enable_%d", server_num);
    if (Webserver->hasArg(field_name))
    {
      needs_save |= (NtripSettings.server_settings[server_idx].enabled != true);
      NtripSettings.server_settings[server_idx].enabled = true;

      // Host
      snprintf(field_name, sizeof(field_name), "ntrip_serv_host_%d", server_num);
      if (Webserver->hasArg(field_name))
      {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].host, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].host, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].host));
      }

      // Port
      snprintf(field_name, sizeof(field_name), "ntrip_serv_port_%d", server_num);
      if (Webserver->hasArg(field_name))
      {
        stmp = Webserver->arg(field_name);
        uint16_t port = stmp.toInt();
        needs_save |= (NtripSettings.server_settings[server_idx].port != port);
        NtripSettings.server_settings[server_idx].port = port;
      }

      // Mountpoint
      snprintf(field_name, sizeof(field_name), "ntrip_serv_mount_%d", server_num);
      if (Webserver->hasArg(field_name))
      {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].mountpoint, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].mountpoint, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].mountpoint));
      }

      // Username
      snprintf(field_name, sizeof(field_name), "ntrip_serv_user_%d", server_num);
      if (Webserver->hasArg(field_name))
      {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].username, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].username, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].username));
      }

      // Password
      snprintf(field_name, sizeof(field_name), "ntrip_serv_pass_%d", server_num);
      if (Webserver->hasArg(field_name))
      {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].password, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].password, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].password));
      }
    }
    else
    {
      needs_save |= (NtripSettings.server_settings[server_idx].enabled != false);
      NtripSettings.server_settings[server_idx].enabled = false;
    }
  }

  // Caster settings
  if (Webserver->hasArg("ntrip_caster_enable"))
  {
    needs_save |= (NtripSettings.caster_settings.enabled != true);
    NtripSettings.caster_settings.enabled = true;

    if (Webserver->hasArg("ntrip_cast_port"))
    {
      stmp = Webserver->arg("ntrip_cast_port");
      uint16_t port = stmp.toInt();
      needs_save |= (NtripSettings.caster_settings.port != port);
      NtripSettings.caster_settings.port = port;
    }

    if (Webserver->hasArg("ntrip_cast_mount"))
    {
      stmp = Webserver->arg("ntrip_cast_mount");
      needs_save |= (strcmp(NtripSettings.caster_settings.mountpoint, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.mountpoint, stmp.c_str(),
              sizeof(NtripSettings.caster_settings.mountpoint));
    }

    if (Webserver->hasArg("ntrip_cast_user"))
    {
      stmp = Webserver->arg("ntrip_cast_user");
      needs_save |= (strcmp(NtripSettings.caster_settings.username, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.username, stmp.c_str(),
              sizeof(NtripSettings.caster_settings.username));
    }

    if (Webserver->hasArg("ntrip_cast_pass"))
    {
      stmp = Webserver->arg("ntrip_cast_pass");
      needs_save |= (strcmp(NtripSettings.caster_settings.password, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.password, stmp.c_str(),
              sizeof(NtripSettings.caster_settings.password));
    }
  }
  else
  {
    needs_save |= (NtripSettings.caster_settings.enabled != false);
    NtripSettings.caster_settings.enabled = false;
  }

  if (needs_save)
  {
    AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: Settings changed, saving"));
    NtripSettingsSave();
    NtripProcessNewSettings();
  }
}

class NTRIPClient
{
public:
  static constexpr uint32_t MAX_RETRIES = 1000;
  static constexpr uint32_t RETRY_DELAY_MS = 5000; // 5 seconds

  NTRIPClient() : client(nullptr), connected(false), enabled(false),
                  retryCount(0), nextRetryTime(0), lastConnectAttempt(0)
  {
    memset(host, 0, sizeof(host));
    memset(mountpoint, 0, sizeof(mountpoint));
    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));
  }

  ~NTRIPClient()
  {
    disconnect();
    if (client)
    {
      delete client;
      client = nullptr;
    }
  }

  void begin(const ntrip_server_settings_t &settings)
  {
    if (!settings.enabled)
    {
      enabled = false;
      disconnect();
      return;
    }

    // Store settings
    enabled = true;
    strlcpy(host, settings.host, sizeof(host));
    port = settings.port;
    strlcpy(mountpoint, settings.mountpoint, sizeof(mountpoint));
    strlcpy(username, settings.username, sizeof(username));
    strlcpy(password, settings.password, sizeof(password));

    // Initialize client if needed
    if (!client)
    {
      client = new AsyncClient();
      if (!client)
      {
        AddLog(LOG_LEVEL_ERROR, PSTR("NTRIPC: Failed to create AsyncClient"));
        return;
      }

      client->onConnect([this](void *obj, AsyncClient *c)
                        { handleConnect(c); });
      client->onDisconnect([this](void *obj, AsyncClient *c)
                           { handleDisconnect(c); });
      client->onError([this](void *obj, AsyncClient *c, int8_t error)
                      { handleError(c, error); });
      client->onData([this](void *obj, AsyncClient *c, void *data, size_t len)
                     { handleData(c, (uint8_t *)data, len); });
    }

    // Start connection if not already connected
    if (!connected && !client->connecting())
    {
      connect();
    }
  }

  void stop()
  {
    enabled = false;
    disconnect();
  }

  void sendData(const uint8_t *data, size_t length)
  {
    if (!enabled || !connected || !client || !client->connected())
    {
      return;
    }

    client->write(reinterpret_cast<const char *>(data), length);
  }

  bool isConnected() const
  {
    return connected;
  }

  void retryConnect()
  {
    if (!enabled || !client)
    {
      return;
    }

    uint32_t now = millis();

    // Check if we need to retry connection
    if (!connected && !client->connecting() && retryCount < MAX_RETRIES)
    {
      if (now >= nextRetryTime)
      {
        connect();
      }
    }
  }

private:
  AsyncClient *client;
  bool connected;
  bool enabled;
  uint32_t retryCount;
  uint32_t nextRetryTime;
  uint32_t lastConnectAttempt;

  char host[33];
  uint16_t port;
  char mountpoint[33];
  char username[33];
  char password[33];

  void connect()
  {
    if (!client || client->connected() || client->connecting())
    {
      return;
    }

    lastConnectAttempt = millis();
    AddLog(LOG_LEVEL_INFO, PSTR("NTRIPC: Connecting to %s:%d..."), host, port);

    if (!client->connect(host, port))
    {
      handleError(client, -1);
      return;
    }
  }

  void disconnect()
  {
    if (client && (client->connected() || client->connecting()))
    {
      client->close(true);
    }
    connected = false;
  }

  void handleConnect(AsyncClient *c)
  {
    if (c != client)
      return;

    AddLog(LOG_LEVEL_INFO, PSTR("NTRIPC: Connected to %s:%d"), host, port);
    connected = true;
    retryCount = 0;

    // Send NTRIP request
    char request[512];
    createNTRIPRequest(request, sizeof(request));
    client->write(request, strlen(request));
  }

  void handleDisconnect(AsyncClient *c)
  {
    if (c != client)
      return;

    connected = false;
    AddLog(LOG_LEVEL_INFO, PSTR("NTRIPC: Disconnected from %s:%d"), host, port);

    if (enabled && retryCount < MAX_RETRIES)
    {
      nextRetryTime = millis() + RETRY_DELAY_MS;
      retryCount++;

      AddLog(LOG_LEVEL_INFO, PSTR("NTRIPC: Will retry in %d ms (attempt %d/%d)"),
      RETRY_DELAY_MS, retryCount, MAX_RETRIES);
    }
  }

  void handleError(AsyncClient *c, int8_t error)
  {
    if (c != client)
      return;

    AddLog(LOG_LEVEL_ERROR, PSTR("NTRIPC: Connection error %d"), error);
    disconnect();
  }

  void handleData(AsyncClient *c, uint8_t *data, size_t len)
  {
    if (c != client || len == 0)
      return;

    // Convert to null-terminated string for response checking
    char *response = (char *)malloc(len + 1);
    if (!response)
      return;

    memcpy(response, data, len);
    response[len] = '\0';

    // Check response code
    if (strstr(response, "200"))
    {
      AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIPC: Server accepted connection"));
    }
    else if (strstr(response, "401"))
    {
      AddLog(LOG_LEVEL_ERROR, PSTR("NTRIPC: Authentication failed"));
      disconnect();
    }

    free(response);
  }

  void createNTRIPRequest(char *buffer, size_t bufferSize)
  {
    char auth[192] = {0};

    // Create authentication string if credentials provided
    if (strlen(username) > 0)
    {
      const uint8_t credentials_buf_len = 65;
      char credentials[credentials_buf_len] = {0};
      snprintf_P(credentials, sizeof(credentials), PSTR("%s:%s"), username, password);
      size_t credentials_len = strnlen(credentials, sizeof(credentials));

      unsigned int encoded_len = encode_base64_length(credentials_len);
      if (encoded_len > credentials_buf_len * 4 / 3 + 2)
      {
        AddLog(LOG_LEVEL_ERROR, PSTR("Credentials too long"));
        buffer[0] = '\0';
      }

      char encoded[credentials_buf_len * 4 / 3 + 2] = {0};
      encode_base64((unsigned char *)credentials, credentials_len, (unsigned char *)encoded);

      snprintf_P(auth, sizeof(auth), PSTR("Authorization: Basic %s\r\n"), encoded);
    }

    // Build request
    snprintf_P(buffer, bufferSize,
               PSTR("GET /%s HTTP/1.1\r\n"
                    "User-Agent: NTRIP ESP32Client/1.0\r\n"
                    "Host: %s\r\n"
                    "%s" // Auth header (if any)
                    "Accept: application/x-rtcm\r\n"
                    "\r\n"),
               mountpoint,
               host,
               auth);
  }
};

NTRIPClient ntripClients[2];

void initializeNTRIPClients()
{
  for (uint8_t i = 0; i < 2; i++)
  {
    ntripClients[i].begin(NtripSettings.server_settings[i]);
  }
  AddLog(LOG_LEVEL_INFO, PSTR("NTRIPC: Initialized NTRIP clients"));
}

void NtripProcessNewSettings()
{
  for (uint8_t i = 0; i < 2; i++)
  {
    ntripClients[i].begin(NtripSettings.server_settings[i]);
  }

  rtcmInitializeCasterEndpoint(nullptr);
}

char *ext_vsnprintf_malloc_P_wrapper(const char *fmt_P, ...)
{
  va_list args;
  va_start(args, fmt_P);
  char *result = ext_vsnprintf_malloc_P(fmt_P, args);
  va_end(args);
  return result;
}

void HandleNtripConfiguration(void)
{
  if (!HttpCheckPriviledgedAccess())
  {
    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_NTRIP));

  if (Webserver->hasArg("save"))
  {
    Ntrip_Save_Settings();
    HandleConfiguration();
    return;
  }

  WSContentStart_P(PSTR(D_CONFIGURE_NTRIP));
  WSContentSendStyle();

  const uint8_t NUM_SERVERS = 2;
  const char *disabled_attr = "disabled";
  const uint16_t DEFAULT_NTRIP_PORT = 2101;

  // Create servers HTML
  char *servers_html = nullptr;
  for (uint8_t server_idx = 0; server_idx < sizeof(NtripSettings.server_settings) / sizeof(ntrip_server_settings_t); server_idx++)
  {
    uint8_t server_num = server_idx + 1;
    const ntrip_server_settings_t *server = &NtripSettings.server_settings[server_idx];

    char *server_html = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_SERVER,
                                                       // Title and checkbox
                                                       server_idx == 0 ? D_NTRIP_SERVER_1 : D_NTRIP_SERVER_2,
                                                       server_num, server_num,                                     // id and name
                                                       server_num, server_num, server_num, server_num, server_num, // toggleNtripFields ids
                                                       server->enabled ? "checked" : "",

                                                       // Host field
                                                       server_num, server_num, server_num,
                                                       server->host,
                                                       server->enabled ? "" : disabled_attr,

                                                       // Port field
                                                       server_num, server_num, server_num,
                                                       server->port ? server->port : DEFAULT_NTRIP_PORT,
                                                       server->enabled ? "" : disabled_attr,

                                                       // Mount field
                                                       server_num, server_num, server_num,
                                                       server->mountpoint,
                                                       server->enabled ? "" : disabled_attr,

                                                       // Username field
                                                       server_num, server_num, server_num,
                                                       server->username,
                                                       server->enabled ? "" : disabled_attr,

                                                       // Password field
                                                       server_num, server_num, server_num,
                                                       server->password,
                                                       server->enabled ? "" : disabled_attr);

    if (!server_html)
    {
      if (servers_html)
        free(servers_html);
      AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for server HTML"));
      return;
    }

    if (servers_html)
    {
      char *new_html = ext_vsnprintf_malloc_P_wrapper(PSTR("%s%s"), servers_html, server_html);
      free(servers_html);
      free(server_html);
      if (!new_html)
      {
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to combine server HTML"));
        return;
      }
      servers_html = new_html;
    }
    else
    {
      servers_html = server_html;
    }
  }

  // Create caster HTML
  char *caster_html = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_CASTER,
                                                     // Checkbox
                                                     NtripSettings.caster_settings.enabled ? "checked" : "",

                                                     // Port field
                                                     NtripSettings.caster_settings.port ? NtripSettings.caster_settings.port : DEFAULT_NTRIP_PORT,
                                                     NtripSettings.caster_settings.enabled ? "" : disabled_attr,

                                                     // Mount field
                                                     NtripSettings.caster_settings.mountpoint,
                                                     NtripSettings.caster_settings.enabled ? "" : disabled_attr,

                                                     // Username field
                                                     NtripSettings.caster_settings.username,
                                                     NtripSettings.caster_settings.enabled ? "" : disabled_attr,

                                                     // Password field
                                                     NtripSettings.caster_settings.password,
                                                     NtripSettings.caster_settings.enabled ? "" : disabled_attr);

  if (!caster_html)
  {
    if (servers_html)
      free(servers_html);
    AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for caster HTML"));
    return;
  }

  // Combine all content
  char *all_content = ext_vsnprintf_malloc_P_wrapper(PSTR("%s%s"), servers_html, caster_html);
  if (!all_content)
  {
    if (servers_html)
      free(servers_html);
    free(caster_html);
    AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for all content"));
    return;
  }

  // Send the form structure
  WSContentSend(HTTP_NTRIP_SCRIPT, strlen_P(HTTP_NTRIP_SCRIPT));
  WSContentSend_P(HTTP_FORM_NTRIP, all_content);

  // Clean up
  free(servers_html);
  free(caster_html);
  free(all_content);

  WSContentSpaceButton(BUTTON_CONFIGURATION);
  WSContentStop();
}

char *ext_snprintf_malloc_P_wrapper(const char *fmt_P, ...)
{
  va_list args;
  va_start(args, fmt_P);
  char *result = (char *)ext_vsnprintf_P(nullptr, 0, fmt_P, args);
  va_end(args);
  return result;
}

class NTRIPAsyncStreamResponse : public AsyncWebServerResponse
{
private:
  AsyncClient *_client = nullptr;

public:
  NTRIPAsyncStreamResponse()
  {
    _code = 200;
    _contentLength = 0;
    _contentType = "application/octet-stream";
    _sendContentLength = false;
    _chunked = false;
  }

  ~NTRIPAsyncStreamResponse()
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

    // Add client to NTRIP clients list
    RemoteNTRIPClient newClient = {
        _client,
        millis(),
        true,
        request->url().substring(1) // Remove leading slash from mountpoint
    };
    remoteNtripClients.push_back(newClient);
    remoteNtripClientsTotal = remoteNtripClients.size();
  }

  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override
  {
    return len;
  }

  static void handleDisconnect(AsyncClient *client)
  {
    AddLog(LOG_LEVEL_INFO, PSTR("NTRIP: Client disconnected"));
    for (auto &cli : remoteNtripClients)
    {
      if (cli.client == client)
      {
        cli.isActive = false;
        break;
      }
    }
  }
};

// Handler for GET "/MOUNT_POINT"
void handleCasterRequest(AsyncWebServerRequest *request)
{
  if (!NtripSettings.caster_settings.enabled || !caster_handler)
  {
    request->send(503, "text/plain", "NTRIP Caster not enabled");
    return;
  }

  String mountpoint = request->url();
  mountpoint.remove(0, 1); // Remove leading slash

  if (mountpoint != NtripSettings.caster_settings.mountpoint)
  {
    if (mountpoint == "")
    {
      // Send source table
      char *sourceTable = ext_snprintf_malloc_P_wrapper(NTRIP_SOURCE_TABLE,
                                                        NtripSettings.caster_settings.mountpoint);
      if (sourceTable)
      {
        request->send(200, "text/plain", sourceTable);
        free(sourceTable);
      }
      else
      {
        request->send(500, "text/plain", "Internal Server Error");
      }
      return;
    }
    request->send(404, "text/plain", "Mountpoint not found");
    return;
  }

  // Additional check for handler validity
  if (strcmp(caster_mountpoint + 1, mountpoint.c_str()) != 0)
  {
    request->send(409, "text/plain", "Mountpoint configuration mismatch");
    return;
  }

  // Check authorization if credentials are configured
  if (strlen(NtripSettings.caster_settings.username) > 0)
  {
    if (!request->authenticate(NtripSettings.caster_settings.username,
                               NtripSettings.caster_settings.password))
    {
      request->requestAuthentication();
      return;
    }
  }

  if (remoteNtripClients.size() >= MAX_STREAMING_CLIENTS)
  {
    request->send(503, "text/plain", "Maximum number of clients reached");
    return;
  }

  AsyncWebServerResponse *response = new NTRIPAsyncStreamResponse();
  if (!response)
  {
    AddLog(LOG_LEVEL_ERROR, PSTR("NTRIP: Failed to create response handler"));
    request->send(500, "text/plain", "Failed to create response handler");
    return;
  }

  if (!request->client())
  {
    delete response;
    AddLog(LOG_LEVEL_ERROR, PSTR("NTRIP: Invalid client connection"));
    request->send(500, "text/plain", "Invalid client connection");
    return;
  }

  request->client()->setNoDelay(true);
  response->addHeader("Content-Type", "application/octet-stream");
  response->addHeader("Connection", "close");

  request->send(response);
  AddLog(LOG_LEVEL_INFO, PSTR("NTRIP: Client connected to Caster from %s:%d"), request->client()->remoteIP().toString().c_str(), request->client()->remotePort());
}

void rtcmInitializeCasterEndpoint(AsyncWebServer *ntrip_web_server)
{
  if (ntrip_web_server)
    web_server = ntrip_web_server;

  if (!ntrip_settings_initialized || !web_server)
    return;

  char mountpoint[34];
  snprintf(mountpoint, sizeof(mountpoint), PSTR("/%s"), NtripSettings.caster_settings.mountpoint);

  // First handle disabling case
  if (!NtripSettings.caster_settings.enabled)
  {
    if (caster_handler)
    {
      web_server->removeHandler(caster_handler);
      caster_handler = nullptr;
      memset(caster_mountpoint, 0, sizeof(caster_mountpoint));
      AddLog(LOG_LEVEL_INFO, PSTR("NTRIP: Removed Caster mountpoint"));
    }
    return;
  }

  // Check if mountpoint changed
  if (strncmp(caster_mountpoint, mountpoint, std::min(sizeof(caster_mountpoint), sizeof(mountpoint))) != 0)
  {
    // Remove old handler if it exists
    if (caster_handler)
    {
      web_server->removeHandler(caster_handler);
      caster_handler = nullptr;
      memset(caster_mountpoint, 0, sizeof(caster_mountpoint));
      AddLog(LOG_LEVEL_INFO, PSTR("NTRIP: Removed old Caster mountpoint"));

      // Clear existing clients
      for (auto &client : remoteNtripClients)
      {
        if (client.client && client.client->connected())
        {
          client.client->close();
        }
      }
      remoteNtripClients.clear();
      remoteNtripClientsTotal = 0;

      // Add small delay to allow cleanup
      delay(100);
    }

    // Add new handler
    caster_handler = &web_server->on(mountpoint, HTTP_GET, handleCasterRequest);
    strncpy(caster_mountpoint, mountpoint, sizeof(caster_mountpoint));
    AddLog(LOG_LEVEL_INFO, PSTR("NTRIP: Added new Caster mountpoint at %s"), mountpoint);
  }
  else
  {
    AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: Caster endpoint not changed"));
  }
}

#endif // USE_WEBSERVER

void InitializeMessageStatistics()
{
  memset(message_statistics, 0, sizeof(message_statistics));
}

void UpdateMessageStatistics(uint16_t msg_type)
{
  for (int i = 0; i < MESSAGE_STATISTICS_COUNT - 1; i++)
  {
    if (message_statistics[i].message_type == msg_type)
    {
      message_statistics[i].message_count++;
      return;
    }
    if (message_statistics[i].message_type == 0)
    {
      message_statistics[i].message_type = msg_type;
      message_statistics[i].message_count = 1;
      return;
    }
  }
  message_statistics[MESSAGE_STATISTICS_COUNT - 1].message_count++;
}

void broadcastRTCMData(const uint8_t *buffer, size_t length)
{
  if (length == 0 || remoteNtripClients.empty())
  {
    return;
  }

  uint32_t currentTime = millis();

  // Clean up inactive clients
  remoteNtripClients.erase(
      std::remove_if(remoteNtripClients.begin(), remoteNtripClients.end(),
                     [currentTime](const RemoteNTRIPClient &client) -> bool
                     {
                       return !client.isActive ||
                              ((currentTime - client.lastDataTime) > CLIENT_TIMEOUT) ||
                              !client.client->connected();
                     }),
      remoteNtripClients.end());

  // Broadcast to remaining clients
  for (auto &client : remoteNtripClients)
  {
    if (client.isActive && client.client->connected() && client.client->canSend())
    {
      client.client->write(reinterpret_cast<const char *>(buffer), length);
      client.lastDataTime = currentTime;
      rtcmBytesForwarded += length;
    }
  }

  remoteNtripClientsTotal = remoteNtripClients.size();
}

void ProcessRTCMMessage(const uint8_t *data, size_t length)
{
  // RTCM messages must be at least 6 bytes (3 header + 3 CRC)
  if (length < 6 || data[0] != 0xD3)
    return;

  uint16_t msg_length = ((data[1] & 0x03) << 8) | data[2];
  if (msg_length + 6 != length)
    return; // Invalid length

  uint16_t msg_type = (data[3] << 4) | ((data[4] & 0xF0) >> 4);

  AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: RTCM type %d, length %d"), msg_type, msg_length);

  UpdateMessageStatistics(msg_type);

  // Forward to connected NTRIP casters
  for (uint8_t i = 0; i < 2; i++)
  {
    ntripClients[i].sendData(data, length);
  }

  // Broadcast to caster clients
  broadcastRTCMData(data, length);
}

void CheckNTRIPClients()
{
  for (uint8_t i = 0; i < 2; i++)
  {
    ntripClients[i].retryConnect();
  }
}

void RTCMShowWebSensor()
{
  if (message_statistics[0].message_type > 0)
  {
    WSContentSend_PD(PSTR("{s}RTCM Messages{m}{e}"));
    for (int i = 0; i < MESSAGE_STATISTICS_COUNT - 1; i++)
    {
      if (message_statistics[i].message_type > 0)
      {
        WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• Type %d{m}%d{e}"),
                         message_statistics[i].message_type,
                         message_statistics[i].message_count);
      }
    }
    if (message_statistics[MESSAGE_STATISTICS_COUNT - 1].message_count > 0)
    {
      WSContentSend_PD(PSTR("{s}&nbsp;&nbsp;• Other Types{m}%d{e}"),
                       message_statistics[MESSAGE_STATISTICS_COUNT - 1].message_count);
    }
    WSContentSend_PD(PSTR("{s}RTCM Bytes Forwarded{m}%d{e}"), rtcmBytesForwarded);
    WSContentSend_PD(PSTR("{s}NTRIP Connected Clients{m}%d{e}"), remoteNtripClientsTotal);

    for (uint8_t i = 0; i < 2; i++)
    {
      if (NtripSettings.server_settings[i].enabled)
      {
        WSContentSend_PD(PSTR("{s}NTRIP Client %d{m}%s{e}"),
                         i + 1,
                         ntripClients[i].isConnected() ? PSTR("Connected") : PSTR("Disconnected"));
      }
    }
  }
}

void RTCMShowJSON()
{
  ResponseAppend_P(PSTR(",\"RTCM\":{\"Messages\":["));

  bool first = true;
  for (int i = 0; i < MESSAGE_STATISTICS_COUNT - 1; i++)
  {
    if (message_statistics[i].message_type > 0)
    {
      if (!first)
      {
        ResponseAppend_P(PSTR(","));
      }
      ResponseAppend_P(PSTR("{\"type\":%d,\"count\":%d}"),
                       message_statistics[i].message_type,
                       message_statistics[i].message_count);
      first = false;
    }
  }

  ResponseAppend_P(PSTR("],\"Other\":%d,\"BytesForwarded\":%d,\"Clients\":%d}"),
                   message_statistics[MESSAGE_STATISTICS_COUNT - 1].message_count,
                   rtcmBytesForwarded,
                   remoteNtripClientsTotal);

  for (uint8_t i = 0; i < 2; i++)
  {
    if (i > 0)
      ResponseAppend_P(PSTR(","));
    ResponseAppend_P(PSTR("{\"enabled\":%d,\"connected\":%d}"),
                     NtripSettings.server_settings[i].enabled,
                     ntripClients[i].isConnected());
  }

  ResponseAppend_P(PSTR("]}"));
}

bool Xdrv93(uint32_t function)
{
  bool result = false;

  switch (function)
  {
  case FUNC_INIT:
    NtripSettingsLoad();
    InitializeMessageStatistics();
    ntrip_settings_initialized = true;
    initializeNTRIPClients();
    rtcmInitializeCasterEndpoint(nullptr);
    break;
  case FUNC_EVERY_100_MSECOND:
    CheckNTRIPClients();
    break;
  case FUNC_JSON_APPEND:
    RTCMShowJSON();
    break;
  case FUNC_SAVE_SETTINGS:
    NtripSettingsSave();
    break;
#ifdef USE_WEBSERVER
  case FUNC_WEB_SENSOR:
    RTCMShowWebSensor();
    break;
    // #ifdef USE_NTRIP_WEB_MENU
  case FUNC_WEB_ADD_BUTTON:
    WSContentSend_P(HTTP_BTN_MENU_NTRIP);
    break;
  case FUNC_WEB_ADD_HANDLER:
    WebServer_on(PSTR("/ntrip"), HandleNtripConfiguration);
    break;
// #endif // USE_NTRIP_WEB_MENU
#endif // USE_WEBSERVER
  case FUNC_COMMAND:
    break;
  }
  return result;
}

#endif // USE_NTRIP
#endif // ESP32
