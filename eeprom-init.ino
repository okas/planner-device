void eepromInitialize()
{
  const size_t eepromSize = eepromCalcAddresses();
  EEPROM.begin(eepromSize);
  EEPROM.get(_AddressEEPROMInitState, _isEEPROMInit);
  if (_isEEPROMInit != 1)
  {
    eepromInitIoTNodeState();
  }
  else
  {
    eepromGetIoTNodeState();
  }
}

const size_t eepromCalcAddresses()
{
  /* Experiencing some wearout! */
  size_t size = 20; // This statement's value is the very start address of the EEPROM allocation.
  eepromCalcEEPROMState(&size);
  eepromCalcIoTStateAddresses(&size);
  eepromCalcOutputsAddresses(&size);
  return size;
}

void eepromCalcEEPROMState(size_t *size)
{
  _AddressEEPROMInitState = *size;
  *size += sizeof(_isEEPROMInit);
}

void eepromCalcIoTStateAddresses(size_t *size)
{
  _AddressIoTState = *size;
  *size += sizeof(_iotState);
}

void eepromCalcOutputsAddresses(size_t *size)
{
  size_t idSize = sizeof(OutputDevice_t::id);
  size_t stateSize = sizeof(OutputDevice_t::state);
  size_t usageSize = sizeof(OutputDevice_t::usage);
  for (OutputDevice_t &item : outDevices)
  {
    item.addressId = *size;
    *size += idSize;
    item.addressState = *size;
    *size += stateSize;
    item.addressUsage = *size;
    *size += usageSize;
  }
}

void eepromInitIoTNodeState()
{
  EEPROM.put(_AddressIoTState, _iotState = IOTState_t::started);
  for (OutputDevice_t &item : outDevices)
  {
    EEPROM.put(item.addressId, item.id = 0ULL);
    EEPROM.put(item.addressState, item.state = 0.0F);
    memset(item.usage, '\0', sizeof(item.usage));
    EEPROM.put(item.addressUsage, item.usage);
  }
  EEPROM.put(_AddressEEPROMInitState, _isEEPROMInit = 1);
  EEPROM.commit();
}

void eepromGetIoTNodeState()
{
  EEPROM.get(_AddressIoTState, _iotState);
  /* Init Output states from EEPROM to variable (array) */
  for (OutputDevice_t &item : outDevices)
  {
    EEPROM.get(item.addressId, item.id);
    EEPROM.get(item.addressState, item.state);
    EEPROM.get(item.addressUsage, item.usage);
  }
}
