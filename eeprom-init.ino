void eepromInitialize()
{
  size_t eepromSize = eepromCalcAddresses();
  EEPROM.begin(eepromSize);
  eepromInitstateInfo();
  eepromInitIotDeviceId();
  /* In case .put() was called */
  EEPROM.commit();
}

size_t eepromCalcAddresses()
{
  iotDeviceIdAddres = 0;
  size_t size = sizeof(iotDeviceId);
  size_t outStateValueSize = sizeof(Output_t::state);
  size_t outActiveValueSize = sizeof(Output_t::active);
  for (Output_t &item : outDevices)
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
  for (Output_t &item : outDevices)
  {
    EEPROM.get(item.addressState, item.state);
    if (isnan(item.state))
    { /* init to get rid of 0xFF */
      item.state = 0.0f;
      EEPROM.put(item.addressState, item.state);
    }
    EEPROM.get(item.addressActive, item.active);
    if (((byte)item.active) == 255)
    { /* init to get rid of 0xFF */
      item.active = false;
      EEPROM.put(item.addressActive, item.active);
    }
  }
}

void eepromInitIotDeviceId()
{
  EEPROM.get(iotDeviceIdAddres, iotDeviceId);
  for (byte b : iotDeviceId)
  {
    if (b == 255)
    { /* init to get rid of any 0xFF */
      memset(iotDeviceId, 0, sizeof(iotDeviceId));
      EEPROM.put(iotDeviceIdAddres, iotDeviceId);
      break;
    }
  }
}
