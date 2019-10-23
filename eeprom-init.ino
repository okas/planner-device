void eepromInitialize()
{
  size_t eepromSize = eepromCalcAddresses();
  EEPROM.begin(eepromSize);
  eepromInitIoTStateInfo();
  eepromInitOutputsInfo();
  /* In case .put() was called */
  EEPROM.commit();
}

size_t eepromCalcAddresses()
{
  size_t size = 0; // This statement's value is the very start address of the EEPROM allocation.
  eepromCalcIoTStateAddresses(&size);
  eepromCalcOutputsAddresses(&size);
  return size;
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

void eepromInitIoTStateInfo()
{
  EEPROM.get(_AddressIoTState, _iotState);
  if ((byte)_iotState == 0xFF)
  { /* init to get rid of 0xFF */
    _iotState = IOTState_t::started;
    EEPROM.put(_AddressIoTState, _iotState);
  }
}

void eepromInitOutputsInfo()
{
  /* Init Output states from EEPROM to variable (array) */
  for (OutputDevice_t &item : outDevices)
  {
    EEPROM.get(item.addressId, item.id);
    if (isnan(item.id))
    { /* init to get rid of 0xFF */
      item.id = 0L;
      EEPROM.put(item.addressId, item.id);
    }
    //
    EEPROM.get(item.addressState, item.state);
    if (isnan(item.state))
    { /* init to get rid of 0xFF */
      item.state = 0.0F;
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
