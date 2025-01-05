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
        "<span style='color:white;'>%s</span>"
        "<input type='checkbox' id='ntrip_server_enable_%d' "
        "onchange='toggleNtripFields(this, [\"ntrip_serv_host_%d\",\"ntrip_serv_port_%d\",\"ntrip_serv_mount_%d\",\"ntrip_serv_user_%d\",\"ntrip_serv_pass_%d\"])' "
        "style='margin:0;'/>"
      "</legend>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
        "<label for='ntrip_serv_host_%d' style='color:white;'>" D_NTRIP_HOST "</label>"
        "<input type='text' id='ntrip_serv_host_%d' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
        "<label for='ntrip_serv_port_%d' style='color:white;'>" D_NTRIP_PORT "</label>"
        "<input type='number' id='ntrip_serv_port_%d' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
        "<label for='ntrip_serv_mount_%d' style='color:white;'>" D_NTRIP_MOUNT "</label>"
        "<input type='text' id='ntrip_serv_mount_%d' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
      "</div>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
        "<label for='ntrip_serv_user_%d' style='color:white;'>" D_NTRIP_USERNAME "</label>"
        "<input type='text' id='ntrip_serv_user_%d' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
        "<label for='ntrip_serv_pass_%d' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
        "<input type='text' id='ntrip_serv_pass_%d' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
      "</div>"
    "</fieldset>";

const char HTTP_FORM_NTRIP_CASTER[] PROGMEM = 
    "<fieldset style='margin:10px 0;padding:15px;border:1px solid #666;'>"
      "<legend style='text-align:left; display:flex; align-items:center; gap:8px;'>"
        "<span style='color:white;'>" D_NTRIP_CASTER "</span>"
        "<input type='checkbox' id='ntrip_caster_enable' "
        "onchange='toggleNtripFields(this, [\"ntrip_cast_port\",\"ntrip_cast_mount\",\"ntrip_cast_user\",\"ntrip_cast_pass\"])' "
        "style='margin:0;'/>"
      "</legend>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr 100px 1fr; gap:10px; align-items:center; margin-bottom:10px;'>"
        "<label for='ntrip_cast_port' style='color:white;'>" D_NTRIP_PORT "</label>"
        "<input type='number' id='ntrip_cast_port' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
        "<label for='ntrip_cast_mount' style='color:white;'>" D_NTRIP_MOUNT "</label>"
        "<input type='text' id='ntrip_cast_mount' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
      "</div>"
      "<div style='display:grid; grid-template-columns:100px 1fr 100px 1fr; gap:10px; align-items:center;'>"
        "<label for='ntrip_cast_user' style='color:white;'>" D_NTRIP_USERNAME "</label>"
        "<input type='text' id='ntrip_cast_user' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
        "<label for='ntrip_cast_pass' style='color:white;'>" D_NTRIP_PASSWORD "</label>"
        "<input type='text' id='ntrip_cast_pass' style='width:100%%; background:#333; color:white; border:1px solid #555; padding:5px; transition:all 0.3s ease;' disabled class='ntrip-input'/>"
      "</div>"
    "</fieldset>";
  
void Ntrip_Save_Settings(void) {

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
   
    // Create server1 HTML - note there are now 17 %d parameters
    char* server1 = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_SERVER,
        D_NTRIP_SERVER_1, 1,1,1,1,1,1,1, 1,1,1,1,1,1, 1,1,1,1);
    if (!server1) {
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for server1"));
        return;
    }
   
    // Add server1 and its onload script
    char* script1 = ext_vsnprintf_malloc_P_wrapper(HTTP_NTRIP_SCRIPT_ONLOAD, "ntrip_serv", 1);
    if (!script1) {
        free(server1);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for script1"));
        return;
    }
   
    // Create server2 HTML - note there are now 17 %d parameters
    char* server2 = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_SERVER,
        D_NTRIP_SERVER_2, 2,2,2,2,2,2,2, 2,2,2,2,2,2, 2,2,2,2);
    if (!server2) {
        free(server1);
        free(script1);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for server2"));
        return;
    }
   
    // Add server2 and its onload script
    char* script2 = ext_vsnprintf_malloc_P_wrapper(HTTP_NTRIP_SCRIPT_ONLOAD, "ntrip_serv", 2);
    if (!script2) {
        free(server1);
        free(script1);
        free(server2);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for script2"));
        return;
    }

    char* caster = ext_vsnprintf_malloc_P_wrapper(HTTP_FORM_NTRIP_CASTER);
    if (!caster) {
        free(server1);
        free(script1);
        free(server2);
        free(script2);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for caster"));
        return;
    }
   
    // Combine all content and wrap it in the main form
    char* all_content = ext_vsnprintf_malloc_P_wrapper(PSTR("%s%s%s%s%s"),
        server1, script1, server2, script2, caster);
       
    if (!all_content) {
        free(server1);
        free(script1);
        free(server2);
        free(script2);
        free(caster);
        AddLog(LOG_LEVEL_ERROR, PSTR("Failed to allocate memory for all content"));
        return;
    }
   
    WSContentSend(HTTP_NTRIP_SCRIPT, strlen_P(HTTP_NTRIP_SCRIPT));
    WSContentSend_P(HTTP_FORM_NTRIP, all_content);
   
    // Clean up
    free(server1);
    free(script1);
    free(server2);
    free(script2);
    free(caster);
    free(all_content);
}

#endif USE_WEBSERVER


bool Xdrv93(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      break;
    case FUNC_EVERY_100_MSECOND:
      break;
    case FUNC_JSON_APPEND:
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
