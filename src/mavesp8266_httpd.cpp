/****************************************************************************

   Copyright (c) 2015, 2016 Gus Grubba. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
   COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
   OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.

 ****************************************************************************/

/**
   @file mavesp8266_httpd.cpp
   ESP8266 Wifi AP, MavLink UART/UDP Bridge

   @author Gus Grubba <mavlink@grubba.com>
*/

#include "mavesp8266.h"
#include "mavesp8266_httpd.h"
#include "mavesp8266_parameters.h"
#include "mavesp8266_gcs.h"
#include "mavesp8266_vehicle.h"
#include "mavesp8266_htmlTemplate.h"

#include <ESP8266WebServer.h>
#include <Hash.h>





const char* kBAUD       = "baud";
const char* kPWD        = "pwd";
const char* kSSID       = "ssid";
const char* kPWDSTA     = "pwdsta";
const char* kSSIDSTA    = "ssidsta";
const char* kIPSTA      = "ipsta";
const char* kGATESTA    = "gatewaysta";
const char* kSUBSTA     = "subnetsta";
const char* kCPORT      = "cport";
const char* kHPORT      = "hport";
const char* kCHANNEL    = "channel";
const char* kDEBUG      = "debug";
const char* kREBOOT     = "reboot";
const char* kPOSITION   = "position";
const char* kMODE       = "mode";
const char* kWEBACCOUNT   = "webaccount";
const char* kWEBPASSWORD       = "webpassword";

const char* kFlashMaps[7] = {
  "512KB (256/256)",
  "256KB",
  "1MB (512/512)",
  "2MB (512/512)",
  "4MB (512/512)",
  "2MB (1024/1024)",
  "4MB (1024/1024)"
};

static uint32_t flash = 0;
static char paramCRC[12] = {""};

ESP8266WebServer    webServer(80);
MavESP8266Update*   updateCB    = NULL;
bool                started     = false;

const char * headerkeys[] = {"User-Agent", "Cookie"} ;
size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);


//---------------------------------------------------------------------------------
void setNoCacheHeaders() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
}

//---------------------------------------------------------------------------------
void returnFail(String msg) {
  webServer.send(500, FPSTR(kTEXTPLAIN), msg + "\r\n");
}

//---------------------------------------------------------------------------------
void respondOK() {
  webServer.send(200, FPSTR(kTEXTPLAIN), "OK");
}
//---------------------------------------------------------------------------------
void response_HTML(uint32_t code, String mine, String body) {
  setNoCacheHeaders();
  String message = FPSTR(kTEMPLATE);
  message.replace("{body}", body);
  webServer.send(code, mine, message);
}
//---------------------------------------------------------------------------------
String get_SessionKey() {
  String key = FPSTR(getWorld()->getParameters()->getWebAccount());
  key += FPSTR(getWorld()->getParameters()->getWebPassword());
  return  sha1(key) ;
}
//---------------------------------------------------------------------------------
bool is_authentified() {
  if (webServer.hasHeader("Cookie")) {
    String cookie = webServer.header("Cookie");
    if (cookie.indexOf("ESPSESSIONID=" + get_SessionKey()) != -1) {
      return true;
    }
  }
  return false;
}


//---------------------------------------------------------------------------------
void handle_update() {
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  webServer.sendHeader("Connection", "close");
  webServer.sendHeader(FPSTR(kACCESSCTL), "*");
  response_HTML(200, FPSTR(kTEXTHTML), FPSTR(kUPLOADFORM));
}

//---------------------------------------------------------------------------------
void handle_upload() {
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  webServer.sendHeader("Connection", "close");
  webServer.sendHeader(FPSTR(kACCESSCTL), "*");
  response_HTML(200, FPSTR(kTEXTPLAIN), (Update.hasError()) ? "FAIL" : "OK");
  if (updateCB) {
    updateCB->updateCompleted();
  }
  ESP.restart();
}

//---------------------------------------------------------------------------------
void handle_upload_status() {

  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  bool success  = true;
  if (!started) {
    started = true;
    if (updateCB) {
      updateCB->updateStarted();
    }
  }
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.setDebugOutput(true);
#endif
    WiFiUDP::stopAll();
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.printf("Update: %s\n", upload.filename.c_str());
#endif
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
#ifdef DEBUG_SERIAL
      Update.printError(DEBUG_SERIAL);
#endif
      success = false;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
#ifdef DEBUG_SERIAL
      Update.printError(DEBUG_SERIAL);
#endif
      success = false;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
#ifdef DEBUG_SERIAL
      DEBUG_SERIAL.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
#endif
    } else {
#ifdef DEBUG_SERIAL
      Update.printError(DEBUG_SERIAL);
#endif
      success = false;
    }
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.setDebugOutput(false);
#endif
  }
  yield();
  if (!success) {
    if (updateCB) {
      updateCB->updateError();
    }
  }
}

//---------------------------------------------------------------------------------
void handle_getSystemConfig()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  String message = FPSTR(kSYSTEMCFG);
  message.replace("{baud}", String(getWorld()->getParameters()->getUartBaudRate()));
  message.replace("{debug}", String(getWorld()->getParameters()->getDebugEnabled()));
  message.replace("{mode}", String(getWorld()->getParameters()->getWifiMode()));
  message.replace("{webaccount}", FPSTR(getWorld()->getParameters()->getWebAccount()));
  message.replace("{webpassword}", FPSTR(getWorld()->getParameters()->getWebPassword()));
  response_HTML(200, FPSTR(kTEXTHTML), message);
}
//---------------------------------------------------------------------------------
void handle_getAPConfig()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  String message = FPSTR(kAPMODECFG);
  message.replace("{channel}", String(getWorld()->getParameters()->getWifiChannel()));
  message.replace("{ssid}", FPSTR(getWorld()->getParameters()->getWifiSsid()));
  message.replace("{pwd}", FPSTR(getWorld()->getParameters()->getWifiPassword()));
  message.replace("{hport}", String(getWorld()->getParameters()->getWifiUdpHport()));
  response_HTML(200, FPSTR(kTEXTHTML), message);
}
//---------------------------------------------------------------------------------
void handle_getSTAConfig()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  String message = FPSTR(kSTAMODECFG);
  message.replace("{ssidsta}", FPSTR(getWorld()->getParameters()->getWifiStaSsid()));
  message.replace("{pwdsta}", FPSTR(getWorld()->getParameters()->getWifiStaPassword()));
  message.replace("{ipsta}", String(getWorld()->getParameters()->getWifiStaIP()));
  message.replace("{cport}", String(getWorld()->getParameters()->getWifiUdpCport()));
  message.replace("{gatewaysta}", String(getWorld()->getParameters()->getWifiStaGateway()));
  message.replace("{subnetsta}", String(getWorld()->getParameters()->getWifiStaSubnet()));
  response_HTML(200, FPSTR(kTEXTHTML), message);
}
//---------------------------------------------------------------------------------
void handle_getStatus()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  if (!flash)
    flash = ESP.getFreeSketchSpace();
  if (!paramCRC[0]) {
    snprintf(paramCRC, sizeof(paramCRC), "%08X", getWorld()->getParameters()->paramHashCheck());
  }
  linkStatus* gcsStatus = getWorld()->getGCS()->getStatus();
  linkStatus* vehicleStatus = getWorld()->getVehicle()->getStatus();
  String message = "<p>Comm Status</p><table><tr><td width=\"240\">Packets Received from GCS</td><td>";
  message += gcsStatus->packets_received;
  message += "</td></tr><tr><td>Packets Sent to GCS</td><td>";
  message += gcsStatus->packets_sent;
  message += "</td></tr><tr><td>GCS Packets Lost</td><td>";
  message += gcsStatus->packets_lost;
  message += "</td></tr><tr><td>Packets Received from Vehicle</td><td>";
  message += vehicleStatus->packets_received;
  message += "</td></tr><tr><td>Packets Sent to Vehicle</td><td>";
  message += vehicleStatus->packets_sent;
  message += "</td></tr><tr><td>Vehicle Packets Lost</td><td>";
  message += vehicleStatus->packets_lost;
  message += "</td></tr><tr><td>Radio Messages</td><td>";
  message += gcsStatus->radio_status_sent;
  message += "</td></tr></table>";
  message += "<p>System Status</p><table><tr><td width=\"240\">Flash Memory Left</td><td>";
  message += flash;
  message += "</td></tr><tr><td>RAM Left</td><td>";
  message += String(ESP.getFreeHeap());
  message += "</td></tr><tr><td>Parameters CRC</td><td>";
  message += paramCRC;
  message += "</td></tr></table>";
  setNoCacheHeaders();
  response_HTML(200, FPSTR(kTEXTHTML), message);
}

//---------------------------------------------------------------------------------
void handle_getJLog()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  uint32_t position = 0, len;
  if (webServer.hasArg(kPOSITION)) {
    position = webServer.arg(kPOSITION).toInt();
  }
  String logText = getWorld()->getLogger()->getLog(&position, &len);
  char jStart[128];
  snprintf(jStart, 128, "{\"len\":%d, \"start\":%d, \"text\": \"", len, position);
  String payLoad = jStart;
  payLoad += logText;
  payLoad += "\"}";
  webServer.send(200, FPSTR(kAPPJSON), payLoad);
}

//---------------------------------------------------------------------------------
void handle_getJSysInfo()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  if (!flash)
    flash = ESP.getFreeSketchSpace();
  if (!paramCRC[0]) {
    snprintf(paramCRC, sizeof(paramCRC), "%08X", getWorld()->getParameters()->paramHashCheck());
  }
  uint32_t fid = spi_flash_get_id();
  char message[512];
  snprintf(message, 512,
           "{ "
           "\"size\": \"%s\", "
           "\"id\": \"0x%02lX 0x%04lX\", "
           "\"flashfree\": \"%u\", "
           "\"heapfree\": \"%u\", "
           "\"logsize\": \"%u\", "
           "\"paramcrc\": \"%s\""
           " }",
           kFlashMaps[system_get_flash_size_map()],
           fid & 0xff, (fid & 0xff00) | ((fid >> 16) & 0xff),
           flash,
           ESP.getFreeHeap(),
           getWorld()->getLogger()->getPosition(),
           paramCRC
          );
  webServer.send(200, "application/json", message);
}

//---------------------------------------------------------------------------------
void handle_getJSysStatus()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  bool reset = false;
  if (webServer.hasArg("r")) {
    reset = webServer.arg("r").toInt() != 0;
  }
  linkStatus* gcsStatus = getWorld()->getGCS()->getStatus();
  linkStatus* vehicleStatus = getWorld()->getVehicle()->getStatus();
  if (reset) {
    memset(gcsStatus,     0, sizeof(linkStatus));
    memset(vehicleStatus, 0, sizeof(linkStatus));
  }
  char message[512];
  snprintf(message, 512,
           "{ "
           "\"gpackets\": \"%u\", "
           "\"gsent\": \"%u\", "
           "\"glost\": \"%u\", "
           "\"vpackets\": \"%u\", "
           "\"vsent\": \"%u\", "
           "\"vlost\": \"%u\", "
           "\"radio\": \"%u\", "
           "\"buffer\": \"%u\""
           " }",
           gcsStatus->packets_received,
           gcsStatus->packets_sent,
           gcsStatus->packets_lost,
           vehicleStatus->packets_received,
           vehicleStatus->packets_sent,
           vehicleStatus->packets_lost,
           gcsStatus->radio_status_sent,
           vehicleStatus->queue_status
          );
  webServer.send(200, "application/json", message);
}
//---------------------------------------------------------------------------------
void handle_help() {
  response_HTML(200, FPSTR(kTEXTHTML), FPSTR(kHELPHTML));
}
//---------------------------------------------------------------------------------
void handle_setParameters()
{
  if (!is_authentified()) {
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  if (webServer.args() == 0) {
    returnFail(kBADARG);
    return;
  }
  bool ok = false;
  bool reboot = false;
  uint8_t cfgType=0;
  if (webServer.hasArg(kBAUD)) {
    ok = true;
	cfgType=1;
    getWorld()->getParameters()->setUartBaudRate(webServer.arg(kBAUD).toInt());
  }
  if (webServer.hasArg(kPWD)) {
    if (strlen(webServer.arg(kPWD).c_str()) >= 8) {
	  cfgType=2;
      ok = true;
      getWorld()->getParameters()->setWifiPassword(webServer.arg(kPWD).c_str());
    } else {
      ok = false;
    }
  }
  if (webServer.hasArg(kSSID)) {
    ok = true;
	cfgType=2;
    getWorld()->getParameters()->setWifiSsid(webServer.arg(kSSID).c_str());
  }
  if (webServer.hasArg(kPWDSTA)) {
    if (strlen(webServer.arg(kPWD).c_str()) >= 8) {
      ok = true;
	  cfgType=3;
      getWorld()->getParameters()->setWifiStaPassword(webServer.arg(kPWDSTA).c_str());
    } else {
      ok = false;
    }
  }
  if (webServer.hasArg(kSSIDSTA)) {
    ok = true;
	cfgType=3;
    getWorld()->getParameters()->setWifiStaSsid(webServer.arg(kSSIDSTA).c_str());
  }
  if (webServer.hasArg(kIPSTA)) {
    IPAddress ip;
    ip.fromString(webServer.arg(kIPSTA).c_str());
	cfgType=3;
	ok = true;
    getWorld()->getParameters()->setWifiStaIP(ip);
  }
  if (webServer.hasArg(kGATESTA)) {
    IPAddress ip;
    ip.fromString(webServer.arg(kGATESTA).c_str());
	cfgType=3;
	ok = true;
    getWorld()->getParameters()->setWifiStaGateway(ip);
  }
  if (webServer.hasArg(kSUBSTA)) {
    IPAddress ip;
    ip.fromString(webServer.arg(kSUBSTA).c_str());
	cfgType=3;
	ok = true;
    getWorld()->getParameters()->setWifiStaSubnet(ip);
  }
  if (webServer.hasArg(kCPORT)) {
    ok = true;
	cfgType=3;
    getWorld()->getParameters()->setWifiUdpCport(webServer.arg(kCPORT).toInt());
  }
  if (webServer.hasArg(kHPORT)) {
    ok = true;
	cfgType=2;
    getWorld()->getParameters()->setWifiUdpHport(webServer.arg(kHPORT).toInt());
  }
  if (webServer.hasArg(kCHANNEL)) {
    ok = true;
	cfgType=2;
    getWorld()->getParameters()->setWifiChannel(webServer.arg(kCHANNEL).toInt());
  }
  if (webServer.hasArg(kDEBUG)) {
    ok = true;
	cfgType=1;
    getWorld()->getParameters()->setDebugEnabled(webServer.arg(kDEBUG).toInt());
  }
  if (webServer.hasArg(kMODE)) {
    ok = true;
	cfgType=1;
    getWorld()->getParameters()->setWifiMode(webServer.arg(kMODE).toInt());
  }
  if (webServer.hasArg(kWEBACCOUNT)) {
    ok = true;
	cfgType=1;
    getWorld()->getParameters()->setWebAccount(webServer.arg(kWEBACCOUNT).c_str());
  }
  if (webServer.hasArg(kWEBPASSWORD)) {
    ok = true;
	cfgType=1;
    getWorld()->getParameters()->setWebPassword(webServer.arg(kWEBPASSWORD).c_str());
  }
  if (webServer.hasArg(kREBOOT)) {
    ok = true;
    reboot = webServer.arg(kREBOOT) == "1";
  }
  if (ok) {
    getWorld()->getParameters()->saveAllToEeprom();
    //-- Send new parameters back
	if(cfgType==1)
		handle_getSystemConfig();
	else if(cfgType==2)
		handle_getAPConfig();
	else if(cfgType==3)
		handle_getAPConfig();
	else
    returnFail("unknow error");
    if (reboot) {
      delay(100);
      ESP.restart();
    }
  } else
    returnFail(kBADARG);
}
//---------------------------------------------------------------------------------
//-- 404
void handle_notFound() {
  String message = "<label>URI: ";
  message += webServer.uri();
  message += "\nMethod: ";
  message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webServer.args();
  message += "\n</label>";

  for (uint8_t i = 0; i < webServer.args(); i++) {
    message += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
  }
  message +=	FPSTR(kERRORPage);
  response_HTML(404, FPSTR(kTEXTHTML), message);
}
//---------------------------------------------------------------------------------
void handle_Login() {
  String info = "";
  if (webServer.hasHeader("Cookie")) {
    String cookie = webServer.header("Cookie");
  }
  if (webServer.hasArg("DISCONNECT")) {
    String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=0\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    webServer.sendContent(header);
    return;
  }
  if (webServer.hasArg("USERNAME") && webServer.hasArg("PASSWORD")) {
    //getWorld()->getParameters()->getWebAccount()
    //getWorld()->getParameters()->getWebPassword()

    if (webServer.arg("USERNAME") == getWorld()->getParameters()->getWebAccount() &&  webServer.arg("PASSWORD") == getWorld()->getParameters()->getWebPassword() ) {
      String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=";
      header += get_SessionKey();
      header += "\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
      webServer.sendContent(header);
	  handle_getSystemConfig();
      return;
    }
    info = "Wrong username/password! try again.";
    info += FPSTR(getWorld()->getParameters()->getWebAccount());
    info += ",";
    info += FPSTR(getWorld()->getParameters()->getWebPassword());
  }
  String message = FPSTR(kLOGINFORM);
  message.replace("{msg}", info);
  response_HTML(200, "text/html", message);
}


//---------------------------------------------------------------------------------
MavESP8266Httpd::MavESP8266Httpd()
{

}

//---------------------------------------------------------------------------------
//-- Initialize
void
MavESP8266Httpd::begin(MavESP8266Update* updateCB_)
{
  updateCB = updateCB_;

  webServer.on("/login", handle_Login);
  webServer.on("/help", handle_help);
  webServer.on("/getsysconfig",  handle_getSystemConfig);
  webServer.on("/getapconfig",  handle_getAPConfig);
  webServer.on("/getstaconfig",  handle_getSTAConfig);
  webServer.on("/setparameters",  handle_setParameters);
  webServer.on("/getstatus",      handle_getStatus);
  webServer.on("/info.json",      handle_getJSysInfo);
  webServer.on("/status.json",    handle_getJSysStatus);
  webServer.on("/log.json",       handle_getJLog);
  webServer.on("/update",         handle_update);
  webServer.on("/upload",         HTTP_POST, handle_upload, handle_upload_status);
  webServer.onNotFound(handle_notFound);
  webServer.collectHeaders(headerkeys, headerkeyssize );
  webServer.begin();
}

//---------------------------------------------------------------------------------
//-- Initialize
void
MavESP8266Httpd::checkUpdates()
{
  webServer.handleClient();
}
