/* Init Mode --- */

const char *STA_WIFI_KEY = "hellohello";
const float leaveInitTimeout = 30;
const char *staDet = "stateDetails";

struct InitStatePhase_t
{
  char step[16];
  char desc[31];
} _currentPhase;

/* --- Init Mode */

// 2 member: `state` and `stateDetails`.
const size_t wsCalcResponseBaseSize(size_t dataMemberCount = 2);
JsonDocument wsCreateResponse(const size_t, const char *, bool details = true);

bool startInitMode()
{
  _initState = InitState_t::idle;
  boolean result = softAPInit();
  if (result)
  {
    wsInit();
    setPhase("IoT Node", "idle");
  }
  return result;
}

bool softAPInit()
{
  // ToDo if websocket do not receive any connections within... 1-2 minutes, then go back tu
  // Wifi reconnection loop. Maybe in void loop() ?
  Serial.printf("\nSetting up soft-AP fo IoT initialization ... \n");
  WiFi.mode(WIFI_AP);
  boolean result = WiFi.softAP(wifiHostname, STA_WIFI_KEY, 9, false, 1);
  for (size_t i = 0; !result || i < 3; i++)
  {
    delay(500);
    result = WiFi.softAP(wifiHostname, STA_WIFI_KEY, 9, false, 1);
  }
  if (result)
  {
    Serial.printf("SoftAP SSID: %s\n", wifiHostname);
    Serial.println("Ready");
  }
  else
  {
    Serial.printf("SoftAP startup failed! Status code: %d.\n", WiFi.status());
  }
  return result;
}

void wsInit()
{
  Serial.println("Setting up WebSocket server for IoT device initialization ... ");
  webSocket.onEvent(webSocketEvent);
  webSocket.begin();
}

bool endInitMode()
{
  clearPhase();
  webSocket.close();
  // TODO replace following with call to wifiStationConnect() ?
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{
  switch (type)
  {
  case WStype_DISCONNECTED: // if the websocket is disconnected
  {
    if (_initState == InitState_t::succeed)
    {
      Serial.printf(" - - In %fs leaving Initialization Mode.\n", leaveInitTimeout);
      initMode_ticker.once(leaveInitTimeout, leaveIotInitMode);
    }
    break;
  }
  case WStype_CONNECTED: // if a new websocket connection is established
  {
    if (initMode_ticker.active())
    {
      Serial.println(" - - Cancel timer of \"Leaving Initialization Mode\".");
      initMode_ticker.detach();
    }
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    break;
  }
  case WStype_TEXT: // if new text data is received
  {
    wsTXTMessageHandler(num, (char *)payload, lenght);
    break;
  }
  }
}

void wsTXTMessageHandler(uint8_t num, char *payload, size_t lenght)
{
  const size_t capacity = 2 * JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + 40;
  StaticJsonDocument<capacity> payloadDoc;
  deserializeJson(payloadDoc, payload, lenght);
  const char *subject = payloadDoc[0];
  if (strcmp(subject, "get-initState") == 0)
  {
    wsGetInitState(num, "get-initState-R", _initState == InitState_t::idle);
  }
  else if (strcmp(subject, "set-initValues") == 0)
  {
    wsSetInitValues(num, "set-initValues-R", payloadDoc[1]);
  }
  else if (strcmp(subject, "get-currentConfig") == 0)
  {
    wsGetInitState(num, "get-currentConfig-R", true);
  }
}

void wsGetInitState(uint8_t num, const char *responseSubject, bool includeCurrentConfig)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsGetInitStateJsonCapacity(includeCurrentConfig, detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  // TODO Either add some meaningful or nothing as a Status Detail.
  wsAddStateDetails(responseDoc);
  if (includeCurrentConfig)
  {
    wsAddConfigParams(responseDoc);
  }
  wsSendTxtJsonResponse(num, responseDoc);
}

void wsAddConfigParams(JsonDocument &doc)
{
  JsonObject data = doc[1];
  data["iotDeviceId"] = (const char *)iotNodeId;
  data["ssid"] = WiFi.SSID();
  data["psk"] = WiFi.psk();
  data["iotType"] = IOT_TYPE;
  JsonArray outputs = data.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    outputs.add((const char *)device.usage);
  }
}

void wsSetInitValues(uint8_t num, const char *responseSubject, JsonObject payloadObj)
{
  _initState = InitState_t::working;
  changeOutputStates();
  // if (!wifiStationInit(payloadObj["ssid"], payloadObj["psk"]))
  const char *ssid = payloadObj["ssid"];
  const char *psk = payloadObj["psk"];
  wl_status_t wifiResult = WiFi.begin(ssid, psk);
  for (size_t i = 0; i < 10; i++)
  {
    if (wifiResult == WL_CONNECTED)
    {
      break;
    }
    /* if/else clauses could check other statuses as well, but so far only dis/connect is returned :-( */
    wsSetInitValuesHandleWifiMessaging(num, responseSubject, wifiResult);
    delay(1000);
    wifiResult = WiFi.status();
  }
  if (wifiResult != WL_CONNECTED)
  {
    _initState = InitState_t::failed;
  }
  wsSetInitValuesHandleWifiMessaging(num, responseSubject, wifiResult);
  if (wifiResult != WL_CONNECTED)
  {
    WiFi.setAutoReconnect(false);
    return;
  }
  //  === WiFi == !

  //  === MQTT == !
  // ! === SUCCESS->END ===
  //TODO this line cab be only called after async response from MQTT API
  // _initState = stageSucceed ? InitState_t::succeed : InitState_t::failed;
  _initState = InitState_t::succeed;
  // TODO store ID' from API/MQTT as well.
  wsStoreConfigToEEPROM(payloadObj["outputs"]);
  wsSendState(num, responseSubject);
  return;
  // === SUCCESS->END === !
  // ! === MQTT ===
  int mqttState = mqttIoTInit();
  setPhase("mqtt", mqttHelpGetStateTxt(mqttState));
  wsSendStateDetails(num, responseSubject);
  if (!mqttState)
  {
    _initState = InitState_t::failed;
    return;
  }
  setPhase("mqtt", mqttHelpGetStateTxt(mqttState));
  wsSendStateDetails(num, responseSubject);

}

void wsSetInitValuesHandleWifiMessaging(uint8_t num, const char *responseSubject, wl_status_t result)
{
  char wifiResultStr[sizeof(result) + 1];
  itoa(result, wifiResultStr, 10);
  setPhase("wifi", wifiResultStr);
  Serial.printf(" - - - wifiResultStr: %s \n", wifiResultStr);
  wsSendStateDetails(num, responseSubject);
}

void wsStoreConfigToEEPROM(JsonArray outputValues)
{
  size_t lenUsage = sizeof(OutputDevice_t::usage);
  for (size_t i = 0; i < lenOutputs; i++)
  {
    OutputDevice_t &device = outDevices[i];
    const char *val = outputValues[i] | "";
    size_t lenVal = strlen(val);
    memset(device.usage, '\0', lenUsage);
    if (lenVal > 0)
    {
      strncpy(device.usage, val, lenVal < lenUsage ? lenVal : lenUsage);
      /* Ensure 0-terminated */
      device.usage[lenUsage - 1] = '\0';
    }
    EEPROM.put(device.addressUsage, device.usage);
    device.id = i + 1; // TODO mock value!!
    EEPROM.put(device.addressId, device.id);
  }
  EEPROM.put(_AddressIoTState, _iotState = IOTState_t::initialized);
  EEPROM.commit();
}

bool wsSendState(uint8_t num, const char *responseSubject)
{
  /* Size w/ only `state` member. */
  const size_t capacity = wsCalcResponseBaseSize(1);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject, false);
  return wsSendTxtJsonResponse(num, responseDoc);
}

bool wsSendStateDetails(uint8_t num, const char *responseSubject)
{
  return wsSendStateDetails(num, responseSubject, getPhaseStep(), getPhaseDesc());
}

bool wsSendStateDetails(uint8_t num, const char *responseSubject, const char *step, const char *descr)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsCalcResponseBaseSize() + wsCalcStateDetailsSize(detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  wsAddStateDetails(responseDoc, step, descr);
  return wsSendTxtJsonResponse(num, responseDoc);
}

JsonDocument wsCreateResponse(const size_t docSize, const char *responseSubject, bool details)
{
  DynamicJsonDocument responseDoc(docSize);
  responseDoc.add(responseSubject);
  JsonObject data = responseDoc.createNestedObject();
  data["state"] = (byte)_initState; // otherwise inferes wrong type (boolean)
  if (details)
  {
    data.createNestedArray(staDet);
  }
  return responseDoc;
}

void wsAddStateDetails(JsonDocument &doc)
{
  wsAddStateDetails(doc, getPhaseStep(), getPhaseDesc());
}

void wsAddStateDetails(JsonDocument &doc, const char *step, const char *descr)
{
  if (strlen(step))
  {
    doc[1][staDet].createNestedObject()[step] = descr;
  }
}

bool wsSendTxtJsonResponse(uint8_t num, JsonDocument &doc)
{
  const size_t size = measureJson(doc) + 1; // make room for \0 as well.
  char buffer[size];
  serializeJson(doc, buffer, size);
  return webSocket.sendTXT(num, buffer, size - 1); // cut off \0 from data to be sent.
}

/**
 * Base response size, with WS subject part. Corresponds to:
 * ["request-R",{"state":"","stateDetails":[]]}]
 */
const size_t wsCalcResponseBaseSize(size_t dataMemberCount)
{
  return JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(dataMemberCount);
}

/**
 * Response's data object `stateDetails` array member's dynamic size. Corresponds to:
 * [{"name":"value"},{"name":"value"}]
 */
const size_t wsCalcStateDetailsSize(size_t detailsCount)
{
  return JSON_ARRAY_SIZE(detailsCount) + detailsCount * JSON_OBJECT_SIZE(1);
}

/**
 * Calculates necessary JSON doc size for serialization.
 * NB! Assumes all string members as `const char*`; SSID and PSK capacities are added by their max lenghts.
 * If any of string members are other type then add thos lenght+1 to this result.
 * Source: https://arduinojson.org/v6/faq/why-is-the-output-incomplete/
 */
const size_t wsGetInitStateJsonCapacity(bool includeCurrentConfig, int detailsCount)
{
  /* Data part has `state` and `stateDetails` by default;
   * but dynamically count in Config members;
   * dynamically add Details size. */
  size_t result = wsCalcResponseBaseSize(includeCurrentConfig ? 7 : 2) + wsCalcStateDetailsSize(detailsCount);
  /* Add stateDetails:[] and its members. */
  if (includeCurrentConfig)
  { /* Dynamically add IoT Config outputs:[] */
    result += JSON_ARRAY_SIZE(lenOutputs);
    /* PSK+1 + SSID+1 */
    result += 65 + 33;
  }
  return result;
}

void setPhase(const char *step, const char *desc)
{
  strncpy(_currentPhase.step, step, sizeof(_currentPhase.step) - 1);
  strncpy(_currentPhase.desc, desc, sizeof(_currentPhase.desc) - 1);
}

void clearPhase()
{
  memset(_currentPhase.step, '\0', sizeof(_currentPhase.step));
  memset(_currentPhase.desc, '\0', sizeof(_currentPhase.desc));
}

const char *getPhaseStep()
{
  return _currentPhase.step;
}

const char *getPhaseDesc()
{
  return _currentPhase.desc;
}
