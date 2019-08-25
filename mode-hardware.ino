/* Initialization button -- */

const byte LED_PIN = 2;
const byte FLASH_PIN = 0;
const time_t init_btn_delay = 5;        // Seconds
const uint button_debounce_delay = 200; // Milliseconds
volatile bool init_btn_timer_reached;
volatile unsigned long button_last_micros;
volatile int init_btn_last_state;
Ticker led_blinker_ticker;
Ticker init_btn_ticker;
void ICACHE_RAM_ATTR init_button_change();

/* -- Initialization button */

void setupInitButton()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(FLASH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLASH_PIN), init_button_change, CHANGE);
}

void ICACHE_RAM_ATTR init_button_change()
{
  int temp_last_state = digitalRead(FLASH_PIN);
  if ((long)(micros() - button_last_micros) < button_debounce_delay * 1000 &&
      init_btn_last_state == temp_last_state)
  {
    return;
  }
  button_last_micros = micros();
  init_btn_last_state = temp_last_state;
  if (init_btn_last_state)
  {
    init_button_up();
  }
  else
  {
    init_button_down();
  }
}

void init_button_down()
{
  time_t timer = time(0);
  Serial.printf(" - - TIME: %d; TARGET: %d.\n", timer, timer + init_btn_delay);
  init_btn_ticker.attach(init_btn_delay, []() {
    init_btn_timer_reached = true;
    Serial.println(" - - TIMER reached!");
  });
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED_BUILTIN ON");
}

void init_button_up()
{
  init_btn_ticker.detach();
  if (!led_blinker_ticker.active())
  {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED_BUILTIN OFF");
  }
}

void iot_start_init_loop()
{
  if (init_btn_timer_reached)
  {
    init_btn_timer_reached = false;
    startLEDBlinker();
    gotoIotInitMode(false);
  }
}

void startLEDBlinker()
{
  led_blinker_ticker.attach(0.5, []() {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  });
}
