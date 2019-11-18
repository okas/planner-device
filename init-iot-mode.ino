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
  webSocket.onEvent(webSocketEventHandler);
  webSocket.begin();
}

bool endInitMode()
{
  webSocket.close();
  clearTempOutputs();
  clearPhase();
  // TODO replace following with call to wifiStationConnect() ?
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
}

void webSocketEventHandler(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
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
  DynamicJsonDocument payloadDoc(wsCalcIncomingJsonSize(lenght));
  deserializeJson(payloadDoc, payload, lenght);
  const char *subject = payloadDoc[0];
  if (strcmp(subject, "get-initState") == 0)
  {
    JsonDocument responseDocument = wsGetInitStateDoc("get-initState-R", _initState == InitState_t::idle);
    wsSendTxtJsonResponse(num, responseDocument);
  }
  else if (strcmp(subject, "set-initValues") == 0)
  {
    wsSetInitValues("set-initValues-R", payloadDoc[1]);
  }
  else if (strcmp(subject, "get-currentConfig") == 0)
  {
    JsonDocument responseDocument = wsGetInitStateDoc("get-currentConfig-R", true);
    wsSendTxtJsonResponse(num, responseDocument);
  }
}

const size_t wsCalcIncomingJsonSize(size_t dataLength)
{
  const size_t baseSize = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(lenOutputs);
  return wsCalcDeserializeSizeBaseOrDouble(dataLength, baseSize);
}

JsonDocument wsGetInitStateDoc(const char *responseSubject, bool includeCurrentConfig)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsGetInitStateJsonCapacity(includeCurrentConfig, detailsCount);
  JsonDocument doc = wsCreateResponse(capacity, responseSubject);
  // TODO Either add some meaningful or nothing as a Status Detail.
  wsAddStateDetails(doc);
  if (includeCurrentConfig)
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
  return doc;
}

void wsSetInitValues(const char *responseSubject, JsonObject payloadObj)
{
  _initState = InitState_t::working;
  // TODO analyze if it needs to be here, maybe it is too eraly.
  // changeOutputStates();
  // if (!wifiStationInit(payloadObj["ssid"], payloadObj["psk"]))
  wsStoreOutputsToRAM(payloadObj["outputs"]);
  // ! === WiFi ===
  const char *ssid = payloadObj["ssid"];
  const char *psk = payloadObj["psk"];
  wl_status_t wifiResult;
  if (WiFi.SSID() == ssid && WiFi.status() == WL_CONNECTED)
  {
    wsSetInitValuesHandleWifiMessaging(responseSubject, wifiResult);
  }
  else
  {
    wifiResult = WiFi.begin(ssid, psk);
    for (size_t i = 0; i < 10; i++)
    {
      if (i)
      {
        delay(1000);
        wifiResult = WiFi.status();
      }
      wsSetInitValuesHandleWifiMessaging(responseSubject, wifiResult);
      if (wifiResult == WL_CONNECTED || wifiResult == WL_NO_SSID_AVAIL || wifiResult == WL_CONNECT_FAILED)
      {
        break;
      }
    }
    if (wifiResult != WL_CONNECTED)
    {
      _initState = InitState_t::failed;
      WiFi.setAutoReconnect(false);
      return;
    }
  }
  //  === WiFi == !
  // ! === MQTT ===
  int8_t mqttState = mqttIoTInit();
  wsSetInitValuesHandleMQTTMessaging(responseSubject, (lwmqtt_return_code_t)mqttState);
  if (mqttState != 0)
  {
    _initState = InitState_t::failed;
    return;
  }
  bool pubResult = mqttPublishIoTInit(mqttState);
  if (!pubResult)
  {
    _initState = InitState_t::failed;
    if (mqttState != 0)
    {
      wsSetInitValuesHandleMQTTMessaging(responseSubject, (lwmqtt_err_t)mqttState);
    }
    wsSetInitValuesHandleMQTTMessaging(responseSubject, "__PUBLISH_FAILED");
    return;
  }
  //  === MQTT == !
  // ! === IoTNode ===
  wsSetInitValuesHandleGenericMessaging(responseSubject, "iotnode", "INIT_WAITING_IDS_FROM_API");
}

void wsStoreOutputsToRAM(JsonArray values)
{
  size_t lenUsage = sizeof(OutputDevice_t::usage);
  for (size_t i = 0; i < lenOutputs; i++)
  {
    OutputDevice_t &device = outDevices[i];
    const char *val = values[i] | "";
    size_t lenVal = strlen(val);
    memset(device.usage, '\0', lenUsage);
    if (lenVal > 0)
    {
      strncpy(device.usage, val, lenVal < lenUsage ? lenVal : lenUsage);
      /* Ensure 0-terminated */
      device.usage[lenUsage - 1] = '\0';
    }
  }
}

void wsHandleMQTTIoTNodeInitResponse(const char *payload, size_t length)
{
  DynamicJsonDocument doc(mqttCalcResponseStructureSize(length));
  deserializeJson(doc, payload);
  if (wsMQTTIoTNodeInitErrorHandler(doc["errors"]))
  {
    _initState = InitState_t::failed;
    // TODO Send Feedback with some error data here!
    // wsSetInitValuesHandleGenericMessaging(num, "set-initValues-R", "iotnode", "INIT_FAILED_IDS_FROM_API");
    return;
  }
  wsStoreOutputIdsToRAM(doc["outputs"]);

  wsStoreConfigToEEPROM();
  _initState = InitState_t::succeed;
  wsSetInitValuesHandleGenericMessaging("set-initValues-R", "iotnode", "INIT_SUCCESS");
  //  === IoTNode == !
}

const size_t mqttCalcResponseStructureSize(size_t dataLength)
{
  const size_t baseSize = JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(lenOutputs) + lenOutputs * JSON_OBJECT_SIZE(2);
  return wsCalcDeserializeSizeBaseOrDouble(dataLength, baseSize);
}

bool wsMQTTIoTNodeInitErrorHandler(JsonArray errors)
{
  if (!errors)
  {
    return false;
  }
  // TODO compose WS response for browser.
  for (auto &&err : errors)
  {
    Serial.printf("~ ~ ~ ~ ~ err: %s\n", err.as<char *>());
  }
  return true;
}

void wsStoreOutputIdsToRAM(JsonArray outputs)
{
  for (size_t i = 0; i < lenOutputs; i++)
  {
    outDevices[i].id = outputs[i]["id"];
    // TODO debug only!
    Serial.printf("~ ~ ~ ~ ~ device.id: %llu\n", outDevices[i].id);
  }
}

void wsStoreConfigToEEPROM()
{
  for (OutputDevice_t &device : outDevices)
  {
    EEPROM.put(device.addressUsage, device.usage);
    EEPROM.put(device.addressId, device.id);
  }
  /* Intentionally saved separately, Init Mode End method will set this value in RAM.
   * This way probram overall stat still reflects that InitMode is on. */
  EEPROM.put(_AddressIoTState, IOTState_t::initialized);
  EEPROM.commit();
}

bool wsBroadcastInitStateDetails(const char *responseSubject)
{
  return wsBroadcastInitStateDetails(responseSubject, getPhaseStep(), getPhaseDesc());
}

bool wsBroadcastInitStateDetails(const char *responseSubject, const char *step, const char *descr)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsCalcResponseBaseSize() + wsCalcStateDetailsSize(detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  wsAddStateDetails(responseDoc, step, descr);
  return wsBroadcastTxtJsonResponse(responseDoc);
}

JsonDocument wsCreateResponse(const size_t docSize, const char *responseSubject, bool details)
{
  DynamicJsonDocument doc(docSize);
  doc.add(responseSubject);
  JsonObject data = doc.createNestedObject();
  data["state"] = (byte)_initState; // otherwise inferes wrong type (boolean)
  if (details)
  {
    data.createNestedArray(staDet);
  }
  return doc;
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
  const size_t size = measureJson(doc);
  char buffer[size + 1]; // make room for \0 as well.
  serializeJson(doc, buffer, size);
  return webSocket.sendTXT(num, buffer, size);
}

bool wsBroadcastTxtJsonResponse(JsonDocument &doc)
{
  const size_t size = measureJson(doc);
  char buffer[size + 1]; // make room for \0 as well.
  serializeJson(doc, buffer, size);
  return webSocket.broadcastTXT(buffer, size);
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

void wsSetInitValuesHandleWifiMessaging(const char *responseSubject, wl_status_t wifiState)
{
  wsSetInitValuesHandleGenericMessaging(responseSubject, "wifi", wifiHelpGetStateTxt(wifiState));
}

void wsSetInitValuesHandleMQTTMessaging(const char *responseSubject, lwmqtt_return_code_t mqttState)
{
  wsSetInitValuesHandleMQTTMessaging(responseSubject, mqttHelpGetStateTxt(mqttState));
}

void wsSetInitValuesHandleMQTTMessaging(const char *responseSubject, lwmqtt_err_t mqttState)
{
  wsSetInitValuesHandleMQTTMessaging(responseSubject, mqttHelpGetStateTxt(mqttState));
}

void wsSetInitValuesHandleMQTTMessaging(const char *responseSubject, const char *desc)
{
  wsSetInitValuesHandleGenericMessaging(responseSubject, "mqtt", desc);
}

void wsSetInitValuesHandleGenericMessaging(const char *responseSubject, const char *step, const char *desc)
{
  setPhase(step, desc);
  wsBroadcastInitStateDetails(responseSubject);
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

void clearTempOutputs()
{
  if (_initState == InitState_t::failed || _initState == InitState_t::working)
  {
    size_t lenUsage = sizeof(OutputDevice_t::usage);
    for (OutputDevice_t &device : outDevices)
    {
      memset(device.usage, '\0', lenUsage);
    }
  }
}

const char *getPhaseStep()
{
  return _currentPhase.step;
}

const char *getPhaseDesc()
{
  return _currentPhase.desc;
}
