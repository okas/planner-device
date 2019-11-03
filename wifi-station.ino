bool wifiStationInit(const char *ssid, const char *psk)
{
  WiFi.begin(ssid, psk);
  return wifiStationConnectVerifier();
}

bool wifiStationConnect()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
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
      return false;
    }
  }
  Serial.printf("\nConnected to SSID \"%s\"\n", WiFi.SSID().c_str());
  return true;
}
