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
  size_t size = 0; // This statement's value is the very start address of the EEPROM allocation.
  size_t outStateValueSize = sizeof(OutputDevice_t::state);
  size_t outUsageValueSize = sizeof(OutputDevice_t::usage);
  for (OutputDevice_t &item : outDevices)
  {
    item.addressState = size;
    size += outStateValueSize;
    item.addressUsage = size;
    size += outUsageValueSize;
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
    EEPROM.get(item.addressUsage, item.usage);
    if (memchr(item.usage, 0xFF, sizeof(item.usage)))
    { /* init to get rid of 0xFF */
      memset(item.usage, '\0', sizeof(item.usage));
      EEPROM.put(item.addressUsage, item.usage);
    }
  }
}
