#include <Arduino.h>
#include <BleConsole.h>

static bool cmd(int argc, char **argv)
{
  if (!strcmp(argv[0], "led") && argc == 2) {
    digitalWrite(LED_BUILTIN, strcmp(argv[1], "on") ? LOW : HIGH);
    BleConsole.print("ok\r\n");
    return true;
  }
  if (!strcmp(argv[0], "btn")) {
    BleConsole.printf("bouton=%d\r\n", digitalRead(PIN_BUTTON1) == HIGH);
    return true;
  }
  return false;
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  BleConsole.begin("T1000A-Arduino", "blink-0.1");
  BleConsole.onCommand(cmd);
}

void loop()
{
  static unsigned long last;
  BleConsole.poll();
  if (millis() - last >= 2000) {         /* flash de 50 ms toutes les 2 s */
    last = millis();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    Serial.printf("t=%lu ms\r\n", last);
  }
}
