/*
  NetworkManager.cpp - Glow Worm Luciferin for Firefly Luciferin
  All in one Bias Lighting system for PC

  Copyright © 2020 - 2023  Davide Perini

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "NetworkManager.h"

uint16_t NetworkManager::part = 1;
[[maybe_unused]] boolean NetworkManager::firmwareUpgrade = false;
size_t NetworkManager::updateSize = 0;
String NetworkManager::fpsData;

/**
 * Parse UDP packet
 */
void NetworkManager::getUDPStream() {
  yield();
  if (!servingWebPages) {
    // If packet received...
    uint16_t packetSize = UDP.parsePacket();
    UDP.read(packet, UDP_MAX_BUFFER_SIZE);
    if (effect == Effect::GlowWormWifi) {
      if (packetSize > 20) {
        packet[packetSize] = '\0';
        fromUDPStreamToStrip(packet);
      }
    }
    // If packet received...
    uint16_t packetSizeBroadcast = broadcastUDP.parsePacket();
    broadcastUDP.read(packetBroadcast, UDP_BR_MAX_BUFFER_SIZE);
    packetBroadcast[packetSizeBroadcast] = '\0';
    char * dn;
    char * dnStatic;
    dn = strstr (packetBroadcast, DN);
    dnStatic = strstr (packetBroadcast, DNStatic);
    if (dn || dnStatic) {
      if (dnStatic) {
        for (uint8_t dnIdx = 0; dnIdx < packetSizeBroadcast; dnIdx++) {
          dname[dnIdx] = packetBroadcast[dnIdx + strlen(DNStatic)];
        }
      } else {
        for (uint8_t dnIdx = 0; dnIdx < packetSizeBroadcast; dnIdx++) {
          dname[dnIdx] = packetBroadcast[dnIdx + strlen(DN)];
        }
      }
      if (!remoteIpForUdp.toString().equals(broadcastUDP.remoteIP().toString())
        && ((strcmp(dname, deviceName.c_str()) == 0) || (strcmp(dname, microcontrollerIP.c_str()) == 0))) {
        remoteIpForUdp = broadcastUDP.remoteIP();
        Serial.println(F("-> Setting IP to use <-"));
        Serial.println(remoteIpForUdp.toString());
      }
    } else {
      char * p;
      p = strstr (packetBroadcast, PING);
      if (p) {
        for (uint8_t brIdx = 0; brIdx < packetSizeBroadcast; brIdx++) {
          broadCastAddress[brIdx] = packetBroadcast[brIdx + strlen(PING)];
        }
        if (!remoteIpForUdpBroadcast.toString().equals(broadCastAddress)) {
          remoteIpForUdpBroadcast.fromString(broadCastAddress);
          Serial.println(F("-> Setting Broadcast IP to use <-"));
          Serial.println(remoteIpForUdpBroadcast.toString());
        }
      }
    }
  }
}

/**
 * Get data from the stream and send to the strip
 * @param payload stream data
 */
void NetworkManager::fromUDPStreamToStrip(char (&payload)[UDP_MAX_BUFFER_SIZE]) {
  uint32_t myLeds;
  char delimiters[] = ",";
  char *ptr;
  char *saveptr;
  char *ptrAtoi;

  uint16_t index;
  ptr = strtok_r(payload, delimiters, &saveptr);
  // Discard packet if header does not match the correct one
  if (strcmp(ptr, "DPsoftware") != 0) {
    return;
  }
  lastUdpMsgReceived = millis();
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  uint16_t numLedFromLuciferin = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  uint8_t audioBrightness = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  if (brightness != audioBrightness && !ldrEnabled) {
    brightness = audioBrightness;
  }
  uint8_t chunkTot, chunkNum;
  chunkTot = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  chunkNum = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  index = UDP_CHUNK_SIZE * chunkNum;
  if (numLedFromLuciferin == 0) {
    effect = Effect::solid;
  } else {
    if (ledManager.dynamicLedNum != numLedFromLuciferin) {
      LedManager::setNumLed(numLedFromLuciferin);
      ledManager.initLeds();
    }
    while (ptr != nullptr) {
      myLeds = strtoul(ptr, &ptrAtoi, 10);
      if (ldrInterval != 0 && ldrEnabled && ldrReading && ldrTurnOff) {
        ledManager.setPixelColor(index, 0, 0, 0);
      } else {
        ledManager.setPixelColor(index, (myLeds >> 16 & 0xFF), (myLeds >> 8 & 0xFF), (myLeds >> 0 & 0xFF));
      }
      index++;
      ptr = strtok_r(nullptr, delimiters, &saveptr);
    }
  }
  if (effect != Effect::solid) {
    if (chunkNum == chunkTot - 1) {
      framerateCounter++;
      lastStream = millis();
      ledManager.ledShow();
    }
  }
}

#ifdef TARGET_GLOWWORMLUCIFERINFULL

/**
 * MANAGE WIFI AND MQTT DISCONNECTION
 */
void NetworkManager::manageDisconnections() {
  Serial.print(F("disconnection counter="));
  Serial.println(disconnectionCounter);
  if (disconnectionCounter < MAX_RECONNECT) {
    disconnectionCounter++;
  } else if (disconnectionCounter >= MAX_RECONNECT && disconnectionCounter < (MAX_RECONNECT * 2)) {
    disconnectionCounter++;
    ledManager.stateOn = true;
    effect = Effect::solid;
    String ap = bootstrapManager.readValueFromFile(AP_FILENAME, AP_PARAM);
    if ((ap.isEmpty() || ap == ERROR) || (!ap.isEmpty() && ap != ERROR && ap.toInt() != 10)) {
      DynamicJsonDocument asDoc(1024);
      asDoc[AP_PARAM] = 10;
      BootstrapManager::writeToLittleFS(asDoc, AP_FILENAME);
    }
    ledManager.setColorLoop(255, 0, 0);
  } else if (disconnectionCounter >= (MAX_RECONNECT * 2)) {
    ledManager.stateOn = true;
    effect = Effect::solid;
    String ap = bootstrapManager.readValueFromFile(AP_FILENAME, AP_PARAM);
    if ((ap.isEmpty() || ap == ERROR) || (!ap.isEmpty() && ap != ERROR && ap.toInt() != 0)) {
      DynamicJsonDocument asDoc(1024);
      asDoc[AP_PARAM] = 0;
      BootstrapManager::writeToLittleFS(asDoc, AP_FILENAME);
    }
    ledManager.setColor(0, 0, 0);
  }
}

/**
 * MQTT SUBSCRIPTIONS
 */
void NetworkManager::manageQueueSubscription() {
  // Note: Add another topic subscription can cause performance issues on ESP8266
  // Double check it with 60FPS, 100 LEDs, with MQTT enabled.
  BootstrapManager::subscribe(networkManager.lightSetTopic.c_str(), 1);
  BootstrapManager::subscribe(networkManager.cmndReboot.c_str());
  BootstrapManager::subscribe(networkManager.updateStateTopic.c_str());
  BootstrapManager::subscribe(networkManager.firmwareConfigTopic.c_str());
  BootstrapManager::subscribe(networkManager.effectToGw.c_str());
  // TODO remove subscription to topic that doesn't need MQTT, some topics can be managed via HTTP only
  BootstrapManager::subscribe(networkManager.streamTopic.c_str(), 0);
  BootstrapManager::subscribe(networkManager.unsubscribeTopic.c_str());
  apFileRead = false;
}

/**
 * Unsubscribe from the default MQTT topic
 */
void NetworkManager::swapTopicUnsubscribe() {
  // No firmwareConfigTopic unsubscribe because that topic needs MAC, no need to swap topic
  BootstrapManager::unsubscribe(networkManager.lightSetTopic.c_str());
  BootstrapManager::unsubscribe(networkManager.effectToGw.c_str());
  BootstrapManager::unsubscribe(networkManager.streamTopic.c_str());
  BootstrapManager::unsubscribe(networkManager.cmndReboot.c_str());
  BootstrapManager::unsubscribe(networkManager.updateStateTopic.c_str());
  BootstrapManager::unsubscribe(networkManager.unsubscribeTopic.c_str());
}

/**
 * Swap MQTT topi with the custom one
 * @param customtopic custom MQTT topic to use, received by Firefly Luciferin
 */
void NetworkManager::swapTopicReplace(const String &customtopic) {
  // No firmwareConfigTopic unsubscribe because that topic needs MAC, no need to swap topic
  networkManager.lightStateTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.effectToGw.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.effectToFw.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.updateStateTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.updateResultStateTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.lightSetTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.baseStreamTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.streamTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.unsubscribeTopic.replace(networkManager.BASE_TOPIC, customtopic);
  networkManager.cmndReboot.replace(networkManager.BASE_TOPIC, customtopic);
}

/**
 * Subscribe to custom MQTT topic
 */
void NetworkManager::swapTopicSubscribe() {
  // No firmwareConfigTopic unsubscribe because that topic needs MAC, no need to swap topic
  BootstrapManager::subscribe(networkManager.lightSetTopic.c_str(), 1);
  BootstrapManager::subscribe(networkManager.effectToGw.c_str());
  BootstrapManager::subscribe(networkManager.streamTopic.c_str(), 0);
  BootstrapManager::subscribe(networkManager.cmndReboot.c_str());
  BootstrapManager::subscribe(networkManager.updateStateTopic.c_str());
  BootstrapManager::subscribe(networkManager.unsubscribeTopic.c_str());
}

/**
 * List on HTTP GET
 */
void NetworkManager::listenOnHttpGet() {
  server.on(F("/"), []() {
      stopUDP();
      server.send(200, F("text/html"), settingsPage);
      startUDP();
  });
  server.on(F("/prefs"), [this]() {
      prefsData = F("{\"VERSION\":\"");
      prefsData += VERSION;
      prefsData += F("\",\"cp\":\"");
      prefsData += ledManager.red;
      prefsData += F(",");
      prefsData += ledManager.green;
      prefsData += F(",");
      prefsData += ledManager.blue;
      prefsData += F("\",\"toggle\":\"");
      prefsData += ledManager.stateOn;
      prefsData += F("\",\"effect\":\"");
      prefsData += Globals::effectToString(effect);
      prefsData += F("\",\"ffeffect\":\"");
      prefsData += ffeffect;
      prefsData += F("\",\"whiteTemp\":\"");
      prefsData += whiteTempInUse;
      prefsData += F("\",\"brightness\":\"");
      prefsData += brightness;
      prefsData += F("\",\"wifi\":\"");
      prefsData += BootstrapManager::getWifiQuality();
      prefsData += F("\",\"framerate\":\"");
      prefsData += framerate;
      prefsData += F("\",\"autosave\":\"");
      prefsData += autoSave;
      if (ldrEnabled) {
        prefsData += F("\",\"ldr\":\"");
        prefsData += ((ldrValue * 100) / ldrDivider);
      }
      prefsData += F("\"}");
      server.send(200, F("application/json"), prefsData);
  });
  server.on(F("/getLdr"), [this]() {
      prefsData = F("{\"ldrEnabled\":\"");
      prefsData += ldrEnabled;
      prefsData += F("\",\"ldrInterval\":\"");
      prefsData += ldrInterval;
      prefsData += F("\",\"ldrTurnOff\":\"");
      prefsData += ldrTurnOff;
      prefsData += F("\",\"ldrMin\":\"");
      prefsData += ldrMin;
      prefsData += F("\",\"ledOn\":\"");
      prefsData += ledOn;
      prefsData += F("\",\"relayPin\":\"");
      prefsData += relayPin;
      prefsData += F("\",\"sbPin\":\"");
      prefsData += sbPin;
      prefsData += F("\",\"ldrPin\":\"");
      prefsData += ldrPin;
      prefsData += F("\",\"ldrMax\":\"");
      if (ldrEnabled) {
        prefsData += ((ldrValue * 100) / ldrDivider);
      } else {
        prefsData += 0;
      }
      prefsData += F("\"}");
      server.send(200, F("application/json"), prefsData);
  });
  server.on(F("/setldr"), []() {
      stopUDP();
      server.send(200, F("text/html"), setLdrPage);
      startUDP();
  });
  server.on(F("/setAutoSave"), []() {
      stopUDP();
      autoSave = server.arg(F("autosave")).toInt();
      DynamicJsonDocument asDoc(1024);
      asDoc[F("autosave")] = autoSave;
      BootstrapManager::writeToLittleFS(asDoc, AUTO_SAVE_FILENAME);
      delay(20);
      server.send(200, F("text/html"), F("Stored"));
      startUDP();
  });
  server.on(F("/ldr"), []() {
      httpCallback(processLDR);
  });
  server.on(("/" + networkManager.lightSetTopic).c_str(), []() {
      httpCallback(nullptr);
      setLeds();
      setColor();
  });
  server.on(F("/set"), []() {
      httpCallback(nullptr);
      setLeds();
      setColor();
  });
  server.on(("/" + networkManager.cmndReboot).c_str(), []() {
      httpCallback(processGlowWormLuciferinRebootCmnd);
  });
  server.on(("/" + networkManager.updateStateTopic).c_str(), []() {
      httpCallback(processUpdate);
  });
  server.on(("/" + networkManager.unsubscribeTopic).c_str(), []() {
      httpCallback(processUnSubscribeStream);
  });
  server.on(("/" + networkManager.firmwareConfigTopic).c_str(), []() {
      httpCallback(processFirmwareConfig);
  });
  server.onNotFound([]() {
      server.send(404, F("text/plain"), ("Glow Worm Luciferin: Uri not found ") + server.uri());
  });
  manageAPSetting(false);

  server.begin();
}

/**
 * Manage AP Settings
 */
void NetworkManager::manageAPSetting(bool isSettingRoot) {
  if (isSettingRoot) {
    server.on(F("/"), []() {
        server.send(200, F("text/html"), setSettingsPageOffline);
    });
  } else {
    server.on(F("/setsettings"), []() {
        stopUDP();
        server.send(200, F("text/html"), setSettingsPage);
        startUDP();
    });
  }
  server.on(F("/getsettings"), [this]() {
      stopUDP();
      prefsData = F("{\"deviceName\":\"");
      prefsData += deviceName;
      prefsData += F("\",\"dhcp\":\"");
      prefsData += dhcpInUse;
      prefsData += F("\",\"ip\":\"");
      prefsData += microcontrollerIP;
      prefsData += F("\",\"dhcp\":\"");
      prefsData += dhcpInUse;
      prefsData += F("\",\"mqttuser\":\"");
      prefsData += mqttuser;
      prefsData += F("\",\"mqttIp\":\"");
      prefsData += mqttIP;
      prefsData += F("\",\"mqttpass\":\"");
      prefsData += mqttpass;
      prefsData += F("\",\"mqttPort\":\"");
      prefsData += mqttPort;
      prefsData += F("\",\"mqttTopic\":\"");
      prefsData += networkManager.topicInUse;
      prefsData += F("\",\"lednum\":\"");
      prefsData += ledManager.dynamicLedNum;
      prefsData += F("\",\"gpio\":\"");
      prefsData += gpioInUse;
      prefsData += F("\",\"gpioClock\":\"");
      prefsData += gpioClockInUse;
      prefsData += F("\",\"colorMode\":\"");
      prefsData += colorMode;
      prefsData += F("\",\"colorOrder\":\"");
      prefsData += colorOrder;
      prefsData += F("\",\"br\":\"");
      prefsData += baudRateInUse;
      prefsData += F("\",\"ssid\":\"");
      prefsData += qsid.c_str();
      prefsData += F("\"}");
      server.send(200, F("application/json"), prefsData);
      startUDP();
  });
  server.on(F("/setting"), []() {
      stopUDP();
      httpCallback(processFirmwareConfigWithReboot);
  });
}

/**
 * Set color
 */
void NetworkManager::setColor() {
  if (ledManager.stateOn) {
    LedManager::setColor(map(ledManager.red, 0, 255, 0, brightness), map(ledManager.green, 0, 255, 0, brightness),
                         map(ledManager.blue, 0, 255, 0, brightness));
  } else {
    LedManager::setColor(0, 0, 0);
  }
}

/**
 * Set LEDs state, used by HTTP and MQTT requests
 */
void NetworkManager::setLeds() {
  String requestedEffect = bootstrapManager.jsonDoc[F("effect")];
  ffeffect = bootstrapManager.jsonDoc[F("effect")].as<String>();
  if (requestedEffect == F("GlowWormWifi") || requestedEffect == F("GlowWormWifi")
      || requestedEffect.indexOf("Music") > -1 || requestedEffect.indexOf("Bias") > -1) {
    bootstrapManager.jsonDoc[F("effect")].set(F("GlowWormWifi"));
    requestedEffect = "GlowWormWifi";
  }
  processJson();
  if (mqttIP.length() > 0) {
    if (requestedEffect == F("GlowWormWifi") || requestedEffect == F("GlowWormWifi")) {
      BootstrapManager::publish(networkManager.effectToFw.c_str(), ffeffect.c_str(), false);
    } else {
      if (ledManager.stateOn) {
        BootstrapManager::publish(networkManager.effectToFw.c_str(), ffeffect.c_str(), false);
      } else {
        BootstrapManager::publish(networkManager.effectToFw.c_str(), OFF_CMD.c_str(), false);
      }
      framerate = framerateCounter = 0;
    }
  } else {
#if defined(ESP8266)
    if (networkManager.remoteIpForUdp.isSet()) {
#elif defined(ARDUINO_ARCH_ESP32)
    if (!networkManager.remoteIpForUdp.toString().equals(F("0.0.0.0"))) {
#endif
      networkManager.broadcastUDP.beginPacket(networkManager.remoteIpForUdp, UDP_BROADCAST_PORT);
      if (requestedEffect == F("GlowWormWifi") || requestedEffect == F("GlowWormWifi")) {
        networkManager.broadcastUDP.print(ffeffect.c_str());
      } else {
        networkManager.broadcastUDP.print(networkManager.STOP_FF);
        framerate = framerateCounter = 0;
      }
      networkManager.broadcastUDP.endPacket();
    }
  }
}

/**
 * Stop UDP broadcast while serving pages
 */
void NetworkManager::stopUDP() {
  networkManager.UDP.stop();
  networkManager.servingWebPages = true;
  delay(10);
}

/*
 * Start UDP broadcast while serving pages
 */
void NetworkManager::startUDP() {
  delay(10);
  networkManager.servingWebPages = false;
  networkManager.UDP.begin(UDP_PORT);
}

/**
 * MANAGE HARDWARE BUTTON
 */
void NetworkManager::manageHardwareButton() {
  // no hardware button at the moment
}

/**
 * START CALLBACK
 * @param topic MQTT topic
 * @param payload MQTT payload
 * @param length MQTT message length
 */
void NetworkManager::callback(char *topic, byte *payload, unsigned int length) {
  if (networkManager.streamTopic.equals(topic)) {
    if (effect == Effect::GlowWormWifi) {
      fromMqttStreamToStrip(reinterpret_cast<char *>(payload));
    }
  } else {
    bootstrapManager.jsonDoc.clear();
    bootstrapManager.parseQueueMsg(topic, payload, length);
    if (networkManager.cmndReboot.equals(topic)) {
      processGlowWormLuciferinRebootCmnd();
    } else if (networkManager.lightSetTopic.equals(topic)) {
      processJson();
    } else if (networkManager.effectToGw.equals(topic)) {
      setLeds();
    } else if (networkManager.updateStateTopic.equals(topic)) {
      processMqttUpdate();
    } else if (networkManager.firmwareConfigTopic.equals(topic)) {
      processFirmwareConfig();
    } else if (networkManager.unsubscribeTopic.equals(topic)) {
      processUnSubscribeStream();
    }
    if (ledManager.stateOn) {
      LedManager::setColor(map(ledManager.red, 0, 255, 0, brightness), map(ledManager.green, 0, 255, 0, brightness),
                           map(ledManager.blue, 0, 255, 0, brightness));
    } else {
      LedManager::setColor(0, 0, 0);
    }
  }
}

/**
 * Execute callback from HTTP payload
 * @param callback to execute using HTTP payload
 */
void NetworkManager::httpCallback(bool (*callback)()) {
  bootstrapManager.jsonDoc.clear();
  String payload = server.arg(F("payload"));
  bootstrapManager.parseHttpMsg(payload, payload.length());
  if (callback != nullptr) {
    callback();
    networkManager.setColor();
  }
  server.send(200, F("text/plain"), F("OK"));
}

/**
 * Get data from the stream and send to the strip
 * @param payload stream data
 */
void NetworkManager::fromMqttStreamToStrip(char *payload) {
  uint32_t myLeds;
  char delimiters[] = ",";
  char *ptr;
  char *saveptr;
  char *ptrAtoi;

  uint16_t index = 0;
  ptr = strtok_r(payload, delimiters, &saveptr);
  uint16_t numLedFromLuciferin = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  uint8_t audioBrightness = strtoul(ptr, &ptrAtoi, 10);
  ptr = strtok_r(nullptr, delimiters, &saveptr);
  if (brightness != audioBrightness && !ldrEnabled) {
    brightness = audioBrightness;
  }
  if (numLedFromLuciferin == 0) {
    effect = Effect::solid;
  } else {
    if (ledManager.dynamicLedNum != numLedFromLuciferin) {
      LedManager::setNumLed(numLedFromLuciferin);
      ledManager.initLeds();
    }
    while (ptr != nullptr) {
      myLeds = strtoul(ptr, &ptrAtoi, 10);
      if (ldrInterval != 0 && ldrEnabled && ldrReading && ldrTurnOff) {
        ledManager.setPixelColor(index, 0, 0, 0);
      } else {
        ledManager.setPixelColor(index, (myLeds >> 16 & 0xFF), (myLeds >> 8 & 0xFF), (myLeds >> 0 & 0xFF));
      }
      index++;
      ptr = strtok_r(nullptr, delimiters, &saveptr);
    }
  }
  if (effect != Effect::solid) {
    framerateCounter++;
    lastStream = millis();
    ledManager.ledShow();
  }
}

/**
 * Process Firmware Configuration sent from Firefly Luciferin, this config requires reboot
 * @param json StaticJsonDocument
 * @return true if message is correctly processed
 */
bool NetworkManager::processFirmwareConfigWithReboot() {
  String deviceName = bootstrapManager.jsonDoc[F("deviceName")];
  String microcontrollerIP = bootstrapManager.jsonDoc[F("microcontrollerIP")];
  String mqttCheckbox = bootstrapManager.jsonDoc[F("mqttCheckbox")];
  String setSsid = bootstrapManager.jsonDoc[F("ssid")];
  String wifipwd = bootstrapManager.jsonDoc[F("wifipwd")];
  String mqttIP = bootstrapManager.jsonDoc[F("mqttIP")];
  String mqttPort = bootstrapManager.jsonDoc[F("mqttPort")];
  String mqttTopic = bootstrapManager.jsonDoc[F("mqttTopic")];
  String mqttuser = bootstrapManager.jsonDoc[F("mqttuser")];
  String mqttpass = bootstrapManager.jsonDoc[F("mqttpass")];
  String additionalParam = bootstrapManager.jsonDoc[F("additionalParam")];
  String gpioClockParam = bootstrapManager.jsonDoc[F("gpioClock")];
  String colorModeParam = bootstrapManager.jsonDoc[F("colorMode")];
  String colorOrderParam = bootstrapManager.jsonDoc[F("colorOrder")];
  String lednum = bootstrapManager.jsonDoc[F("lednum")];
  String br = bootstrapManager.jsonDoc[F("br")];
  DynamicJsonDocument doc(1024);
  if (deviceName.length() > 0 &&
      ((mqttCheckbox == "false") || (mqttIP.length() > 0 && mqttPort.length() > 0 && mqttTopic.length() > 0))) {
    if (microcontrollerIP.length() == 0) {
      microcontrollerIP = "DHCP";
    }
    doc[F("deviceName")] = deviceName;
    doc[F("microcontrollerIP")] = microcontrollerIP;
    doc[F("qsid")] = (setSsid != NULL && !setSsid.isEmpty()) ? setSsid : qsid;
    doc[F("qpass")] = (wifipwd != NULL && !wifipwd.isEmpty()) ? wifipwd : qpass;
    doc[F("OTApass")] = OTApass;
    if (mqttCheckbox.equals("true")) {
      doc[F("mqttIP")] = mqttIP;
      doc[F("mqttPort")] = mqttPort;
      doc[F("mqttTopic")] = mqttTopic;
      doc[F("mqttuser")] = mqttuser;
      doc[F("mqttpass")] = mqttpass;
    } else {
      doc[F("mqttIP")] = "";
      doc[F("mqttPort")] = "";
      doc[F("mqttTopic")] = "";
      doc[F("mqttuser")] = "";
      doc[F("mqttpass")] = "";
    }
    doc[F("additionalParam")] = additionalParam;
    content = F("Success: rebooting the microcontroller using your credentials.");
    statusCode = 200;
  } else {
    content = F("Error: missing required fields.");
    statusCode = 404;
    Serial.println(F("Sending 404"));
  }
  delay(DELAY_500);
  server.sendHeader(F("Access-Control-Allow-Origin"), "*");
  server.send(statusCode, F("text/plain"), content);
  delay(DELAY_500);
  // Write to LittleFS
  Serial.println(F("Saving setup.json"));
  File jsonFile = LittleFS.open("/setup.json", FILE_WRITE);
  if (!jsonFile) {
    Serial.println(F("Failed to open [setup.json] file for writing"));
  } else {
    serializeJsonPretty(doc, Serial);
    serializeJson(doc, jsonFile);
    jsonFile.close();
    Serial.println(F("[setup.json] written correctly"));
  }
  delay(DELAY_200);
  Serial.println(F("Saving lednum"));
  LedManager::setNumLed(lednum.toInt());
  delay(DELAY_200);
  Serial.println(F("Saving gpio"));
  Globals::setGpio(additionalParam.toInt());
  if (!bootstrapManager.jsonDoc[F("gpioClock")].isNull()) {
    delay(DELAY_200);
    Serial.println(F("Saving gpio clock"));
    Globals::setGpioClock(gpioClockParam.toInt());
  }
  delay(DELAY_500);
  DynamicJsonDocument topicDoc(1024);
  topicDoc[networkManager.MQTT_PARAM] = mqttTopic;
  BootstrapManager::writeToLittleFS(topicDoc, networkManager.TOPIC_FILENAME);
  delay(DELAY_500);
  ledManager.setColorMode(colorModeParam.toInt());
  delay(DELAY_500);
  if (colorOrderParam != NULL && colorOrderParam.toInt() != 0) {
    ledManager.setColorOrder(colorOrderParam.toInt());
    delay(DELAY_500);
  }
  Globals::setBaudRateInUse(br.toInt());
  Globals::setBaudRate(baudRateInUse);
  delay(DELAY_1000);
#if defined(ARDUINO_ARCH_ESP32)
  ESP.restart();
#elif defined(ESP8266)
  EspClass::restart();
#endif
  return true;
}

/**
 * Process Firmware Configuration sent from Firefly Luciferin
 * @param json StaticJsonDocument
 * @return true if message is correctly processed
 */
bool NetworkManager::processFirmwareConfig() {
  boolean espRestart = false;
  if (bootstrapManager.jsonDoc.containsKey("MAC")) {
    String macToUpdate = bootstrapManager.jsonDoc["MAC"];
    Serial.println(macToUpdate);
    if (macToUpdate == MAC) {
      // GPIO
      if (bootstrapManager.jsonDoc.containsKey(GPIO_PARAM)) {
        int gpioFromConfig = (int) bootstrapManager.jsonDoc[GPIO_PARAM];
        if (gpioFromConfig != 0 && gpioInUse != gpioFromConfig) {
          Globals::setGpio(gpioFromConfig);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // GPIO CLOCK
      if (bootstrapManager.jsonDoc.containsKey(GPIO_CLOCK_PARAM)) {
        int gpioClockFromConfig = (int) bootstrapManager.jsonDoc[GPIO_CLOCK_PARAM];
        if (gpioClockFromConfig != 0 && gpioClockInUse != gpioClockFromConfig) {
          Globals::setGpioClock(gpioClockFromConfig);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // COLOR_MODE
      if (bootstrapManager.jsonDoc.containsKey(ledManager.COLOR_MODE_PARAM)) {
        int colorModeParam = (int) bootstrapManager.jsonDoc[ledManager.COLOR_MODE_PARAM];
        if (colorMode != colorModeParam) {
          colorMode = colorModeParam;
          ledManager.setColorMode(colorMode);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // COLOR_ORDER
      if (bootstrapManager.jsonDoc.containsKey(ledManager.COLOR_ORDER_PARAM)) {
        int colorOrderParam = (int) bootstrapManager.jsonDoc[ledManager.COLOR_ORDER_PARAM];
        if (colorOrder != colorOrderParam) {
          colorOrder = colorOrderParam;
          ledManager.setColorOrder(colorOrder);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // LDR_PIN_PARAM
      if (bootstrapManager.jsonDoc.containsKey(ledManager.LDR_PIN_PARAM)) {
        int ldrPinParam = (int) bootstrapManager.jsonDoc[ledManager.LDR_PIN_PARAM];
        if (ldrPin != ldrPinParam) {
          ldrPin = ldrPinParam;
          ledManager.setPins(relayPin, sbPin, ldrPin);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // RELAY_PIN_PARAM
      if (bootstrapManager.jsonDoc.containsKey(ledManager.RELAY_PIN_PARAM)) {
        int relayPinParam = (int) bootstrapManager.jsonDoc[ledManager.RELAY_PIN_PARAM];
        if (relayPin != relayPinParam) {
          relayPin = relayPinParam;
          ledManager.setPins(relayPin, sbPin, ldrPin);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // SB_PIN_PARAM
      if (bootstrapManager.jsonDoc.containsKey(ledManager.SB_PIN_PARAM)) {
        int sbrPinParam = (int) bootstrapManager.jsonDoc[ledManager.SB_PIN_PARAM];
        if (sbPin != sbrPinParam) {
          sbPin = sbrPinParam;
          ledManager.setPins(relayPin, sbPin, ldrPin);
          ledManager.reinitLEDTriggered = true;
        }
      }
      // Restart if needed
      if (ledManager.reinitLEDTriggered) {
        ledManager.reinitLEDTriggered = false;
        ledManager.initLeds();
      }
      if (espRestart) {
#if defined(ARDUINO_ARCH_ESP32)
        ESP.restart();
#elif defined(ESP8266)
        EspClass::restart();
#endif
      }
    }
  }
  return true;
}

/**
 * Unsubscribe from the stream topic, we will use a specific topic for this instance
 * @param json StaticJsonDocument
 * @return true if message is correctly processed
 */
bool NetworkManager::processUnSubscribeStream() {
  if (bootstrapManager.jsonDoc.containsKey("instance")) {
    String instance = bootstrapManager.jsonDoc["instance"];
    String manager = bootstrapManager.jsonDoc["manager"];
    if (manager.equals(deviceName)) {
      BootstrapManager::unsubscribe(networkManager.streamTopic.c_str());
      networkManager.streamTopic = networkManager.baseStreamTopic + instance;
      effect = Effect::GlowWormWifi;
      Globals::turnOnRelay();
      ledManager.stateOn = true;
      BootstrapManager::subscribe(networkManager.streamTopic.c_str(), 0);
    }
  }
  return true;
}

/**
 * Process JSON message
 * @param json StaticJsonDocument
 * @return true if message is correctly processed
 */
bool NetworkManager::processJson() {
  ledManager.lastLedUpdate = millis();
  boolean skipMacCheck = ((bootstrapManager.jsonDoc.containsKey("MAC") && bootstrapManager.jsonDoc["MAC"] == MAC)
                          || bootstrapManager.jsonDoc.containsKey("allInstances"));
  if (!bootstrapManager.jsonDoc.containsKey("MAC") || skipMacCheck) {
    if (bootstrapManager.jsonDoc.containsKey("state")) {
      String state = bootstrapManager.jsonDoc["state"];
      if (state == ON_CMD) {
        Globals::turnOnRelay();
        ledManager.stateOn = true;
      } else if (state == OFF_CMD) {
        ledManager.stateOn = false;
      }
    }
    if (bootstrapManager.jsonDoc.containsKey("color")) {
      ledManager.red = bootstrapManager.jsonDoc["color"]["r"];
      ledManager.green = bootstrapManager.jsonDoc["color"]["g"];
      ledManager.blue = bootstrapManager.jsonDoc["color"]["b"];
    }
    if (bootstrapManager.jsonDoc.containsKey("brightness")) {
      brightness = bootstrapManager.jsonDoc["brightness"];
    }
    if (autoSave && (ledManager.red != rStored || ledManager.green != gStored || ledManager.blue != bStored ||
                     brightness != brightnessStored)) {
      Globals::saveColorBrightnessInfo(ledManager.red, ledManager.green, ledManager.blue, brightness);
    }
    if (skipMacCheck) {
      if (bootstrapManager.jsonDoc.containsKey("whitetemp")) {
        uint8_t wt = bootstrapManager.jsonDoc["whitetemp"];
        if (wt != 0 && whiteTempInUse != wt) {
          LedManager::setWhiteTemp(wt);
        }
      }
    }
    if (bootstrapManager.jsonDoc.containsKey("ffeffect")) {
      ffeffect = bootstrapManager.jsonDoc["ffeffect"].as<String>();
    }
    if (bootstrapManager.jsonDoc.containsKey("effect")) {
      boolean effectIsDifferent = (effect != Effect::GlowWorm && effect != Effect::GlowWormWifi);
      JsonVariant requestedEffect = bootstrapManager.jsonDoc["effect"];
      if (requestedEffect == "Bpm") effect = Effect::bpm;
      else if (requestedEffect == "Fire") effect = Effect::fire;
      else if (requestedEffect == "Twinkle") effect = Effect::twinkle;
      else if (requestedEffect == "Rainbow") effect = Effect::rainbow;
      else if (requestedEffect == "Chase rainbow") effect = Effect::chase_rainbow;
      else if (requestedEffect == "Solid rainbow") effect = Effect::solid_rainbow;
      else if (requestedEffect == "Mixed rainbow") effect = Effect::mixed_rainbow;
      else {
        effect = Effect::solid;
        breakLoop = true;
      }
      if (skipMacCheck) {
        if (requestedEffect == "GlowWorm") {
          if (effectIsDifferent) {
            previousMillisLDR = 0;
          }
          effect = Effect::GlowWorm;
          ledManager.lastLedUpdate = millis();
        } else if (requestedEffect == "GlowWormWifi") {
          if (effectIsDifferent) {
            previousMillisLDR = 0;
          }
          effect = Effect::GlowWormWifi;
          lastStream = millis();
        }
      }
    }
  }
  return true;
}

/**
 * Send microcontroller state
 * For debug: ESP.getFreeHeap() ESP.getHeapFragmentation() ESP.getMaxFreeBlockSize()
 */
void NetworkManager::sendStatus() {
  // Skip JSON framework for lighter processing during the stream
  if (effect == Effect::GlowWorm || effect == Effect::GlowWormWifi) {
    fpsData = F("{\"deviceName\":\"");
    fpsData += deviceName;
    fpsData += F("\",\"state\":\"");
    fpsData += (ledManager.stateOn) ? ON_CMD : OFF_CMD;
    fpsData += F("\",\"brightness\":");
    fpsData += brightness;
    fpsData += F(",\"MAC\":\"");
    fpsData += MAC;
    fpsData += F("\",\"lednum\":\"");
    fpsData += ledManager.dynamicLedNum;
    fpsData += F("\",\"framerate\":\"");
    fpsData += framerate > framerateSerial ? framerate : framerateSerial;
    fpsData += F("\",\"wifi\":\"");
    fpsData += BootstrapManager::getWifiQuality();
    if (ldrEnabled) {
      fpsData += F("\",\"ldr\":\"");
      fpsData += ((ldrValue * 100) / ldrDivider);
    }
    fpsData += F("\"}");
    if (mqttIP.length() > 0) {
      BootstrapManager::publish(networkManager.lightStateTopic.c_str(), fpsData.c_str(), false);
    } else {
#if defined(ESP8266)
      if (networkManager.remoteIpForUdp.isSet()) {
#elif defined(ARDUINO_ARCH_ESP32)
      if (!networkManager.remoteIpForUdp.toString().equals(F("0.0.0.0"))) {
#endif
        networkManager.broadcastUDP.beginPacket(networkManager.remoteIpForUdp, UDP_BROADCAST_PORT);
        networkManager.broadcastUDP.print(fpsData.c_str());
        networkManager.broadcastUDP.endPacket();
      }
    }
  } else {
    bootstrapManager.jsonDoc.clear();
    JsonObject root = bootstrapManager.jsonDoc.to<JsonObject>();
    JsonObject color = root.createNestedObject(F("color"));
    root[F("state")] = (ledManager.stateOn) ? ON_CMD : OFF_CMD;
    color[F("r")] = ledManager.red;
    color[F("g")] = ledManager.green;
    color[F("b")] = ledManager.blue;
    color[F("colorMode")] = colorMode;
    color[F("colorOrder")] = colorOrder;
    root[F("brightness")] = brightness;
    root[F("effect")] = Globals::effectToString(effect);
    root[F("deviceName")] = deviceName;
    root[F("IP")] = microcontrollerIP;
    root[F("dhcp")] = dhcpInUse;
    root[F("wifi")] = BootstrapManager::getWifiQuality();
    root[F("MAC")] = MAC;
    root[F("ver")] = VERSION;
    root[F("framerate")] = framerate > framerateSerial ? framerate : framerateSerial;
    if (ldrEnabled) {
      root[F("ldr")] = ((ldrValue * 100) / ldrDivider);
    }
    root[F("relayPin")] = relayPin;
    root[F("sbPin")] = sbPin;
    root[F("ldrPin")] = ldrPin;
    root[BAUDRATE_PARAM] = baudRateInUse;
#if defined(ESP8266)
    root[F("board")] = F("ESP8266");
#endif
#if CONFIG_IDF_TARGET_ESP32C3
    root["board"] = "ESP32_C3_CDC";
#elif CONFIG_IDF_TARGET_ESP32S2
    root["board"] = "ESP32_S2";
#elif CONFIG_IDF_TARGET_ESP32S3
#if ARDUINO_USB_MODE==1
    root["board"] = "ESP32_S3_CDC";
#else
    root["board"] = "ESP32_S3";
#endif
#elif CONFIG_IDF_TARGET_ESP32
    root["board"] = "ESP32";
#endif
    root[LED_NUM_PARAM] = String(ledManager.dynamicLedNum);
    root[F("gpio")] = gpioInUse;
    root[F("gpioClock")] = gpioClockInUse;
    root[F("mqttopic")] = networkManager.topicInUse;
    root[F("whitetemp")] = whiteTempInUse;
    if (effect == Effect::solid && !ledManager.stateOn) {
      LedManager::setColor(0, 0, 0);
    }

    // This topic should be retained, we don't want unknown values on battery voltage or wifi signal
    if (mqttIP.length() > 0) {
      BootstrapManager::publish(networkManager.lightStateTopic.c_str(), root, true);
    } else {
      String output;
      serializeJson(root, output);
#if defined(ESP8266)
      if (networkManager.remoteIpForUdpBroadcast.isSet()) {
#elif defined(ARDUINO_ARCH_ESP32)
      if (!networkManager.remoteIpForUdpBroadcast.toString().equals(F("0.0.0.0"))) {
#endif
        networkManager.broadcastUDP.beginPacket(networkManager.remoteIpForUdpBroadcast, UDP_BROADCAST_PORT);
        networkManager.broadcastUDP.print(output.c_str());
        networkManager.broadcastUDP.endPacket();
      }
    }
  }
  // Built in led triggered
  ledTriggered = true;
}

/**
* Handle web server for the upgrade process
* @return true if message is correctly processed
*/
bool NetworkManager::processMqttUpdate() {
  if (bootstrapManager.jsonDoc.containsKey(F("update"))) {
    return processUpdate();
  }
  return true;
}

/**
 * Handle web server for the upgrade process
 * @return true if message is correctly processed
 */
bool NetworkManager::processUpdate() {
  Serial.println(F("Starting web server"));
  server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      bool error = Update.hasError();
      server.send(200, "text/plain", error ? "KO" : "OK");
      if (!error) {
        if (mqttIP.length() > 0) {
          BootstrapManager::publish(networkManager.updateResultStateTopic.c_str(), deviceName.c_str(), false);
        } else {
#if defined(ESP8266)
          if (networkManager.remoteIpForUdp.isSet()) {
#elif defined(ARDUINO_ARCH_ESP32)
          if (!networkManager.remoteIpForUdp.toString().equals(F("0.0.0.0"))) {
#endif
            networkManager.broadcastUDP.beginPacket(networkManager.remoteIpForUdp, UDP_BROADCAST_PORT);
            networkManager.broadcastUDP.print(deviceName.c_str());
            networkManager.broadcastUDP.endPacket();
          }
        }
      }
      delay(DELAY_500);
#if defined(ARDUINO_ARCH_ESP32)
      ESP.restart();
#elif defined(ESP8266)
      EspClass::restart();
#endif
  }, []() {
      HTTPUpload &upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
#if defined(ARDUINO_ARCH_ESP32)
        updateSize = UPDATE_SIZE_UNKNOWN;
#elif defined(ESP8266)
        updateSize = 480000;
#endif
        if (!Update.begin(updateSize)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        /* flashing firmware to ESP*/
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
  });
  server.begin();
  Serial.println(F("Web server started"));
  firmwareUpgrade = true;

  return true;
}

/**
 * Reboot the microcontroller
 * @param json StaticJsonDocument
 * @return true if message is correctly processed
 */
bool NetworkManager::processGlowWormLuciferinRebootCmnd() {
  if (bootstrapManager.jsonDoc[VALUE] == OFF_CMD) {
    ledManager.stateOn = false;
    sendStatus();
    delay(1500);
#if defined(ARDUINO_ARCH_ESP32)
    ESP.restart();
#elif defined(ESP8266)
    EspClass::restart();
#endif
  }
  return true;
}

/**
 * Process LDR settings
 * @return true if message is correctly processed
 */
bool NetworkManager::processLDR() {
  if (bootstrapManager.jsonDoc.containsKey(F("ldrEnabled"))) {
    stopUDP();
    String ldrEnabledMqtt = bootstrapManager.jsonDoc[F("ldrEnabled")];
    String ldrTurnOffMqtt = bootstrapManager.jsonDoc[F("ldrTurnOff")];
    String ldrIntervalMqtt = bootstrapManager.jsonDoc[F("ldrInterval")];
    String ldrMinMqtt = bootstrapManager.jsonDoc[F("ldrMin")];
    String ldrActionMqtt = bootstrapManager.jsonDoc[F("ldrAction")];
    String ledOnMqtt = bootstrapManager.jsonDoc[F("ledOn")];
    String rPin = bootstrapManager.jsonDoc[F("relayPin")];
    String sPin = bootstrapManager.jsonDoc[F("sbPin")];
    String lPin = bootstrapManager.jsonDoc[F("ldrPin")];
    ldrEnabled = ldrEnabledMqtt == "true";
    ldrTurnOff = ldrTurnOffMqtt == "true";
    ledOn = ledOnMqtt == "true";
    ldrInterval = ldrIntervalMqtt.toInt();
    ldrMin = ldrMinMqtt.toInt();
    if (ldrActionMqtt.toInt() == 2) {
      ldrDivider = ldrValue;
      ledManager.setLdr(ldrDivider);
      delay(DELAY_500);
    } else if (ldrActionMqtt.toInt() == 3) {
      ldrDivider = LDR_DIVIDER;
      ledManager.setLdr(-1);
      delay(DELAY_500);
    }
    ledManager.setLdr(ldrEnabledMqtt == "true", ldrTurnOffMqtt == "true",
                      ldrInterval, ldrMinMqtt.toInt(), ledOnMqtt == "true");
    delay(DELAY_500);
    content = F("Success: rebooting the microcontroller using your credentials.");
    statusCode = 200;
    server.sendHeader(F("Access-Control-Allow-Origin"), "*");
    server.send(statusCode, F("text/plain"), content);
    delay(DELAY_500);
    if (!bootstrapManager.jsonDoc[F("relayPin")].isNull() && !bootstrapManager.jsonDoc[F("sbPin")].isNull() && !bootstrapManager.jsonDoc[F("ldrPin")].isNull()) {
      relayPin = rPin.toInt();
      sbPin = sPin.toInt();
      ldrPin = lPin.toInt();
      ledManager.setPins(relayPin, sbPin, ldrPin);
    }
    delay(DELAY_500);
    startUDP();
  }
  return true;
}


/**
 * Execute the MQTT topic swap
 * @param customtopic new topic to use
 */
void NetworkManager::executeMqttSwap(const String &customtopic) {
  Serial.println("Swapping topic=" + customtopic);
  networkManager.topicInUse = customtopic;
  swapTopicUnsubscribe();
  swapTopicReplace(customtopic);
  swapTopicSubscribe();
}

#endif

/**
 * Check connection and turn off the LED strip if no input received
 */
void NetworkManager::checkConnection() {
#ifdef TARGET_GLOWWORMLUCIFERINFULL
  // Bootsrap loop() with Wifi, MQTT and OTA functions
  bootstrapManager.bootstrapLoop(manageDisconnections, manageQueueSubscription, manageHardwareButton);
  server.handleClient();
  currentMillisCheckConn = millis();
  if (currentMillisCheckConn - prevMillisCheckConn1 > 10000) {
    prevMillisCheckConn1 = currentMillisCheckConn;
    // No updates since 7 seconds, turn off LEDs
    if ((!breakLoop && (effect == Effect::GlowWorm) && (currentMillisCheckConn > ledManager.lastLedUpdate + 10000)) ||
        (!breakLoop && (effect == Effect::GlowWormWifi) && (currentMillisCheckConn > lastStream + 10000))) {
      breakLoop = true;
      effect = Effect::solid;
      ledManager.stateOn = false;
      Globals::turnOffRelay();
    }
    framerate = framerateCounter > 0 ? framerateCounter / 10 : 0;
    framerateCounter = 0;
    NetworkManager::sendStatus();
  }
#elif  TARGET_GLOWWORMLUCIFERINLIGHT
  if (currentMillisCheckConn - prevMillisCheckConn2 > 15000) {
    prevMillisCheckConn2 = currentMillisCheckConn;
    // No updates since 15 seconds, turn off LEDs
    if(currentMillisCheckConn > ledManager.lastLedUpdate + 10000){
      ledManager.setColor(0, 0, 0);
      globals.turnOffRelay();
    }
  }
#endif
  Globals::sendSerialInfo();
}