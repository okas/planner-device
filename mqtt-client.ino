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

bool mqttConnect(uint8_t limit = 0);

/* --- MQTT */

bool mqttIoTInit()
{
  mqttInit();
  if (!mqttConnect(2))
  {
    return false;
  }
  if (!mqttSubscriberIoTInit())
  {
    return false;
  }
  if (mqttPublishIoTInit())
  {
    return false;
  }
  return true;
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
  mqttClient.setClient(espClient)
      .setServer(MQTT_SERVER, MQTT_PORT)
      .setCallback(logMessage)
      .setCallback(commandMessageHandler);
}

bool mqttConnect(uint8_t limit)
{
  char lastWillTopic[80];
  mqttGetSubscrForOther(lastWillTopic, nodeName, iotNodeId, "lost");
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic);
  for (size_t i = 0; !mqttClient.connected() && i < limit; i = limit ? i + 1 : 0)
  {
    Serial.println(" Connecting to MQTT...");
    if (mqttClient.connect(iotNodeId, mqttUser, mqttPassword, lastWillTopic, 0, false, ""))
    {
      Serial.println(" MQTT connected!");
      return true;
    }
    else
    {
      Serial.printf(R"( failed with state "%d")", mqttClient.state());
      Serial.println();
      delay(2000);
    }
  }
  return false;
}

bool mqttSubscriberIoTInit()
{
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, respInit);
  Serial.printf("- -subscribing to Init topic is : \"%s\"\n", topic);
  return mqttClient.subscribe(topic);
}

bool mqttPublishIoTInit()
{
  char topic[80];
  mqttGetSubscrForOther(topic, nodeName, iotNodeId, cmndInit);
  JsonDocument payloadDoc = mqttGenerateInitPayload();
  const size_t size = measureJson(payloadDoc) + 1; // make room for \0 as well.
  char buffer[size];
  serializeJson(payloadDoc, buffer, size);
  Serial.printf("- - publishing Init topic : \"%s\"\n", topic);
  Serial.printf("- - payload is : \"%s\"\n", buffer);
  return mqttClient.publish(topic, buffer);
}

JsonDocument mqttGenerateInitPayload()
{
  const size_t docSize = JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(lenOutputs) + (lenOutputs * JSON_OBJECT_SIZE(2));
  DynamicJsonDocument payloadDoc(docSize);
  payloadDoc["iottype"] = IOT_TYPE;
  auto outputs = payloadDoc.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    auto out = outputs.createNestedObject();
    out["id"] = device.id;
    out["usage"] = device.usage;
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

void logMessage(char *topic, byte *payload, size_t length)
{
  Serial.println(">>>>> Message:");
  Serial.printf(" topic: \"%s\"\n", topic);
  Serial.printf(" length, payload: %d\n", length);
  printBuffer(" payload bytes: ", payload, length);
  Serial.println("<<<<<");
}

void printBuffer(const char *msg, byte *buffer, size_t length)
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

void commandMessageHandler(char *topic, byte *payload, size_t length)
{
  const vector<string> topicTokens = strsplit(topic, "/");
  if (topicTokens[1] == "api")
  {
    // ToDo: api present...
  }
  else if (topicTokens[6] == cmndState)
  {
    int8_t idIdx = findOutputIndex(topicTokens);
    publishResponseDeviceState(idIdx, topicTokens);
  }
  else if (topicTokens[6] == cmndSetState)
  {
    cmndSetStateHandler(topicTokens, payload, length);
  }
  else
  {
    Serial.printf("- - Bad topic, unknown command! End handler.\n");
    return;
  }
}

void cmndSetStateHandler(const vector<string> topicTokens, byte *buffer, size_t length)
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
  byte payload[sizeof(device.state)];
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

const char *mqttHelpGetStateTxt(int status)
{
  switch (status)
  {
  case MQTT_CONNECTION_TIMEOUT:
    return "MQTT_CONNECTION_TIMEOUT";
  case MQTT_CONNECTION_LOST:
    return "MQTT_CONNECTION_LOST";
  case MQTT_CONNECT_FAILED:
    return "MQTT_CONNECT_FAILED";
  case MQTT_DISCONNECTED:
    return "MQTT_DISCONNECTED";
  case MQTT_CONNECTED:
    return "MQTT_CONNECTED";
  case MQTT_CONNECT_BAD_PROTOCOL:
    return "MQTT_CONNECT_BAD_PROTOCOL";
  case MQTT_CONNECT_BAD_CLIENT_ID:
    return "MQTT_CONNECT_BAD_CLIENT_ID";
  case MQTT_CONNECT_UNAVAILABLE:
    return "MQTT_CONNECT_UNAVAILABLE";
  case MQTT_CONNECT_BAD_CREDENTIALS:
    return "MQTT_CONNECT_BAD_CREDENTIALS";
  case MQTT_CONNECT_UNAUTHORIZED:
    return "MQTT_CONNECT_UNAUTHORIZED";
  default:
    return "UNKNOWN MQTT STATUS!";
  }
}
