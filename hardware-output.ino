void changeOutputStates()
{
  for (OutputDevice_t &device : outDevices)
  {
    if (strlen(device.usage) > 0)
    {
      pinMode(device.pin, OUTPUT);
      analogWrite(device.pin, round(device.state * 1024));
    }
    else
    {
      /* Defaulting pin state */
      pinMode(device.pin, INPUT);
      analogWrite(device.pin, 0);
    }
  }
}
