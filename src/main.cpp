#include "main.h"
#include <Wire.h>
#include <avr/pgmspace.h>

#define DEBUG_COM

//назначение пинов Arduino Mega Pro 2560
const uint8_t L1 = 3; //контроль фазы №1
const uint8_t L2 = 5; //контроль фазы №2
const uint8_t L3 = 7; //контроль фазы №3
const uint8_t L4 = 32; //контроль фазы №4
const uint8_t L5 = 33; //контроль фазы №5
const uint8_t L6 = 34; //контроль фазы №6
const uint8_t L7 = 35; //контроль фазы №7
const uint8_t L8 = 36; //контроль фазы №8
const uint8_t L9 = 37; //контроль фазы №9
const uint8_t thermo = 38; //включение контактора с термостата
const uint8_t buzz = 23; //пищалка
const uint8_t CS = 11; //CS pin
uint8_t h_curr = 0; //текущий час
uint8_t h_alarm = 23; //время синхронизации часов

const uint32_t speed = 115200;

bool l1_fail = false; //флаг аварии системы L1
bool l2_fail = false; //флаг аварии системы L2
bool l3_fail = false; //флаг аварии системы L3
bool thermo_on = false; // флаг отключения термостата
bool good_rtc = false; //флаг статуса работы модуля, true если все ОК
bool good_lan = false; //флаг статуса работы модуля LAN, true если все ОК
bool send_fail = false; // флаг отправки сообщения об аварии
bool time_sync = true; // флаг разрешения на синхронизацию часов модуля при первом включении принудительно синхронизируем

char server[] = "ms-poll.ru";    // name address for web-server (using DNS)
String uid = "7933965295ea79ec"; //уникальный ID устройства
enum t_state {NORMAL, FAIL} state;

byte mac[] = {
  0x52, 0x41, 0x49, 0xAA, 0xDE, 0x02
};

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192, 168, 0, 254);
IPAddress myDns(192, 168, 0, 1);

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;
RTC_DS3231 rtc;

void setup() {
  //настройка входов-выходов
  pinMode(L1, INPUT);
  pinMode(L2, INPUT);
  pinMode(L3, INPUT);
  pinMode(L4, INPUT);
  pinMode(L5, INPUT);
  pinMode(L6, INPUT);
  pinMode(L7, INPUT);
  pinMode(L8, INPUT);
  pinMode(L9, INPUT);
  pinMode(thermo, INPUT);
  pinMode(buzz, OUTPUT);
  
  pinMode(53, OUTPUT); // иначе не будет работать SPI на меге!!!
  Serial.begin(speed); // Скорость обмена данными с компьютером
  //инициализируем модуль RTC
  if(rtc.begin()) {
    good_rtc = true;
    #ifdef DEBUG_COM
    Serial.println("Модуль RTC найден!");
    #endif
    rtc.disable32K();    
  }
  else{
    good_rtc = false;
    buzzer(3,true);
    #ifdef DEBUG_COM
    Serial.println("Модуль RTC не найден!");
    #endif
  }
  // You can use Ethernet.init(pin) to configure the CS pin
  Ethernet.init(CS);  // 10 for most Arduino shields
   // start the Ethernet connection:
  #ifdef DEBUG_COM
  Serial.println("Initialize Ethernet with DHCP:");
  #endif
  if (Ethernet.begin(mac) == 0) {
    #ifdef DEBUG_COM
    Serial.println("Failed to configure Ethernet using DHCP");
    #endif
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      #ifdef DEBUG_COM
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      #endif
      good_lan = false;
      buzzer(4,true);
    } else if (Ethernet.linkStatus() == LinkOFF) {
      #ifdef DEBUG_COM
      Serial.println("Ethernet cable is not connected.");
      #endif
      good_lan = false;
      buzzer(2,true);
    }
    
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, myDns);
    #ifdef DEBUG_COM
    // print your local IP address:
    Serial.print("My IP address: ");
    Serial.println(Ethernet.localIP());
    #endif
  }
  //  Если соединение с динамической адресацией было установлено, то
  else{
    #ifdef DEBUG_COM
    //  Выводим в монитор порта соответствующее сообщение об этом и выводим назначенный устройству IP-адрес
    Serial.print("  DHCP assigned IP ");
    Serial.println(Ethernet.localIP());
    #endif
    good_lan = true;
  }
}

void loop() {
  if(good_lan){
    // if there are incoming bytes available
    // from the server, read them and print them:
    int len = client.available();
    if (len > 0) {
      byte buffer[1024];
      if (len > 1024) len = 1024;
      client.read(buffer, len);
      #ifdef DEBUG_COM
        Serial.write(buffer, len); // show in the serial monitor (slows some boards)
      #endif
      //берем из буфера дату-время и синхронизируем часы
      String str((char*) buffer);
      String dt = str.substring(len - 19);
      #ifdef DEBUG_COM
      Serial.println("\nDate-time from site");
      Serial.println(dt);
      #endif
      if(time_sync){
        setRTC(dt);
        time_sync = false;
      }
    }
    // контроль наличия фаз
    check_phases();
    if(state == FAIL && thermo_on && !send_fail){
      // обработка аварийной ситуации
      send_fail = true;
      //пора отправлять аварийный запрос на сервер немедленно
      httpRequest();
      buzzer(5,false);
    }
    if(millis() % 60000 == 0){ // если прошла 1 минута
      DateTime now = rtc.now(); // читаем время с модуля
      if(now.hour() != h_curr) {
        h_curr = now.hour();
        //пора отправлять запрос на сервер, запрос отправляем раз в час
        httpRequest();
        if(state == FAIL && thermo_on){
          buzzer(5,false);
        }
        if(now.hour() == h_alarm) {
          time_sync = true;
        }
      }
    }
    if(state == NORMAL){
      send_fail = false;
    }
  }
  else{
    //пищим периодически 5 пиков каждые 5 секунд
    buzzer(5,false);
  }  
}

void check_phases(){
  thermo_on = !digitalRead(thermo); // HIGH отключено or LOW включено
  
  if((digitalRead(L1) == HIGH) || ( digitalRead(L2) == HIGH) || ( digitalRead(L3) == HIGH) ){
    l1_fail = true;
  }
  else{
    l1_fail = false;
  }
  if((digitalRead(L4) == HIGH) || ( digitalRead(L5) == HIGH) || ( digitalRead(L6) == HIGH) ){
    l2_fail = true;
  }
  else{
    l2_fail = false;
  }
  if((digitalRead(L7) == HIGH) || ( digitalRead(L8) == HIGH) || ( digitalRead(L9) == HIGH) ){
    l3_fail = true;
  }
  else{
    l3_fail = false;
  }
  if(l1_fail || l2_fail || l3_fail)
    state = FAIL; //авария фаз
  else
    state = NORMAL; //все в порядке
}

//генерация звука
void buzzer(int8_t step = 1, bool _long = true){
  int _time = 500;
  if(_long){
    _time = 1000;
  }
  while(step){
    digitalWrite(buzz, 1); //включаем пьезоизлучатель на 1с
    delay(_time); // на 1000 мс
    digitalWrite(buzz, 0); //выключаем пьезоизлучатель на 1с
    delay(_time);
    --step;
  }
}

void setRTC(String dt){
  //парсим строку даты-времени 2021-11-25 08:53:44
  if(dt.length() > 17){
    dt.trim();
    String tmp = dt.substring(0,4);
    uint8_t year = tmp.toInt();
    tmp = dt.substring(5,7);
    uint8_t month = tmp.toInt();
    tmp = dt.substring(8,10);
    uint8_t day = tmp.toInt();
    tmp = dt.substring(dt.indexOf(" ")+1, dt.indexOf(":"));
    uint8_t hour = tmp.toInt();
    tmp = dt.substring(dt.indexOf(":")+1, dt.lastIndexOf(":"));
    uint8_t minute = tmp.toInt();
    tmp = dt.substring(dt.lastIndexOf(":")+1, dt.lastIndexOf(":")+3);
    uint8_t sec = tmp.toInt();
    #ifdef DEBUG_COM
    Serial.println("Set date-time from site");
    Serial.println(dt.substring(0,4) + "-" + dt.substring(5,7) + "-" + dt.substring(8,10) + " " + dt.substring(dt.indexOf(" ")+1, dt.indexOf(":")) + ":" + dt.substring(dt.indexOf(":")+1, dt.lastIndexOf(":"))
    + ":" + sec);
    #endif
    rtc.adjust(DateTime(year, month, day, hour, minute, sec));
  }
}

// this method makes a HTTP connection to the server:
void httpRequest() {
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  client.stop();
  //параметры для отправки данных на сайт
  String params = "/data?device=" + uid;
  if(l1_fail)
    params += "&l1=1";
  else
    params += "&l1=0";
  if(l2_fail)
    params += "&l2=1";
  else
    params += "&l2=0";
  if(l3_fail)
    params += "&l3=1";
  else
    params += "&l3=0";
  if(thermo_on)
    params += "&thermo=1";
  else
    params += "&thermo=0";
  // if there's a successful connection:
  if (client.connect(server, 80)) {
    #ifdef DEBUG_COM
    Serial.println("connecting...");
    #endif
    // send the HTTP GET request:
    client.print( "GET ");
    client.print(params);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();
  }
  #ifdef DEBUG_COM
  else {  
    // if you couldn't make a connection:
    Serial.println("connection failed");
  }
  #endif
}