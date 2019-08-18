bool wifiStationConnect(const char *ssid, const char *psk)
{
  if (strcmp(ssid, WiFi.SSID().c_str()) == 0 && strcmp(psk, WiFi.psk().c_str()) == 0)
  {
    return wifiStationConnect();
  }
  else
  {
    WiFi.disconnect();
    WiFi.begin(ssid, psk);
    return wifiStationConnectVerifier();
  }
}

bool wifiStationConnect()
{
  if (strlen(WiFi.SSID().c_str()) == 0 || strlen(WiFi.psk().c_str()) == 0)
  {
    Serial.printf("\nNo WiFi SSID or PSK stored, end connecting.\n");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  return wifiStationConnectVerifier();
}

bool wifiStationConnectVerifier()
{
  Serial.printf("\nConnecting to Wifi");
  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(" .");
    if (++i == 10)
    {
      Serial.printf("\nfailed to connect in %d attempts to SSID \"%s\"\n", i, WiFi.SSID().c_str());
      WiFi.disconnect();
      return false;
    }
  }
  Serial.printf("\nConnected to SSID \"%s\"\n", WiFi.SSID().c_str());
  return true;
}
