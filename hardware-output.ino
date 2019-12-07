void hwWriteStatesFromRAM()
{
  for (OutputDevice_t &device : outDevices)
  {
    if (strlen(device.usage) > 0)
    {
      hwOutputSetToState(device);
    }
    else
    {
      hwTurnOffOutput(device);
    }
  }
  EEPROM.commit();
}

void hwOutputsTurnOffActive()
{
  for (OutputDevice_t &device : outDevices)
  {
    if (strlen(device.usage) > 0)
    {
      hwTurnOffOutput(device);
    }
  }
  EEPROM.commit();
}

void hwOutputSetToState(OutputDevice_t &device)
{
  pinMode(device.pin, OUTPUT);
  analogWrite(device.pin, round(device.state * 1024));
}

void hwTurnOffOutput(OutputDevice_t &device)
{
  pinMode(device.pin, OUTPUT);
  analogWrite(device.pin, device.state = 0);
  EEPROM.put(device.addressState, device.state);
}
