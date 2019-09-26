void eepromInitialize()
{
  size_t eepromSize = eepromCalcAddresses();
  EEPROM.begin(eepromSize);
  eepromInitstateInfo();
  /* In case .put() was called */
  EEPROM.commit();
}

size_t eepromCalcAddresses()
{
  size_t size = 0;
  size_t outStateValueSize = sizeof(OutputDevice_t::state);
  size_t outActiveValueSize = sizeof(OutputDevice_t::active);
  for (OutputDevice_t &item : outDevices)
  {
    item.addressState = size;
    size += outStateValueSize;
    item.addressActive = size;
    size += outActiveValueSize;
  }
  return size;
}

void eepromInitstateInfo()
{
  /* Init Output states from EEPROM to variable (array) */
  for (OutputDevice_t &item : outDevices)
  {
    EEPROM.get(item.addressState, item.state);
    if (isnan(item.state))
    { /* init to get rid of 0xFF */
      item.state = 0.0f;
      EEPROM.put(item.addressState, item.state);
    }
    byte temp;
    EEPROM.get(item.addressActive, temp);
    if (temp != 0xFF)
    {
      item.active = (bool)temp;
    }
    else
    { /* init to get rid of 0xFF */
      item.active = false;
      EEPROM.put(item.addressActive, item.active);
    }
  }
}
