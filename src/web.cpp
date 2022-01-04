#include <Arduino.h>
#include <SPI.h>
#include "RFV3.h"
#include "main_variables.h"
#include "cc1101_spi.h"
#include "cc1101.h"
#include "class.h"
#include "compression.h"
#include "interval_timer.h"
#include "web.h"
#include "utils.h"
#include <FS.h>
#if defined(ESP32)
#include "SPIFFS.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#endif
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266mDNS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <HTTPClient.h>

#include "trans_assist.h"
#include "settings.h"

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager/tree/feature_asyncwebserver

extern uint8_t data_to_send[];

const char *http_username = "admin";
const char *http_password = "admin";
AsyncWebServer server(80);
HTTPClient http;

void WriteBMP(const char *filename, uint8_t *pData, int width, int height, int bpp)
{
  int lsize = 0, i, iHeaderSize, iBodySize;
  uint8_t pBuf[128]; // holds BMP header
  File file_out = SPIFFS.open(filename, "wb");

  lsize = (lsize + 3) & 0xfffc; // DWORD aligned
  iHeaderSize = 54;
  iHeaderSize += (1 << (bpp + 2));
  iBodySize = lsize * height;
  i = iBodySize + iHeaderSize; // datasize
  memset(pBuf, 0, 54);
  pBuf[0] = 'B';
  pBuf[1] = 'M';
  pBuf[2] = i & 0xff; // 4 bytes of file size
  pBuf[3] = (i >> 8) & 0xff;
  pBuf[4] = (i >> 16) & 0xff;
  pBuf[5] = (i >> 24) & 0xff;
  /* Offset to data bits */
  pBuf[10] = iHeaderSize & 0xff;
  pBuf[11] = (unsigned char)(iHeaderSize >> 8);
  pBuf[14] = 0x28;
  pBuf[18] = width & 0xff;                // xsize low
  pBuf[19] = (unsigned char)(width >> 8); // xsize high
  i = -height;                            // top down bitmap
  pBuf[22] = i & 0xff;                    // ysize low
  pBuf[23] = (unsigned char)(i >> 8);     // ysize high
  pBuf[24] = 0xff;
  pBuf[25] = 0xff;
  pBuf[26] = 1; // number of planes
  pBuf[28] = (uint8_t)bpp;
  pBuf[30] = 0; // uncompressed
  i = iBodySize;
  pBuf[34] = i & 0xff; // data size
  pBuf[35] = (i >> 8) & 0xff;
  pBuf[36] = (i >> 16) & 0xff;
  pBuf[37] = (i >> 24) & 0xff;
  pBuf[54] = pBuf[55] = pBuf[56] = pBuf[57] = pBuf[61] = 0; // palette
  pBuf[58] = pBuf[59] = pBuf[60] = 0xff;
  {
    uint8_t *s = pData;
    file_out.write(pBuf, iHeaderSize);
    for (i = 0; i < height; i++)
    {
      file_out.write(s, lsize);
      s += lsize;
    }
    file_out.close();
  }
} /* WriteBMP() */

void init_web()
{
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  bool res;
  res = wm.autoConnect("AutoConnectAP");
  if (!res)
  {
    Serial.println("Failed to connect");
    ESP.restart();
  }
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // Make accessible via http://web-tag.local using mDNS responder
  if (!MDNS.begin("web-tag"))
    {
    Serial.println("Error setting up mDNS responder!");
    while(1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  MDNS.addService("http", "tcp", 80);

  server.addHandler(new SPIFFSEditor(SPIFFS, http_username, http_password));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/set_file", HTTP_POST, [](AsyncWebServerRequest *request) {
    int id;
    String filename;
    if (request->hasParam("id") && request->hasParam("file"))
    {
      id = request->getParam("id")->value().toInt();
      filename = request->getParam("file")->value();

      if (!SPIFFS.exists("/" + filename))
      {
        request->send(200, "text/plain", "Error opening file");
        return;
      }

      int size = set_trans_file("/" + filename);
      if (size)
      {
        set_is_data_waiting(id);
      }
      request->send(200, "text/plain", "OK cmd to display " + String(id) + " File: " + filename + " Len: " + String(size));
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  // Call custom function to generate a dynamic display
  server.on("/set_url", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println("Entering set_url");
    if (!request->hasParam("id") || !request->hasParam("url"))
    {
      request->send(400, "text/plain", "Wrong parameter");
      return;
    }

    long id = request->getParam("id")->value().toInt();
    String url = request->getParam("url")->value();
    Serial.printf("Loading %s for display %d\n", url.c_str(), (int)id);
    
    String bwFileName = "/tmp-dl.bmp";
    String colorFileName = ""; //disabled
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode <= 0) {
      http.end();
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      request->send(500, "text/plain", "HTTP request failed");
      return;
    }
    if (httpCode != HTTP_CODE_OK) {
      http.end();
      Serial.printf("[HTTP] GET... wrong code: %d\n", httpCode);
      request->send(500, "text/plain", "Wrong HTTP code");
      return;
    }

    File bwFile = SPIFFS.open(bwFileName, "w");
    if (!bwFile) {
      http.end();
      Serial.printf("[HTTP] GET... file missing!\n");
      request->send(500, "text/plain", "FS bug");
      return;
    }

    http.writeToStream(&bwFile);
    bwFile.close();
    http.end();

    
    if (request->hasParam("url_color"))
    {
      String colorUrl = request->getParam("url_color")->value();
      if (!colorUrl.isEmpty()) {
        colorFileName = "/tmp-dl-col.bmp";
        Serial.printf("Loading color %s for display %d\n", colorUrl.c_str(), (int)id);
        http.begin(colorUrl);
        int httpCode = http.GET();
        if (httpCode <= 0) {
          http.end();
          Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          request->send(500, "text/plain", "HTTP request failed");
          return;
        }
        if (httpCode != HTTP_CODE_OK) {
          http.end();
          Serial.printf("[HTTP] GET... wrong code: %d\n", httpCode);
          request->send(500, "text/plain", "Wrong HTTP code");
          return;
        }

        File colFile = SPIFFS.open(colorFileName, "w");
        if (!colFile) {
          http.end();
          Serial.printf("[HTTP] GET... file missing!\n");
          request->send(500, "text/plain", "FS bug");
          return;
        }

        http.writeToStream(&colFile);
        colFile.close();
        http.end();
      }
    }
    
    int iCompressedLen = load_img_to_bufer(bwFileName, colorFileName, false);

    if (iCompressedLen) {
      set_is_data_waiting(id);
      request->send(200, "text/plain", "OK cmd to display " + String(id) + " URL: " + url + " Len: " + String(iCompressedLen));
    }
    else {
      request->send(500, "text/plain", "Something wrong with the file");
    }
  });

  server.on("/set_bmp_file", HTTP_POST, [](AsyncWebServerRequest *request) {
    int id;
    int iCompressedLen = 0;
    int save_compressed_file_to_spiffs = 0;
    String filename = "";
    String filename_color = "";
    if (!request->hasParam("id") || !request->hasParam("file"))
    {
      request->send(400, "text/plain", "Wrong parameter");
      return;
    }
    id = request->getParam("id")->value().toInt();
    if (request->hasParam("save_comp_file"))
    {
      save_compressed_file_to_spiffs = (request->getParam("save_comp_file")->value().toInt() ? 1 : 0);
    }
    filename = request->getParam("file")->value();
    if (!SPIFFS.exists("/" + filename))
    {
      request->send(500, "text/plain", "Error opening file");
      return;
    }

    if (request->hasParam("file1"))
    {
      filename_color = request->getParam("file1")->value();
      if (filename_color != "" && !SPIFFS.exists("/" + filename_color))
      {
        request->send(500, "text/plain", "Error opening color file");
        return;
      }
    }
    iCompressedLen = load_img_to_bufer("/" + filename, "/" + filename_color, save_compressed_file_to_spiffs);

    if (iCompressedLen)
    {
      set_is_data_waiting(id);
      request->send(200, "text/plain", "OK cmd to display " + String(id) + " File: " + filename + " Len: " + String(iCompressedLen));
    }
    else
    {
      request->send(500, "text/plain", "Something wrong with the file");
    }
    return;
  });

  server.on("/set_cmd", HTTP_POST, [](AsyncWebServerRequest *request) {
    int id;
    String cmd;
    if (request->hasParam("id") && request->hasParam("cmd"))
    {
      id = request->getParam("id")->value().toInt();
      cmd = request->getParam("cmd")->value();
      int cmd_len = cmd.length() / 2;

      uint8_t temp_buffer[cmd_len + 1];
      hexCharacterStringToBytes(temp_buffer, cmd);
      set_trans_buffer(temp_buffer, cmd_len);
      set_is_data_waiting(id);
      set_last_send_status(1);
      request->send(200, "text/plain", "OK cmd to display " + String(id) + " " + cmd + " Len: " + String(cmd_len));
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/get_answer", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", get_last_receive_string());
  });

  server.on("/activate_display", HTTP_POST, [](AsyncWebServerRequest *request) {
    String serial;
    int id;
    uint8_t serial_array[7];
    if (request->hasParam("serial") && request->hasParam("id"))
    {
      serial = request->getParam("serial")->value();
      int serial_len = serial.length();
      if (serial_len == 11)
      {
        id = request->getParam("id")->value().toInt();

        set_is_data_waiting(0);

        if (get_trans_mode())
        {
          set_trans_mode(0);
          restore_current_settings();
          set_last_activation_status(0);
          reset_full_sync_count();
        }

        set_display_id(id);

        serial_array[0] = serial[0];
        serial_array[1] = serial[1];
        serial_array[2] = (serial[2] - 0x30) << 4 | (serial[3] - 0x30);
        serial_array[3] = (serial[4] - 0x30) << 4 | (serial[5] - 0x30);
        serial_array[4] = (serial[6] - 0x30) << 4 | (serial[7] - 0x30);
        serial_array[5] = (serial[8] - 0x30) << 4 | (serial[9] - 0x30);
        serial_array[6] = serial[10];

        set_serial(serial_array);

        if (serial_array[6] == 'C')
          set_mode_wun_activation();
        else
          set_mode_wu_activation();

        request->send(200, "text/plain", "OK activating new display " + serial + " to id: " + String(id));
        return;
      }
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/recover_display", HTTP_POST, [](AsyncWebServerRequest *request) {
    String serial;
    uint8_t serial_array[7];
    if (request->hasParam("serial"))
    {
      serial = request->getParam("serial")->value();
      int serial_len = serial.length();
      if (serial_len == 11)
      {

        set_is_data_waiting(0);

        if (get_trans_mode())
        {
          set_trans_mode(0);
          restore_current_settings();
          set_last_activation_status(0);
          reset_full_sync_count();
        }

        serial_array[0] = serial[0];
        serial_array[1] = serial[1];
        serial_array[2] = (serial[2] - 0x30) << 4 | (serial[3] - 0x30);
        serial_array[3] = (serial[4] - 0x30) << 4 | (serial[5] - 0x30);
        serial_array[4] = (serial[6] - 0x30) << 4 | (serial[7] - 0x30);
        serial_array[5] = (serial[8] - 0x30) << 4 | (serial[9] - 0x30);
        serial_array[6] = serial[10];

        set_serial(serial_array);

        set_mode_wu_reset();

        request->send(200, "text/plain", "OK trying to recover display " + serial);
        return;
      }
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/get_mode", HTTP_GET, [](AsyncWebServerRequest *request) {
    String acti_status = "";
    String send_status = "";
    switch (get_last_activation_status())
    {
    case 0:
      acti_status = "not started";
      break;
    case 1:
      acti_status = "started";
      break;
    case 2:
      acti_status = "timeout";
      break;
    case 3:
      acti_status = "successful";
      break;
    default:
      acti_status = "Error";
      break;
    }
    switch (get_last_send_status())
    {
    case 0:
      send_status = "nothing send";
      break;
    case 1:
      send_status = "in sending";
      break;
    case 2:
      send_status = "timeout";
      break;
    case 3:
      send_status = "successful";
      break;
    default:
      send_status = "Error";
      break;
    }

    request->send(200, "text/plain", "Send: " + send_status + " , waiting: " + String(get_is_data_waiting_raw()) + "<br>Activation: " + acti_status + "<br>NetID " + String(get_network_id()) + " freq " + String(get_freq()) + " slot " + String(get_slot_address()) + " bytes left: " + String(get_still_to_send()) + " Open: " + String(get_trans_file_open()) + "<br>last answer: " + get_last_receive_string() + "<br>mode " + get_mode_string());
  });

  server.on("/set_mode", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("mode"))
    {
      String new_mode = request->getParam("mode")->value();
      if (new_mode == "idle")
      {
        set_is_data_waiting(0);
        set_mode_idle();
      }
      else if (new_mode == "wusync")
      {
        set_is_data_waiting(0);
        set_mode_wu();
      }
      else if (new_mode == "fullsync")
      {
        set_is_data_waiting(0);
        set_mode_full_sync();
      }
      else
      {
        return;
      }
      request->send(200, "text/plain", "Ok set mode");
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/set_id", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("freq") && request->hasParam("net_id"))
    {
      int sniff_freq = request->getParam("freq")->value().toInt();
      int sniff_net_id = request->getParam("net_id")->value().toInt();
      request->send(200, "text/plain", "Ok set IDs Frq: " + String(sniff_freq) + " NetID : " + String(sniff_net_id));
      set_freq(sniff_freq);
      set_network_id(sniff_net_id);
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/set_wu_channel", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("freq"))
    {
      int wu_freq = request->getParam("freq")->value().toInt();
      request->send(200, "text/plain", "Ok set WU Channel: " + String(wu_freq));
      set_wu_channel(wu_freq);
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK Reboot");
    ESP.restart();
  });

  server.on("/delete_file", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK delete file");
    deleteFile("/answers.txt");
    deleteFile("/");
  });

  server.on("/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "SETTINGS:CHANNEL:" + String(get_freq()) + ":NET_ID:" + String(get_network_id()) + ":SLOTS:" + String(get_num_slots() + 1) + ":FREQ_OFFSET:" + String(get_freq_offset()));
  });

  server.on("/save_settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK saving settings");
    save_settings_to_flash();
  });

  server.on("/delete_settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK delete settings");
    delete_settings_file();
  });

  server.on("/set_num_slot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("num_slots"))
    {
      int num_slots = request->getParam("num_slots")->value().toInt();
      request->send(200, "text/plain", "Ok set num_slots: " + String(num_slots));
      set_num_slot(num_slots);
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.on("/set_freq_offset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("offset"))
    {
      int freq_offset = request->getParam("offset")->value().toInt();
      request->send(200, "text/plain", "Ok set freq_offset: " + String(freq_offset));
      CC1101_set_freq_offset(freq_offset);
      return;
    }
    request->send(200, "text/plain", "Wrong parameter");
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.begin();
}
