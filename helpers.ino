union UnionFloatByte {
  float f;
  byte b[sizeof f];
};

float bufferToFloat(byte *buffer, unsigned int length)
{
  UnionFloatByte temp;
  for (size_t i = 0; i < length; i++)
  {
    temp.b[i] = buffer[i];
  }
  return temp.f;
}

vector<string> strsplit(char *phrase, char *delimiter)
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
  return ret;
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

char *getWiFiMACHex()
{
  uint8_t mac[6];
  char macStr[13] = {0};
  WiFi.macAddress(mac);
  sprintf(macStr, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return macStr;
}
