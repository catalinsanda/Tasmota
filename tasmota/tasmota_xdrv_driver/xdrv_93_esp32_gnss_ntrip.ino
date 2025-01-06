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

#define XDRV_93                 93

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#ifdef USE_WEBSERVER

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
        "<span style='color:white;'>%s</span>"  // Server title
        "<input type='checkbox' id='ntrip_server_enable_%d' name='ntrip_server_enable_%d' "
        "onchange='toggleNtripFields(this, [\"ntrip_serv_host_%d\",\"ntrip_serv_port_%d\",\"ntrip_serv_mount_%d\",\"ntrip_serv_user_%d\",\"ntrip_serv_pass_%d\"])' "
        "%s "  // 'checked' if enabled
        "style='margin:0;'/>"
      "</legend>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
        "<label for='ntrip_serv_host_%d' style='color:white;'>" D_NTRIP_HOST "</label>"
        "<input type='text' id='ntrip_serv_host_%d' name='ntrip_serv_host_%d' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
        
        "<label for='ntrip_serv_port_%d' style='color:white;'>" D_NTRIP_PORT "</label>"
        "<input type='number' id='ntrip_serv_port_%d' name='ntrip_serv_port_%d' value='%d' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
        
        "<label for='ntrip_serv_mount_%d' style='color:white;'>" D_NTRIP_MOUNT "</label>"
        "<input type='text' id='ntrip_serv_mount_%d' name='ntrip_serv_mount_%d' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
      "</div>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
        "<label for='ntrip_serv_user_%d' style='color:white;'>" D_NTRIP_USERNAME "</label>"
        "<input type='text' id='ntrip_serv_user_%d' name='ntrip_serv_user_%d' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
        
        "<label for='ntrip_serv_pass_%d' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
        "<input type='text' id='ntrip_serv_pass_%d' name='ntrip_serv_pass_%d' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
      "</div>"
    "</fieldset>";

const char HTTP_FORM_NTRIP_CASTER[] PROGMEM = 
    "<fieldset style='margin:10px 0;padding:15px;border:1px solid #666;'>"
      "<legend style='text-align:left; display:flex; align-items:center; gap:8px;'>"
        "<span style='color:white;'>" D_NTRIP_CASTER "</span>"
        "<input type='checkbox' id='ntrip_caster_enable' name='ntrip_caster_enable' "
        "onchange='toggleNtripFields(this, [\"ntrip_cast_port\",\"ntrip_cast_mount\",\"ntrip_cast_user\",\"ntrip_cast_pass\"])' "
        "%s "  // 'checked' if enabled
        "style='margin:0;'/>"
      "</legend>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
        "<label for='ntrip_cast_port' style='color:white;'>" D_NTRIP_PORT "</label>"
        "<input type='number' id='ntrip_cast_port' name='ntrip_cast_port' value='%d' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
        
        "<label for='ntrip_cast_mount' style='color:white;'>" D_NTRIP_MOUNT "</label>"
        "<input type='text' id='ntrip_cast_mount' name='ntrip_cast_mount' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
      "</div>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
        "<label for='ntrip_cast_user' style='color:white;'>" D_NTRIP_USERNAME "</label>"
        "<input type='text' id='ntrip_cast_user' name='ntrip_cast_user' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
        
        "<label for='ntrip_cast_pass' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
        "<input type='text' id='ntrip_cast_pass' name='ntrip_cast_pass' value='%s' %s "
        "style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' class='ntrip-input'/>"
      "</div>"
      "<br>"
    "</fieldset>";

typedef struct __ntrip_server_settings {
  uint16_t port;
  bool enabled;
  char host[33];
  char mountpoint[33];
  char username[33];
  char password[33];
} ntrip_server_settings_t;
  
typedef struct __ntrip_caster_settings {
  uint16_t port;
  bool enabled;
  char mountpoint[33];
  char username[33];
  char password[33];
} ntrip_caster_settings_t;

typedef struct __ntrip_settings {
  uint32_t crc32;
  uint32_t version;
  ntrip_server_settings_t server_settings[2];
  ntrip_caster_settings_t caster_settings;
} ntrip_settings_t;

ntrip_settings_t NtripSettings;

void NtripSettingsSave(void) {
  // Calculate CRC32 of everything except the CRC itself
  uint32_t crc32 = GetCfgCrc32((uint8_t*)&NtripSettings + 4, sizeof(NtripSettings) - 4);
  
  if (crc32 != NtripSettings.crc32) {
    NtripSettings.crc32 = crc32;
    
    char filename[20];
    snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_93);
    
    AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: Saving settings to file %s"), filename);
    
#ifdef USE_UFILESYS
    if (!TfsSaveFile(filename, (const uint8_t*)&NtripSettings, sizeof(NtripSettings))) {
      AddLog(LOG_LEVEL_INFO, D_ERROR_FILE_NOT_FOUND);
    }
#else
    AddLog(LOG_LEVEL_INFO, D_ERROR_FILESYSTEM_NOT_READY);
#endif  // USE_UFILESYS
  }
}

void NtripSettingsLoad(void) {
  char filename[20];
  snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_93);
  
#ifdef USE_UFILESYS
  if (TfsLoadFile(filename, (uint8_t*)&NtripSettings, sizeof(NtripSettings))) {
    uint32_t crc32 = GetCfgCrc32((uint8_t*)&NtripSettings + 4, sizeof(NtripSettings) - 4);
    if (crc32 != NtripSettings.crc32) {
      // CRC mismatch - initialize with defaults
      memset(&NtripSettings, 0, sizeof(NtripSettings));
      NtripSettings.version = 1;
    }
  } else {
    // File not found - initialize with defaults
    memset(&NtripSettings, 0, sizeof(NtripSettings));
    NtripSettings.version = 1;
  }
#endif  // USE_UFILESYS
}

void Ntrip_Save_Settings(void) {
  String stmp;
  bool needs_save = false;
  const uint8_t NUM_SERVERS = 2;

  // Server settings
  for (uint8_t server_idx = 0; server_idx < NUM_SERVERS; server_idx++) {
    char field_name[32];
    uint8_t server_num = server_idx + 1;

    snprintf(field_name, sizeof(field_name), "ntrip_server_enable_%d", server_num);
    if (Webserver->hasArg(field_name)) {
      needs_save |= (NtripSettings.server_settings[server_idx].enabled != true);
      NtripSettings.server_settings[server_idx].enabled = true;

      // Host
      snprintf(field_name, sizeof(field_name), "ntrip_serv_host_%d", server_num);
      if (Webserver->hasArg(field_name)) {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].host, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].host, stmp.c_str(), 
                sizeof(NtripSettings.server_settings[server_idx].host));
      }

      // Port
      snprintf(field_name, sizeof(field_name), "ntrip_serv_port_%d", server_num);
      if (Webserver->hasArg(field_name)) {
        stmp = Webserver->arg(field_name);
        uint16_t port = stmp.toInt();
        needs_save |= (NtripSettings.server_settings[server_idx].port != port);
        NtripSettings.server_settings[server_idx].port = port;
      }

      // Mountpoint
      snprintf(field_name, sizeof(field_name), "ntrip_serv_mount_%d", server_num);
      if (Webserver->hasArg(field_name)) {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].mountpoint, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].mountpoint, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].mountpoint));
      }

      // Username
      snprintf(field_name, sizeof(field_name), "ntrip_serv_user_%d", server_num);
      if (Webserver->hasArg(field_name)) {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].username, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].username, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].username));
      }

      // Password
      snprintf(field_name, sizeof(field_name), "ntrip_serv_pass_%d", server_num);
      if (Webserver->hasArg(field_name)) {
        stmp = Webserver->arg(field_name);
        needs_save |= (strcmp(NtripSettings.server_settings[server_idx].password, stmp.c_str()) != 0);
        strlcpy(NtripSettings.server_settings[server_idx].password, stmp.c_str(),
                sizeof(NtripSettings.server_settings[server_idx].password));
      }
    } else {
      needs_save |= (NtripSettings.server_settings[server_idx].enabled != false);
      NtripSettings.server_settings[server_idx].enabled = false;
    }
  }

  // Caster settings
  if (Webserver->hasArg("ntrip_caster_enable")) {
    needs_save |= (NtripSettings.caster_settings.enabled != true);
    NtripSettings.caster_settings.enabled = true;

    if (Webserver->hasArg("ntrip_cast_port")) {
      stmp = Webserver->arg("ntrip_cast_port");
      uint16_t port = stmp.toInt();
      needs_save |= (NtripSettings.caster_settings.port != port);
      NtripSettings.caster_settings.port = port;
    }

    if (Webserver->hasArg("ntrip_cast_mount")) {
      stmp = Webserver->arg("ntrip_cast_mount");
      needs_save |= (strcmp(NtripSettings.caster_settings.mountpoint, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.mountpoint, stmp.c_str(), 
              sizeof(NtripSettings.caster_settings.mountpoint));
    }

    if (Webserver->hasArg("ntrip_cast_user")) {
      stmp = Webserver->arg("ntrip_cast_user");
      needs_save |= (strcmp(NtripSettings.caster_settings.username, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.username, stmp.c_str(),
              sizeof(NtripSettings.caster_settings.username));
    }

    if (Webserver->hasArg("ntrip_cast_pass")) {
      stmp = Webserver->arg("ntrip_cast_pass");
      needs_save |= (strcmp(NtripSettings.caster_settings.password, stmp.c_str()) != 0);
      strlcpy(NtripSettings.caster_settings.password, stmp.c_str(),
              sizeof(NtripSettings.caster_settings.password));
    }
  } else {
    needs_save |= (NtripSettings.caster_settings.enabled != false);
    NtripSettings.caster_settings.enabled = false;
  }

  if (needs_save) {
    AddLog(LOG_LEVEL_DEBUG, PSTR("NTRIP: Settings changed, saving"));
    NtripSettingsSave();
  }
}

char* ext_vsnprintf_malloc_P_wrapper(const char* fmt_P, ...) {
    va_list args;
    va_start(args, fmt_P);
    char* result = ext_vsnprintf_malloc_P(fmt_P, args);
    va_end(args);
    return result;
}

void HandleNtripConfiguration(void) {
    if (!HttpCheckPriviledgedAccess()) { return; }
   
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_NTRIP));
   
    if (Webserver->hasArg("save")) {
        Ntrip_Save_Settings();
        HandleConfiguration();
        return;
    }

    WSContentStart_P(PSTR(D_CONFIGURE_NTRIP));
    WSContentSendStyle();
    
    const uint8_t NUM_SERVERS = 2;
    const char* disabled_attr = "disabled";
   
    // Create servers HTML
    char* servers_html = nullptr;
    for (uint8_t server_idx = 0; server_idx < NUM_SERVERS; server_idx++) {
        uint8_t server_num = server_idx + 1;
        const ntrip_server_settings_t* server = &NtripSettings.server_settings[server_idx];
        
        char* server_html = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_SERVER,
            // Title and checkbox
            server_idx == 0 ? D_NTRIP_SERVER_1 : D_NTRIP_SERVER_2,
            server_num, server_num, // id and name
            server_num, server_num, server_num, server_num, server_num, // toggleNtripFields ids
            server->enabled ? "checked" : "",
            
            // Host field
            server_num, server_num, server_num, 
            server->host,
            server->enabled ? "" : disabled_attr,
            
            // Port field
            server_num, server_num, server_num,
            server->port,
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
            server->enabled ? "" : disabled_attr
        );
        
        if (!server_html) {
            if (servers_html) free(servers_html);
            AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for server HTML"));
            return;
        }
        
        if (servers_html) {
            char* new_html = ext_vsnprintf_malloc_P_wrapper(PSTR("%s%s"), servers_html, server_html);
            free(servers_html);
            free(server_html);
            if (!new_html) {
                AddLog(LOG_LEVEL_ERROR, PSTR("Failed to combine server HTML"));
                return;
            }
            servers_html = new_html;
        } else {
            servers_html = server_html;
        }
    }
    
    // Create caster HTML
    char* caster_html = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_CASTER,
        // Checkbox
        NtripSettings.caster_settings.enabled ? "checked" : "",
        
        // Port field
        NtripSettings.caster_settings.port,
        NtripSettings.caster_settings.enabled ? "" : disabled_attr,
        
        // Mount field
        NtripSettings.caster_settings.mountpoint,
        NtripSettings.caster_settings.enabled ? "" : disabled_attr,
        
        // Username field
        NtripSettings.caster_settings.username,
        NtripSettings.caster_settings.enabled ? "" : disabled_attr,
        
        // Password field
        NtripSettings.caster_settings.password,
        NtripSettings.caster_settings.enabled ? "" : disabled_attr
    );

    if (!caster_html) {
        if (servers_html) free(servers_html);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for caster HTML"));
        return;
    }
    
    // Combine all content
    char* all_content = ext_vsnprintf_malloc_P_wrapper(PSTR("%s%s"), servers_html, caster_html);
    if (!all_content) {
        if (servers_html) free(servers_html);
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

#endif USE_WEBSERVER


bool Xdrv93(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      NtripSettingsLoad();
      break;
    case FUNC_EVERY_100_MSECOND:
      break;
    case FUNC_JSON_APPEND:
      break;
    case FUNC_SAVE_SETTINGS:
      NtripSettingsSave();
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
      break;
// #ifdef USE_NTRIP_WEB_MENU
    case FUNC_WEB_ADD_BUTTON:
      WSContentSend_P(HTTP_BTN_MENU_NTRIP);
      break;
      case FUNC_WEB_ADD_HANDLER:
        WebServer_on(PSTR("/ntrip"), HandleNtripConfiguration);
        break;
// #endif // USE_NTRIP_WEB_MENU
#endif  // USE_WEBSERVER
    case FUNC_COMMAND:
      break;
  }
  return result;
}


#endif  // USE_NTRIP
#endif  // ESP32
