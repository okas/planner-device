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

lwmqtt_return_code_t mqttConnect(uint8_t limit = 0);

/* --- MQTT */

int8_t mqttIoTInit()
{
  /* MQTT is already in Init Mode configuration. */
  if (_iotState == IOTState_t::initMode && mqttClient.connected())
  {
    return 0;
  }
  mqttInit();
  int8_t err;
  if (err = mqttConnect(2))
  {
    return err;
  }
  return mqttSubscriberIoTInit();
}

void mqttNormalInit()
{
  mqttInit();
  lwmqtt_return_code_t err;
  if (err = mqttConnect())
  {
    return;
  }
  mqttSubscriberNormal();
  mqttPublishPresentNormal();
}

void mqttInit()
{
  mqttClient.begin(MQTT_SERVER, MQTT_PORT, espClient);
  char lastWillTopic[80];
  mqttGetSubscrForOther(lastWillTopic, nodeName, iotNodeId, "lost");
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic);
  mqttClient.setWill(lastWillTopic);
  mqttClient.onMessageAdvanced(mqttMessageHandler); // TODO check signatures
}

lwmqtt_return_code_t mqttConnect(uint8_t limit)
{
  Serial.println(" Connecting to MQTT...");
  lwmqtt_return_code_t result = LWMQTT_UNKNOWN_RETURN_CODE;
  size_t i = 1;
  while ((mqttClient.connect(iotNodeId, mqttUser, mqttPassword), result = mqttClient.returnCode()))
  {
    Serial.printf(R"( MQTT connection failed with error "%s")", mqttHelpGetStateTxt(result));
    Serial.println();
    if (limit && ++i > limit) //TODO put back pre-increment
    {
      break;
    }
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
  char topics[2][80];
  lwmqtt_err_t err;
  mqttGetSubscrForOther(topics[0], nodeName, iotNodeId, respInit);
  mqttGetSubscrForOther(topics[1], nodeName, iotNodeId, respInitUpdate);
  for (auto &&topic : topics)
  {
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
  for (OutputDevice_t &device : outDevices)
  {
    if (!strlen(device.usage))
    {
      continue;
    }
    char id[22];
    sprintf(id, "%llu", device.id);
    mqttSubscribeOutputToCommand(device.usage, id, cmndSetState);
    mqttSubscribeOutputToCommand(device.usage, id, cmndState);
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
    out["usage"] = device.usage;
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
  Serial.printf(" payload: %s\n", payload);
  Serial.println("<<<<<");
}

void printBuffer(const char *msg, byte *buffer, int length)
{
  Serial.printf("%s<", msg);
  for (unsigned int i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.print(buffer[i]);
  }
  Serial.println(">");
}

void mqttMessageHandler(MQTTClient *client, char *topic, char *payload, int length)
{
  logMessage(topic, payload, length);
  // TODO fix all topic handling, its structure has changed!
  // Todo take care of order of if-else ladder!
  const vector<string> topicTokens = strsplit(topic, "/");
  const size_t len = topicTokens.size();
  if (len > 1 && topicTokens[1] == "api")
  {
    // ToDo: api present...
  }
  else if (len > 4 && topicTokens[4] == respInit)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/init-r */
    wsHandleMQTTIoTNodeInitResponse(payload, length);
  }
  else if (len > 4 && topicTokens[4] == respInitUpdate)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/init-r */
    wsHandleMQTTIoTNodeInitUpdateResponse(payload, length);
  }
  else if (len > 5 && topicTokens[4] == "cmnd" && topicTokens[5] == cmndState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    int8_t idIdx = findOutputIndex(topicTokens);
    publishResponseDeviceState(idIdx, topicTokens);
  }
  else if (len > 5 && topicTokens[4] == "cmnd" && topicTokens[5] == cmndSetState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    cmndSetStateHandler(topicTokens, payload, length);
  }
  else
  {
    Serial.printf("- - Bad topic, unknown command! End handler.\n");
    return;
  }
}

void cmndSetStateHandler(const vector<string> topicTokens, char *buffer, size_t length)
{
  int8_t idIdx = findOutputIndex(topicTokens);
  if (idIdx < -1)
  {
    Serial.println("Topic don't contain known output index. End new setting the state!");
    return;
  }
  float state = bufferToFloat(buffer, length);
  if (idIdx == -1)
  {
    for (size_t i = 0; i < lenOutputs; i++)
    {
      setStateAndSave(i, state);
    }
  }
  else
  {
    setStateAndSave(idIdx, state);
  }
  publishResponseDeviceState(idIdx, topicTokens);
}

int8_t findOutputIndex(const vector<string> topicTokens)
{
  string x = topicTokens[4];
  if (x.length() == 0)
  { /* all devices/indices */
    return -1;
  }
  if (x == "0")
  {
    return 0;
  }
  unsigned int idx = atoi(x.c_str());
  if (idx < 0 || idx > lenOutputs - 1)
  {
    return -2;
  }
  return (int8_t)idx;
}

void setStateAndSave(int8_t outputIdx, float state)
{
  OutputDevice_t &device = outDevices[outputIdx];
  device.state = state;
  analogWrite(device.pin, round(device.state * 1024));
  EEPROM.put(device.addressState, device.state);
  EEPROM.commit();
  Serial.printf("- - set GPIO PIN value: \"%d\"=\"%f\"\n", device.pin, device.state);
}

void publishResponseDeviceState(int8_t outputIdx, const vector<string> topicTokens)
{
  OutputDevice_t &device = outDevices[outputIdx];
  // ToDo handle JSON responses as well
  char *responseTopic = createResponseTopic(topicTokens);
  char payload[sizeof(device.state)];
  *(float *)(payload) = device.state; // convert float to bytes
  Serial.printf("- - responseTopic: \"%s\".\n", responseTopic);
  printBuffer("- - responsePayload bytes: ", (byte *)payload, sizeof(payload));
  mqttClient.publish(responseTopic, payload);
}

char *createResponseTopic(const vector<string> topicTokens)
{
  const size_t lenTok = topicTokens.size();
  char result[sizeof(topicTokens) + lenTok];
  for (size_t i = 0; i < lenTok; i++)
  {
    if (i > 0)
    {
      strcat(result, "/");
    }
    strcat(result, i != 5 ? topicTokens[i].c_str() : "resp");
  }
  return result;
}
