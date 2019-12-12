/* Init Mode --- */

const char *STA_WIFI_KEY = "hellohello";
const float leaveInitTimeout = 30;
const char *staDet = "stateDetails";
const char *respSetInitValues = "set-initValues-R";
const char *respSetInitValuesUpdate = "set-initValuesUpdate-R";

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
  cleanupMqttNormalMode();
  boolean result = softAPInit();
  if (result)
  {
    wsInit();
    setPhase("iotnode", "idle");
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
  clearTempOutputs();
  clearPhase();
  cleanupMqttInitMode();
  webSocket.close();
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
      leaveIotInitMode();
    }
    return;
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
    return;
  }
  case WStype_TEXT: // if new text data is received
  {
    wsTXTMessageHandler(num, (char *)payload, lenght);
    return;
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
    wsSetInitValues(payloadDoc[1]);
  }
  else if (strcmp(subject, "set-initValuesUpdate") == 0)
  {
    wsSetInitValuesUpdate(payloadDoc[1]);
  }
  else if (strcmp(subject, "get-currentConfig") == 0)
  {
    JsonDocument responseDocument = wsGetInitStateDoc("get-currentConfig-R", true);
    wsSendTxtJsonResponse(num, responseDocument);
  }
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
    wsGetInitStateDocIncludeConfig(doc);
  }
  return doc;
}

void wsGetInitStateDocIncludeConfig(JsonDocument &doc)
{
  JsonObject data = doc[1];
  data["iotDeviceId"] = (const char *)iotNodeId;
  data["ssid"] = WiFi.SSID();
  data["psk"] = WiFi.psk();
  data["iotType"] = IOT_TYPE;
  JsonArray outputs = data.createNestedArray("outputs");
  jsonGenerateOutputsArrayContentFromConfig(outputs);
}

void wsSetInitValues(JsonObject payloadObj)
{
  if (!wsSetInitValuesCommon(respSetInitValues, payloadObj, false))
  {
    return;
  }
  // TODO Init ping-pong with API server here?
  wsSetInitValuesHandleIoTNodeMessaging(respSetInitValues, "INIT_WAITING_IDS_FROM_API");
}

void wsSetInitValuesUpdate(JsonObject payloadObj)
{
  if (!wsSetInitValuesCommon(respSetInitValuesUpdate, payloadObj, true))
  {
    return;
  }
  /* All good untill here -- we are waiting response from API server */
  // TODO Init ping-pong with API server here?
  wsSetInitValuesHandleIoTNodeMessaging(respSetInitValuesUpdate, "INITUPDATE_WAITING_CONFIRM_FROM_API");
}

bool wsSetInitValuesCommon(const char *responseSubject, JsonObject payloadObj, bool forUpdate)
{
  _initState = InitState_t::working;
  wsStoreOutputsToRAM(payloadObj["outputs"]);
  if (!wsSetInitValuesCommonPhaseWiFi(responseSubject, payloadObj))
  {
    return false;
  }
  if (!wsSetInitValuesCommonPhaseMQTT(responseSubject, payloadObj, forUpdate))
  {
    return false;
  }
  return true;
}

void wsStoreOutputsToRAM(JsonArray outputs)
{
  // TODO complement correct state signaling with '_initState = InitState_t::'
  size_t lenUsage = sizeof(OutputDevice_t::usage);
  for (size_t i = 0; i < lenOutputs; i++)
  {
    OutputDevice_t &device = outDevices[i];
    JsonObject outObj = outputs[i];
    device.id = outObj["id"];
    const char *val = outObj["usage"] | "";
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

bool wsSetInitValuesCommonPhaseWiFi(const char *responseSubject, JsonObject payloadObj)
{
  const char *ssid = payloadObj["ssid"];
  const char *psk = payloadObj["psk"];
  if (WiFi.SSID() == ssid && WiFi.status() == WL_CONNECTED)
  {
    wsSetInitValuesHandleWifiMessaging(responseSubject, WL_CONNECTED);
    return true;
  }
  bool result;
  WiFi.begin(ssid, psk);
  int8_t wifiResult = WiFi.waitForConnectResult(10000);
  if (wifiResult == WL_CONNECTED)
  {
    result = true;
  }
  else
  {
    _initState = InitState_t::failed;
    WiFi.setAutoReconnect(false);
    result = false;
  }
  wsSetInitValuesHandleWifiMessaging(responseSubject, wifiResult);
  return result;
}
bool wsSetInitValuesCommonPhaseMQTT(const char *responseSubject, JsonObject payloadObj, bool forUpdate)
{
  int8_t mqttState = mqttIoTInit();
  wsSetInitValuesHandleMQTTMessaging(responseSubject, (lwmqtt_return_code_t)mqttState);
  if (mqttState != 0)
  {
    _initState = InitState_t::failed;
    return false;
  }
  bool pubResult = mqttPublishIoTInit(mqttState, forUpdate);
  if (!pubResult)
  {
    _initState = InitState_t::failed;
    wsSetInitValuesHandleMQTTMessaging(responseSubject, (lwmqtt_err_t)mqttState);
    wsSetInitValuesHandleMQTTMessaging(responseSubject, "__PUBLISH_FAILED");
    return false;
  }
  return true;
}

void wsHandleMQTTIoTNodeInitResponse(const char *payload, size_t length)
{
  if (wsHandleMQTTIoTInitErrors(respSetInitValues, "INIT_FAILED_IDS_FROM_API", payload, length))
  {
    return;
  }
  DynamicJsonDocument doc(mqttCalcResponseStructureSize(length));
  deserializeJson(doc, payload);
  JsonArray outputs = doc["outputs"];
  if (!outputs)
  {
    wsHandleMQTTITInitUnknownResponse(respSetInitValues, payload, length);
    return;
  }
  wsStoreOutputIdsToRAM(outputs);
  wsHandleMQTTIoTNodeInitFinalize();
  wsSetInitValuesHandleIoTNodeMessaging(respSetInitValues, "INIT_SUCCESS");
}

void wsHandleMQTTIoTNodeInitUpdateResponse(const char *payload, size_t length)
{
  if (wsHandleMQTTIoTInitErrors(respSetInitValues, "INITUPDATE_FAILED_FROM_API", payload, length))
  {
    return;
  }
  DynamicJsonDocument doc(mqttCalcResponseStructureSize(length));
  deserializeJson(doc, payload);
  if (doc["state"] != "ok")
  {
    wsHandleMQTTITInitUnknownResponse(respSetInitValuesUpdate, payload, length);
    return;
  }
  wsHandleMQTTIoTNodeInitFinalize();
  wsSetInitValuesHandleIoTNodeMessaging(respSetInitValuesUpdate, "INITUPDATE_SUCCESS");
}

void wsHandleMQTTITInitUnknownResponse(const char *responseSubject, const char *payload, size_t length)
{
  _initState = InitState_t::failed;
  wsSetInitValuesHandleIoTNodeMessaging(responseSubject, "INIT_FAILED_UNKNOWS_RESPONSE", payload, length);
}

void wsHandleMQTTIoTNodeInitFinalize()
{
  wsStoreConfigToEEPROM();
  _initState = InitState_t::succeed;
}

bool wsHandleMQTTIoTInitErrors(const char *responseSubject, const char *failedDescription, const char *payload, size_t length)
{
  /* Avoid deserialization just yet, because with errors JSON is quite big,
   * and it can be serialized directly to State Messaging WS response. */
  if (!strstr(payload, "\"errors\":["))
  {
    return false;
  }
  _initState = InitState_t::failed;
  wsSetInitValuesHandleIoTNodeMessaging(responseSubject, failedDescription, payload, length);
  return true;
}

void wsStoreOutputIdsToRAM(JsonArray outputs)
{
  for (size_t i = 0; i < lenOutputs; i++)
  {
    outDevices[i].id = outputs[i]["id"];
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
   * This way program's overall stat still reflects that InitMode is on.
   * This state will be written in leaveIotInitMode() as well to reflect very final state!*/
  eepromIoTStateStore(IOTState_t::initialized);
}

bool wsBroadcastInitStateDetails(const char *responseSubject)
{
  return wsBroadcastInitStateDetails(responseSubject, getPhaseStep(), getPhaseDesc());
}

bool wsBroadcastInitStateDetails(const char *responseSubject, const char *step, const char *descr)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsCalcResponseStateDetailsSize(detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  wsAddStateDetails(responseDoc, step, descr);
  return wsBroadcastTxtJsonResponse(responseDoc);
}

bool wsBroadcastInitStateDetailsMQTTErrors(const char *responseSubject, const char *errorsJson, size_t lenErrors)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsCalcResponseStateDetailsSize(detailsCount) + lenErrors;
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  wsAddStateDetails(responseDoc);
  responseDoc[1]["apiresult"] = serialized(errorsJson, lenErrors);
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
  const size_t size = measureJson(doc) + 1; // make room for \0 as well.
  char buffer[size];
  return webSocket.sendTXT(num, buffer, serializeJson(doc, buffer, size));
}

bool wsBroadcastTxtJsonResponse(JsonDocument &doc)
{
  const size_t size = measureJson(doc) + 1; // make room for \0 as well.
  char buffer[size];
  return webSocket.broadcastTXT(buffer, serializeJson(doc, buffer, size));
}

const size_t wsCalcIncomingJsonSize(size_t dataLength)
{
  const size_t baseSize = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + calcOutputsArraySize();
  return wsCalcDeserializeSizeBaseOrDouble(dataLength, baseSize);
}

const size_t mqttCalcResponseStructureSize(size_t dataLength)
{
  const size_t baseSize = mqttCalcErrorsStuctureBaseSize();
  return wsCalcDeserializeSizeBaseOrDouble(dataLength, baseSize);
}

const size_t mqttCalcErrorsStuctureBaseSize()
{
  // TODO needs cleanup
  return JSON_OBJECT_SIZE(2) +             // Main object variants: a) "outputs" b) "errors" and/or "existing" c) "state"
         JSON_ARRAY_SIZE(10) +             // for b) "errors", limited to 10 itmes
         JSON_ARRAY_SIZE(lenOutputs) +     // "outputs" array length
         lenOutputs * JSON_OBJECT_SIZE(2); // output objects, that have 2 members
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
    result += calcOutputsArraySize();
    /* PSK+1 + SSID+1 */
    result += 65 + 33;
  }
  return result;
}

const size_t wsCalcResponseStateDetailsSize(size_t detailsCount)
{
  return wsCalcResponseBaseSize() + wsCalcStateDetailsSize(detailsCount);
}

void wsSetInitValuesHandleWifiMessaging(const char *responseSubject, int8_t wifiState)
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

void wsSetInitValuesHandleIoTNodeMessaging(const char *responseSubject, const char *desc)
{
  wsSetInitValuesHandleGenericMessaging(responseSubject, "iotnode", desc);
}
void wsSetInitValuesHandleIoTNodeMessaging(const char *responseSubject, const char *desc, const char *errorsJson, size_t lenErrors)
{
  setPhase("iotnode", desc);
  wsBroadcastInitStateDetailsMQTTErrors(responseSubject, errorsJson, lenErrors);
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
      device.id = 0ULL;
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
