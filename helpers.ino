union UnionFloatByte {
  float f;
  byte b[sizeof f];
};

float bufferToFloat(char *buffer, unsigned int length)
{
  UnionFloatByte temp;
  for (size_t i = 0; i < length; i++)
  {
    temp.b[i] = buffer[i];
  }
  return temp.f;
}

vector<string> strsplit(const char *phrase, const char *delimiter)
{
  string s = phrase;
  vector<string> ret;
  size_t start = 0;
  size_t end = 0;
  size_t len = 0;
  string token;
  do
  {
    end = s.find(delimiter, start);
    len = end - start;
    token = s.substr(start, len);
    ret.emplace_back(token);
    start += len + strlen(delimiter);
  } while (end != string::npos);
  ret.shrink_to_fit();
  return ret;
}

char *llutoa(uint64_t value)
{
  char buffer[sizeof(int64_t) * 8 + 1];
  sprintf(buffer, "%llu", value);
  return buffer;
}

size_t getInUseOutputCount()
{
  size_t result = 0;
  for (OutputDevice_t &device : outDevices)
  {
    if (device.usage[0] != '\0')
    {
      result++;
    }
  }
  return result;
}

char *getWifiHostname()
{
  uint8_t mac[6];
  char result[13] = {0};
  WiFi.macAddress(mac);
  strcpy(result, "ESP_");
  for (size_t i = 3; i < 6; i++)
  {
    char b[3];
    sprintf(b, "%02X", mac[i]);
    strcat(result, b);
  }
  return result;
}

char *getWiFiMACHex()
{
  uint8_t mac[6];
  char result[13] = {0};
  WiFi.macAddress(mac);
  sprintf(result, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return result;
}

const size_t wsCalcDeserializeSizeBaseOrDouble(const size_t dataLength, const size_t baseLen)
{
  const size_t doubleDataLen = dataLength * 2;
  /* Ensure that even quite empty JSON can be deserialized, using serialization minimum suitable value. */
  return (doubleDataLen < baseLen ? baseLen : doubleDataLen) + 1;
}

bool eepromIoTStateStore(IOTState_t state)
{
  EEPROM.put(_AddressIoTState, state);
  return EEPROM.commit();
}

void jsonGenerateOutputsArrayContentFromConfig(JsonArray outputsArray)
{
  for (OutputDevice_t &device : outDevices)
  {
    JsonObject out = outputsArray.createNestedObject();
    out["id"] = device.id;
    out["usage"] = (const char *)device.usage;
  }
}

const size_t calcOutputsArraySize()
{
  return JSON_ARRAY_SIZE(lenOutputs) + (lenOutputs * JSON_OBJECT_SIZE(2));
}

const char *wifiHelpGetStateTxt(int status)
{
  switch (status)
  {
  case WL_IDLE_STATUS:
    return "WL_IDLE_STATUS";
  case WL_NO_SSID_AVAIL:
    return "WL_NO_SSID_AVAIL";
  case WL_SCAN_COMPLETED:
    return "WL_SCAN_COMPLETED";
  case WL_CONNECTED:
    return "WL_CONNECTED";
  case WL_CONNECT_FAILED:
    return "WL_CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "WL_CONNECTION_LOST";
  case WL_DISCONNECTED:
    return "WL_DISCONNECTED";
  default:
    return "UNKNOWN WIFI STATUS!";
  }
}

const char *mqttHelpGetStateTxt(lwmqtt_return_code_t status)
{
  switch (status)
  {
  case LWMQTT_CONNECTION_ACCEPTED:
    return "LWMQTT_CONNECTION_ACCEPTED";
  case LWMQTT_UNACCEPTABLE_PROTOCOL:
    return "LWMQTT_UNACCEPTABLE_PROTOCOL";
  case LWMQTT_IDENTIFIER_REJECTED:
    return "LWMQTT_IDENTIFIER_REJECTED";
  case LWMQTT_SERVER_UNAVAILABLE:
    return "LWMQTT_SERVER_UNAVAILABLE";
  case LWMQTT_BAD_USERNAME_OR_PASSWORD:
    return "LWMQTT_BAD_USERNAME_OR_PASSWORD";
  case LWMQTT_NOT_AUTHORIZED:
    return "LWMQTT_NOT_AUTHORIZED";
  case LWMQTT_UNKNOWN_RETURN_CODE:
    return "LWMQTT_UNKNOWN_RETURN_CODE";
  default:
    return "UNKNOWN MQTT STATUS!";
  }
}

const char *mqttHelpGetStateTxt(lwmqtt_err_t status)
{
  switch (status)
  {
  case LWMQTT_SUCCESS:
    return "LWMQTT_SUCCESS";
  case LWMQTT_BUFFER_TOO_SHORT:
    return "LWMQTT_BUFFER_TOO_SHORT";
  case LWMQTT_VARNUM_OVERFLOW:
    return "LWMQTT_VARNUM_OVERFLOW";
  case LWMQTT_NETWORK_FAILED_CONNECT:
    return "LWMQTT_NETWORK_FAILED_CONNECT";
  case LWMQTT_NETWORK_TIMEOUT:
    return "LWMQTT_NETWORK_TIMEOUT";
  case LWMQTT_NETWORK_FAILED_READ:
    return "LWMQTT_NETWORK_FAILED_READ";
  case LWMQTT_NETWORK_FAILED_WRITE:
    return "LWMQTT_NETWORK_FAILED_WRITE";
  case LWMQTT_REMAINING_LENGTH_OVERFLOW:
    return "LWMQTT_REMAINING_LENGTH_OVERFLOW";
  case LWMQTT_REMAINING_LENGTH_MISMATCH:
    return "LWMQTT_REMAINING_LENGTH_MISMATCH";
  case LWMQTT_MISSING_OR_WRONG_PACKET:
    return "LWMQTT_MISSING_OR_WRONG_PACKET";
  case LWMQTT_CONNECTION_DENIED:
    return "LWMQTT_CONNECTION_DENIED";
  case LWMQTT_FAILED_SUBSCRIPTION:
    return "LWMQTT_FAILED_SUBSCRIPTION";
  case LWMQTT_SUBACK_ARRAY_OVERFLOW:
    return "LWMQTT_SUBACK_ARRAY_OVERFLOW";
  case LWMQTT_PONG_TIMEOUT:
    return "LWMQTT_PONG_TIMEOUT";
  default:
    return "UNKNOWN MQTT STATUS!";
  }
}
