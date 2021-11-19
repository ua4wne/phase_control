#include <Arduino.h>
#include "RTClib.h"
#include <SPI.h>
#include <Ethernet.h>

void setRTC(String dt); // установка даты-времени
//DateTime getRTC(void); // чтение даты-времени
void check_phases(void); // проверка наличия фаз
void buzzer(int8_t step, bool _long); // генерация звуков
void httpRequest(void); // отправка запроса на сервер