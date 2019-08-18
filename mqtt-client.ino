/* MQTT --- */

const char *MQTT_SERVER = "broker.hivemq.com";
const unsigned int MQTT_PORT = 1883;
const char *mqttUser = "";
const char *mqttPassword = "";

char topicBase[60];
const char *cmndState = "state";
const char *cmndSetState = "set-state";
const char *topicApiPresent = "saartk/api/present";

/* --- MQTT */

void mqttInit()
{
  mqttClient.setClient(espClient);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(onMessage);
  mqttConnect();
  mqttPublishPresent();
  mqttSubscriber();
}

void mqttConnect()
{
  const char *lost = "/lost";
  char lastWillTopic[strlen(topicBase) + strlen(lost) + 1];
  strcpy(lastWillTopic, topicBase);
  strcat(lastWillTopic, lost);
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic);
  while (!mqttClient.connected())
  {
    Serial.println(" Connecting to MQTT...");
    if (mqttClient.connect(iotDeviceId, mqttUser, mqttPassword, lastWillTopic, 0, false, ""))
    {
      Serial.println(" MQTT connected!");
    }
    else
    {
      Serial.printf(R"( failed with state "%d")", mqttClient.state());
      Serial.println();
      delay(2000);
    }
  }
}

void mqttPublishPresent()
{
  const char *present = "/present";
  char topicPresent[strlen(topicBase) + strlen(present) + 1];
  strcpy(topicPresent, topicBase);
  strcat(topicPresent, present);
  /* 15 is legth of JSON stucture as of current implementation.
     16 is max byte length of pin object. */
  char payload[15 + (16 * lenOutputs)];
  mqttGeneratePresentPayload(payload);
  mqttClient.publish(topicPresent, payload);
  Serial.printf("- - topicPresent is : \"%s\"\n", topicPresent);
  Serial.printf("- - payload is : \"%s\"\n", payload);
}

void mqttGeneratePresentPayload(char *payload)
{
  strcpy(payload, R"({"outputs":[)");
  for (size_t i = 0, ii = 0; i < lenOutputs; i++)
  {
    if (!outDevices[i].active)
    {
      continue;
    }
    if (ii++ > 0)
    {
      strcat(payload, ",");
    }
    char dev[16];
    sprintf(dev, R"({"%d":%.3f})", i, outDevices[i].state);
    strcat(payload, dev);
  }
  strcat(payload, "]}");
}

void mqttSubscriber()
{
  if (!getActiveOutputCount())
  {
    Serial.println("- - Cannot subscribe at MQTT broker, there are no activated outputs!");
    return;
  }
  Serial.printf(" subscribing to following topics: \n");
  for (size_t i = 0; i < lenOutputs; i++)
  {
    if (!outDevices[i].active)
    {
      continue;
    }
    mqttSubscribeOutputToCommand(i, cmndState);
    mqttSubscribeOutputToCommand(i, cmndSetState);
  }
  Serial.printf("  api related: \"%s\"\n", topicApiPresent);
  mqttClient.subscribe(topicApiPresent);
}

void mqttSubscribeOutputToCommand(size_t outputIndex, const char *command)
{
  char topicCommand[256];
  generateSubscriptionForOutput(topicCommand, outputIndex, command);
  mqttClient.subscribe(topicCommand);
  Serial.printf("  command topic: \"%s\"\n", topicCommand);
}

void generateSubscriptionForOutput(char *buffer, size_t outputIndex, const char *command)
{
  char indexBuff[33];
  itoa(outputIndex, indexBuff, 10);
  strcpy(buffer, topicBase);
  strcat(buffer, "/");
  strcat(buffer, indexBuff);
  strcat(buffer, "/cmnd/");
  strcat(buffer, command);
  strcat(buffer, "/");
  strcat(buffer, "+");
}

void onMessage(char *topic, byte *payload, unsigned int length)
{
  logMessage(topic, payload, length);
  commandMessageHandler(topic, payload, length);
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
  Output_t device = outDevices[outputIdx];
  device.state = state;
  analogWrite(device.pin, round(device.state * 1024));
  EEPROM.put(device.addressState, device.state);
  EEPROM.commit();
  Serial.printf("- - set GPIO PIN value: \"%d\"=\"%f\"\n", device.pin, device.state);
}

void publishResponseDeviceState(int8_t outputIdx, const vector<string> topicTokens)
{
  Output_t device = outDevices[outputIdx];
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

bool setTopicBase()
{
  if (strlen(iotDeviceId) == 0)
  {
    Serial.println("- - provided MQTT ClientId is empty, cannot set topic base!");
    return false;
  }
  memset(topicBase, 0, sizeof(topicBase));
  strcpy(topicBase, "saartk/device/lamp/");
  strcat(topicBase, iotDeviceId);
  Serial.printf("- - topicBase [result] is : \"%s\"\n", topicBase);
  return true;
}
