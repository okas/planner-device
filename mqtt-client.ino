/* MQTT --- */

const char *MQTT_SERVER = "broker.hivemq.com";
const unsigned int MQTT_PORT = 1883;
const char *mqttUser = "";
const char *mqttPassword = "";

const char *topicRoot = "saartk/device";
const char *nodeName = "iotnode";
const char *cmndState = "state";
const char *cmndSetState = "set-state";
const char *cmndInit = "init";
const char *respInit = "init-r";
const char *cmndInitUpdate = "init-update";
const char *respInitUpdate = "init-update-r";
const char *logMessageBadTopic = "- - Bad topic, unknown command! End handler.\n";

/* --- MQTT */

int8_t mqttIoTInit()
{
  /* MQTT is already in Init Mode configuration. */
  if (_iotState == IOTState_t::initMode && mqttClient.connected())
  {
    return 0;
  }
  mqttConfigIoTInit();
  int8_t err;
  if (err = mqttConnect(2))
  {
    return err;
  }
  return mqttSubscriberIoTInit();
}

void mqttNormalInit()
{
  mqttConfigNormal();
  lwmqtt_return_code_t err;
  if (err = mqttConnect())
  {
    return;
  }
  mqttSubscriberNormal();
  mqttPublishPresentNormal();
}

void mqttConfigIoTInit()
{
  mqttClient.onMessageAdvanced(mqttMessageHandlerIoTInit);
  mqttClient.setOptions(10, false, 1000);
  mqttConfigCommon();
}

void mqttConfigNormal()
{
  mqttClient.onMessageAdvanced(mqttMessageHandlerNormal);
  mqttConfigCommon();
}

void mqttConfigCommon()
{
  char lastWillTopic[80];
  mqttGetSubscrForOther(lastWillTopic, nodeName, iotNodeId, "lost");
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic);
  mqttClient.setWill(lastWillTopic);
  mqttClient.begin(MQTT_SERVER, MQTT_PORT, espClient);
}

lwmqtt_return_code_t mqttConnect() { return mqttConnect(0); }

lwmqtt_return_code_t mqttConnect(uint8_t limit)
{
  Serial.println(" Connecting to MQTT...");
  lwmqtt_return_code_t result = LWMQTT_UNKNOWN_RETURN_CODE;
  size_t i = 1;
  while ((mqttClient.connect(iotNodeId, mqttUser, mqttPassword), result = mqttClient.returnCode()))
  {
    Serial.printf("MQTT connection failed with error (try: %d of %d) \"%s\"\n", i, limit, mqttHelpGetStateTxt(result));
    Serial.println();
    if (limit && ++i > limit)
    {
      break;
    }
    mqttClient.disconnect();
    delay(2000);
  }
  if (result == LWMQTT_CONNECTION_ACCEPTED)
  {
    Serial.println(" MQTT connected!");
  }
  return result;
}

lwmqtt_err_t mqttSubscriberIoTInit()
{
  lwmqtt_err_t err;
  for (auto &&type : {respInit, respInitUpdate})
  {
    char topic[80];
    mqttGetSubscrForOther(topic, nodeName, iotNodeId, type);
    Serial.printf("- - subscribing to Init* topic is : \"%s\"\n", topic);
    mqttClient.subscribe(topic);
    err = mqttClient.lastError();
    if (err)
    {
      break;
    }
  }
  return err;
}

bool mqttPublishIoTInit(int8_t &outErr, bool forceUpdate)
{
  bool ret;
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, forceUpdate ? cmndInitUpdate : cmndInit);
  JsonDocument payloadDoc = mqttGenerateInitPayload();
  const size_t size = measureJson(payloadDoc) + 1; // make room for \0 as well.
  char buffer[size];
  serializeJson(payloadDoc, buffer, size);
  Serial.printf("- - publishing Init topic : \"%s\"\n", topic);
  Serial.printf("- - payload is : \"%s\"\n", buffer);
  ret = mqttClient.publish(topic, buffer);
  outErr = ret ? 0 : mqttClient.lastError();
  return ret;
}

JsonDocument mqttGenerateInitPayload()
{
  DynamicJsonDocument payloadDoc(mqttCalcInitPayloadSize());
  payloadDoc["iottype"] = IOT_TYPE;
  JsonArray outputs = payloadDoc.createNestedArray("outputs");
  jsonGenerateOutputsArrayContentFromConfig(outputs);
  return payloadDoc;
}

const size_t mqttCalcInitPayloadSize()
{
  size_t result = JSON_OBJECT_SIZE(2) + calcOutputsArraySize();
  /* For copied bytes:
   * outputDevice.id: sizeof();
   * outputDevice.usage: room for `\0`. */
  result += lenOutputs * sizeof(OutputDevice_t::id) + lenOutputs;
  return result;
}

void mqttSubscriberNormal()
{
  Serial.printf(" subscribing to following topics: \n");
  const char *topicTypes[] = {cmndState, cmndSetState};
  for (OutputDevice_t &device : outDevices)
  {
    if (!strlen(device.usage))
    {
      continue;
    }
    char id[22];
    sprintf(id, "%llu", device.id);
    for (auto &&type : topicTypes)
    {
      mqttSubscribeOutputToCommand(device.usage, id, type);
    }
  }
  // TODO: subscribe to node topics?
  const char *topicApiPresent = "saartk/api/present";
  Serial.printf("  api related: \"%s\"\n", topicApiPresent);
  mqttClient.subscribe(topicApiPresent);
}

void mqttSubscribeOutputToCommand(const char *type, const char *id, const char *command)
{
  char topicCommand[120];
  mqttGetSubscrForCommand(topicCommand, type, id, command);
  mqttClient.subscribe(topicCommand);
  Serial.printf("  command topic: \"%s\"\n", topicCommand);
}

void mqttPublishPresentNormal()
{
  // Todo -- for every output and node?
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, "present");
  JsonDocument payloadDoc = mqttGeneratePresentPayload();
  const size_t size = measureJson(payloadDoc) + 1; // make room for \0 as well.
  char buffer[size];
  serializeJson(payloadDoc, buffer, size);
  mqttClient.publish(topic, buffer);
  Serial.printf("- - topicPresent is : \"%s\"\n", topic);
  Serial.printf("- - payload is : \"%s\"\n", buffer);
}

JsonDocument mqttGeneratePresentPayload()
{
  const size_t docSize = JSON_OBJECT_SIZE(1) +
                         JSON_ARRAY_SIZE(lenOutputs) +
                         (lenOutputs * JSON_OBJECT_SIZE(3));
  DynamicJsonDocument payloadDoc(docSize);
  JsonArray outputs = payloadDoc.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    if (!strlen(device.usage))
    {
      continue;
    }
    JsonObject out = outputs.createNestedObject();
    out["id"] = device.id;
    out["usage"] = (const char *)device.usage;
    out["state"] = device.state;
  }
  return payloadDoc;
}

void mqttGetSubscrForCommand(char *buffer, const char *type, const char *id, const char *command)
{
  generateSubscriptionBase(buffer, type, id);
  strcat(buffer, "/cmnd/");
  strcat(buffer, command);
  strcat(buffer, "/");
  strcat(buffer, "+");
}

void mqttGetSubscrForOther(char *buffer, const char *type, const char *id, const char *other)
{
  generateSubscriptionBase(buffer, type, id);
  strcat(buffer, "/");
  strcat(buffer, other);
}

void generateSubscriptionBase(char *buffer, const char *type, const char *id)
{
  strcpy(buffer, topicRoot);
  strcat(buffer, "/");
  strcat(buffer, type);
  strcat(buffer, "/");
  strcat(buffer, id);
}

void logMessage(char *topic, char *payload, int length)
{
  Serial.println(">>>>> Message:");
  Serial.printf(" topic: \"%s\"\n", topic);
  Serial.printf(" length of payload: %d\n", length);
  Serial.printf(" payload: \"%s\"\n", length ? payload : "");
  Serial.println("<<<<<");
}

void mqttMessageHandlerIoTInit(MQTTClient *client, char *topic, char *payload, int length)
{
  logMessage(topic, payload, length);
  // TODO fix all topic handling, its structure has changed!
  // Todo take care of order of if-else ladder!
  const vector<string> topicTokens = strsplit(topic, "/");
  const size_t len = topicTokens.size();
  if (len > 4 && topicTokens[4] == respInit)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/init-r */
    wsHandleMQTTIoTNodeInitResponse(payload, length);
  }
  else if (len > 4 && topicTokens[4] == respInitUpdate)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/init-update-r */
    wsHandleMQTTIoTNodeInitUpdateResponse(payload, length);
  }
  else
  {
    Serial.printf(logMessageBadTopic);
    return;
  }
}

void mqttMessageHandlerNormal(MQTTClient *client, char *topic, char *payload, int length)
{
  logMessage(topic, payload, length);
  // TODO fix all topic handling, its structure has changed!
  // Todo take care of order of if-else ladder!
  const vector<string> topicTokens = strsplit(topic, "/");
  const size_t len = topicTokens.size();
  if (len > 1 && topicTokens[1] == "api")
  { /* saartk/api/present */
    // ToDo: api present...
  }
  else if (len > 5 && topicTokens[4] == "cmnd" && topicTokens[5] == cmndState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    cmndStateHandler(topicTokens);
  }
  else if (len > 5 && topicTokens[4] == "cmnd" && topicTokens[5] == cmndSetState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    cmndSetStateHandler(topicTokens, payload, length);
  }
  else
  {
    Serial.printf(logMessageBadTopic);
    return;
  }
}

void cmndStateHandler(const vector<string> topicTokens)
{
  bool foundDevice;
  OutputDevice_t *device = findOutputDevice(topicTokens[3].c_str(), foundDevice);
  if (!foundDevice)
  {
    return;
  }
  publishResponseDeviceState(device, topicTokens);
}

void cmndSetStateHandler(const vector<string> topicTokens, char *payload, size_t length)
{
  bool foundDevice;
  OutputDevice_t *device = findOutputDevice(topicTokens[3].c_str(), foundDevice);
  if (!foundDevice)
  {
    return;
  }
  device->state = strtof(payload, nullptr);
  analogWrite(device->pin, round(device->state * 1024));
  EEPROM.put(device->addressState, device->state);
  EEPROM.commit();
  Serial.printf("- - set GPIO PIN value: \"%d\"=\"%f\"\n", device->pin, device->state);
  publishResponseDeviceState(device, topicTokens);
}

OutputDevice_t *findOutputDevice(const char *id, bool &found)
{
  uint64_t _id = strtoull(id, nullptr, 10);
  OutputDevice_t *foundDevice;
  for (OutputDevice_t &device : outDevices)
  {
    if (device.id == _id)
    {
      foundDevice = &device;
      break;
    }
  }
  if (foundDevice->pin)
  {
    found = true;
  }
  else
  {
    found = false;
    Serial.printf("- - didn't found device with id: %s\n", id);
  }
  return foundDevice;
}

void publishResponseDeviceState(OutputDevice_t *device, const vector<string> topicTokens)
{
  char responseTopic[120]{0};
  createResponseTopic(responseTopic, topicTokens);
  char payload[9];
  snprintf(payload, sizeof(payload), "%.6f", device->state);
  Serial.printf("- - responseTopic: \"%s\".\n", responseTopic);
  Serial.printf("- - responsePayload: \"%s\".\n", payload);
  mqttClient.publish(responseTopic, payload);
}

/**
 * It replaces specific levels value "cmnd" => "resp".
 * For example:
 *  saartk/device/lamp/FFFFFFFFFFFF/cmnd/command/+
 *  saartk/device/lamp/FFFFFFFFFFFF/resp/command/+
 */
char *createResponseTopic(char *buffer, const vector<string> topicTokens)
{
  const size_t lenTok = topicTokens.size();
  for (size_t i = 0; i < lenTok; i++)
  {
    if (i)
    {
      strcat(buffer, "/");
    }
    strcat(buffer, i != 4 ? topicTokens[i].c_str() : "resp");
  }
  return buffer;
}

void cleanupMqttInitMode()
{
  for (auto &&type : {respInit, respInitUpdate})
  {
    char topic[80];
    mqttGetSubscrForOther(topic, nodeName, iotNodeId, type);
    Serial.printf("- - unsubscribing from Init* topic: \"%s\"\n", topic);
    mqttClient.unsubscribe(topic);
  }
  mqttClient.disconnect();
}

void cleanupMqttNormalMode()
{
  const char *topicTypes[] = {cmndState, cmndSetState};
  for (OutputDevice_t &device : outDevices)
  {
    if (!strlen(device.usage))
    {
      continue;
    }
    char id[22];
    sprintf(id, "%llu", device.id);
    for (auto &&type : topicTypes)
    {
      char topicCommand[120];
      mqttGetSubscrForCommand(topicCommand, device.usage, id, type);
      mqttClient.subscribe(topicCommand);
      Serial.printf("- - unsubscribing from output topic: \"%s\"\n", topicCommand);
    }
  }
  mqttClient.disconnect();
}
