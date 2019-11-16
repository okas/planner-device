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

lwmqtt_return_code_t mqttConnect(uint8_t limit = 0);

/* --- MQTT */

int8_t mqttIoTInit()
{
  int8_t err;
  mqttInit();
  if (err = mqttConnect(2))
  {
    return err;
  }
  return mqttSubscriberIoTInit();
}

void mqttNormalInit()
{
  mqttInit();
  mqttConnect();
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
  lwmqtt_return_code_t result;
  for (size_t i = 0; i < limit; i = limit ? i + 1 : -1)
  {
    if (i)
    {
      delay(2000);
    }
    mqttClient.connect(iotNodeId, mqttUser, mqttPassword);
    result = mqttClient.returnCode();
    if (result == LWMQTT_CONNECTION_ACCEPTED)
    {
      Serial.println(" MQTT connected!");
      break;
    }
    /* TODO
     * Filter out certain erros that do not deserv reattempts */
    Serial.printf(R"( MQTT connection failed with error "%d")", result);
    Serial.println();
  }
  return result;
}

lwmqtt_err_t mqttSubscriberIoTInit()
{
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, respInit);
  Serial.printf("- -subscribing to Init topic is : \"%s\"\n", topic);
  if (mqttClient.subscribe(topic))
  {
    return (lwmqtt_err_t)0;
  }
  return mqttClient.lastError();
}

bool mqttPublishIoTInit(int8_t &outErr)
{
  bool ret;
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, cmndInit);
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
  size_t docSize = JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(lenOutputs) + (lenOutputs * JSON_OBJECT_SIZE(2));
  /* For copied bytes */
  docSize += 10;
  DynamicJsonDocument payloadDoc(docSize);
  payloadDoc["iottype"] = IOT_TYPE;
  auto outputs = payloadDoc.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    auto out = outputs.createNestedObject();
    out["id"] = device.id;
    out["usage"] = (const char *)device.usage;
  }
  return payloadDoc;
}

void mqttSubscriberNormal()
{
  Serial.printf(" subscribing to following topics: \n");
  for (OutputDevice_t &device : outDevices)
  {
    const char *type = device.usage;
    if (strlen(type) == 0)
    {
      continue;
    }
    mqttSubscribeOutputToCommand(type, l64a(device.id), cmndSetState);
    mqttSubscribeOutputToCommand(type, l64a(device.id), cmndState);
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
  const size_t docSize = JSON_ARRAY_SIZE(lenOutputs) + (1 + lenOutputs * JSON_OBJECT_SIZE(1));
  DynamicJsonDocument payloadDoc(docSize);
  auto outputs = payloadDoc.createNestedArray("outputs");
  for (size_t i = 0, ii = 0; i < lenOutputs; i++)
  {
    const char *type = outDevices[i].usage;
    if (strlen(type) == 0)
    {
      continue;
    }
    auto out = outputs.createNestedObject();
    out[l64a(outDevices[i].id)] = outDevices[i].state;
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
  Serial.printf(" length, payload: %d\n", length);
  printBuffer(" payload bytes: ", payload, length);
  Serial.println("<<<<<");
}

void printBuffer(const char *msg, char *buffer, int length)
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
  else if (len > 6 && topicTokens[6] == cmndState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    int8_t idIdx = findOutputIndex(topicTokens);
    publishResponseDeviceState(idIdx, topicTokens);
  }
  else if (len > 6 && topicTokens[6] == cmndSetState)
  { /* saartk/device/iotnode/FFFFFFFFFFFF/cmnd/command/+ */
    cmndSetStateHandler(topicTokens, payload, length);
  }
  /* saartk/device/iotnode/FF:FF:FF:FF:FF:FF/init-r */
  // else if (topicTokens[4] == cmndSetState)
  // {
  //   cmndSetStateHandler(topicTokens, payload, length);
  // }
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
  printBuffer("- - responsePayload bytes: ", payload, sizeof(payload));
  mqttClient.publish(responseTopic, payload, sizeof(payload));
}

char *createResponseTopic(const vector<string> topicTokens)
{
  char result[sizeof(topicTokens) + topicTokens.size()];
  for (size_t i = 0; i < topicTokens.size(); i++)
  {
    if (i > 0)
    {
      strcat(result, "/");
    }
    strcat(result, i != 5 ? topicTokens[i].c_str() : "resp");
  }
  return result;
}
