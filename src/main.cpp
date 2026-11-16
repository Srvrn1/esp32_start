#include <Arduino.h>
#include <AutoOTA.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <GyverDS18.h>

#define WIFI_SSID "Mega5/4"
#define WIFI_PASS "54400544"

#define WIFI_SSID1 "Dnlvrn"
#define WIFI_PASS1 "2155791975"
uint8_t WIFI_mode = 0;                  //смена точки доступа

//----------------------------------- Настройки MQTT
const char* mqttServer   = "m4.wqtt.ru";
const int   mqttPort     = 9478;
const int   mqttPortTLS  = 9479;
String mqttClientId = "srvrn_proba";
const char* mqttUser = "u_5A3C2X";
const char* mqttPass = "HilZPRjD";

const int   Qos = 1;
//================================================топики
     //публикация
const char* topic_Clock = "demo/time";             //время
const char* topic_temper_incar = "demo/t_incar";   //Температура
const char* topic_handbrake = "demo/hbrake";       //состояние ручника
const char* topic_kppsens = "demo/kpp";            // 1 - нейтральная передача
     //подписки
const char* topic_Clock_set = "demo/time1";        //для установки времени
const char* topic_reset = "demo/reset";            //для перезагрузки
const char* topic_start = "demo/start";            //команда для запуска
//----------------------------------------------------
#define zik 2                                  // Пин зажигание
#define starter 16                             // Стартер
#define otopitel 17                             // печка
#define gabarit 18                             // габариты

#define kpp_sens 19                            //КПП
#define tormoz 21                              // контроль Ручник
#define flash 23                               //прерывание сигнал с катушки
//----------------------------------ПЕРЕМЕННЫЕ
int8_t state_tormoz;                          //состояние ручника
int8_t was_tormoz;                            //в каком сост был ручник
int8_t state_N;                               //состояние нейтральной передачи
int8_t was_N;                                 //в каком сост был нейтраль
int8_t flag_minuta = 1;
uint8_t count_connect;                       // счетчик кол-ва подключений к mqtt
int8_t flag_getTemp = 0;                     //что бы не повторять запросы ds18 несколько раз

//--------------------------------------------------clock
uint8_t hrs=1;
uint8_t minut=1;
uint8_t sec=1;


Ticker timerCounter;
WiFiClient espClient;                       //экземпляр WIFI клиента
PubSubClient client(espClient);             // экземпляр mqtt клиента
GyverDS18Single ds(4);                     //инициализация датчика DS18B20 на пине D4, с паразитным питанием
AutoOTA ota("0.3", "Srvrn1/esp32_start");  //======================

void WiFi_connect(){
  volatile int8_t n = 2;
  while (n){                   //пока n,будем выполнять попытки
    n--;
    String SSID_now;
    String Pass_now;

    if(!WIFI_mode){
      SSID_now = WIFI_SSID;
      Pass_now = WIFI_PASS;
    }
    else{
      SSID_now = WIFI_SSID1;
      Pass_now = WIFI_PASS1;
    }

    int8_t i=20;
    Serial.print("Подключение к WiFi...");
    Serial.println(SSID_now);

    WiFi.begin(SSID_now, Pass_now);

    while (WiFi.status() != WL_CONNECTED && i) {
        i--;
      delay(500);
      Serial.print(".");
    }
    if(WiFi.status() != WL_CONNECTED) {
      WIFI_mode = !WIFI_mode;
      Serial.println();
      Serial.println("WiFi Ошибка");
    }
    else{
      n=0;                                  // если подключились попытки не нужны
      Serial.println();
      Serial.println("WiFi подключен!");
      Serial.print("IP адрес: ");
      Serial.println(WiFi.localIP());
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {   // Функция щбработки сообщения MQTT
  Serial.print("Сообщение в топике [");
  Serial.print(topic);
  Serial.print("]: ");
  
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if(strcmp(topic, topic_Clock_set) == 0){                        // Установка времени
    String data = "";
    data += (char)payload[0];
    data += (char)payload[1];
    hrs = data.toInt();
    data="";
    data += (char)payload[3];
    data += (char)payload[4];
    minut =data.toInt();
    data ="";
    data += (char)payload[6];
    data += (char)payload[7];
    sec = data.toInt();

    flag_minuta = 1;    //сразу публикуем
  }

  else if(strcmp(topic, topic_reset) == 0){                      //перезагрузка
    ESP.restart();
  }
 
}

void timer1() {
  //---------------------
  sec++;             //ход часов
  Serial.println(sec);
  if(sec == 60) {
    flag_minuta = 1;
    sec = 0;
    minut++;
    if(minut == 60){
      minut = 0;
      hrs++;
      if(hrs == 24)hrs = 0;
    }
  }
}

void reconnect_MQTT() {                                           // Функция переподключения к MQTT брокеру
  
  // Повторяем попытки до успешного подключения
  while (!client.connected()) {
    Serial.print("Попытка подключения к MQTT брокеру...");
    client.setServer(mqttServer, mqttPort);
    client.setCallback(callback);
 
    client.connect(mqttClientId.c_str(), mqttUser, mqttPass);

      // ======================================После подключения можно подписаться на топики (если нужно)
      client.subscribe(topic_reset, Qos);
      client.subscribe(topic_Clock_set, Qos);
      client.subscribe(topic_start, Qos);


     if(client.connected()){
      Serial.println("подключено" );
      //mode_mqtt = 1;                                       //режим mqtt
      //flag_minuta = 1;                                     //сразу отправляем publish()
     }
    else {
      Serial.print("ошибка, rc=");
      Serial.print(client.state());
      Serial.println(" пробуем снова через 5 секунд");
      count_connect++;
      delay(5000);
    }
    if(count_connect == 5){
        count_connect = 0;         
        Serial.println("попытки подключения исчерпаны");
        break;
    }
  }
}

void publish(){
  flag_minuta = 0;
  char msg[10];                                     //буфер для сообщения
  //=====================отправка времени
  if(minut>9){                                       //отправка времени
    snprintf(msg, sizeof(msg), "%d:%d", hrs, minut);
  }
  else{
    snprintf(msg, sizeof(msg), "%d:0%d", hrs, minut);
  }

  if (client.publish(topic_Clock, msg)) {            //публикуем время
    Serial.print("Опубликовано: ");
    Serial.print(msg);
    Serial.print(" в топик ");
    Serial.println(topic_Clock);
  } else {
    Serial.println("Ошибка публикации");
  }
  //--------------------отправка температуры
  float temp_incar;                         //темпер в салоне
  flag_getTemp = 0;                         
  ds.readTemp();                           //read ds18
    
  temp_incar=ds.getTemp();                

  snprintf(msg, sizeof(msg), "%.2f", temp_incar);       //подготавливаем строку обрезаем 2 знака после запятой
  if (client.publish(topic_temper_incar, msg)) {            //публикуем
    Serial.print("Опубликовано: ");
    Serial.print(msg);
    Serial.print(" в топик ");
    Serial.println(topic_temper_incar);
  } else {
    Serial.println("Ошибка публикации");
  }
}

void pinsMode(){
  pinMode(zik, OUTPUT);
  pinMode(starter, OUTPUT);
  pinMode(otopitel, OUTPUT);
  pinMode(gabarit , OUTPUT);

  digitalWrite(zik, LOW);          
  digitalWrite(starter, LOW);
  digitalWrite(otopitel, LOW);
  digitalWrite(gabarit, LOW);
  
  pinMode(kpp_sens, INPUT_PULLUP);
  pinMode(tormoz, INPUT_PULLUP);
  pinMode(flash, INPUT_PULLUP);
}

void sensor_read(){
  state_tormoz = digitalRead(tormoz);  //======читаем датчик тормоза
  if(state_tormoz != was_tormoz){            //если тормоз изменился
    was_tormoz = state_tormoz;
   client.publish(topic_handbrake, state_tormoz ? "1" : "0"); //отправка сообщения
   Serial.println("тормоз изменился");
  }
  state_N = digitalRead(kpp_sens);  //======читаем датчик нейтральной передачи
  if(state_N != was_N){            //если нейтральная передача изменился
    was_N = state_N;
   client.publish(topic_kppsens, state_N ? "1" : "0"); //отправка сообщения
    Serial.println("нейтральная передача изменилась");
  }
}

void setup() {
  pinsMode();
  Serial.begin(115200);
  Serial.println();
  WiFi_connect();
  reconnect_MQTT();

  Serial.println("текущая версия:  " + ota.version());
  Serial.println("Проверка обновлений...");                  //проверка обновлений
  String ver, notes;
  if (ota.checkUpdate(&ver, &notes)) {
    Serial.println("Обновление доступно!");
    Serial.print("Версия: ");
    Serial.println(ver);
    Serial.print("Описание: ");
    Serial.println(notes);
    ota.updateNow();
  }
  else {
    Serial.println("Обновлений нет!");
  }
  
  ds.setResolution(10);              //Установка разрешениe датчика ds18
  sensor_read();
  timerCounter.attach(1.0, timer1);  // Запуск прерывания по таймеру 1 раз в секунду
}

void loop() {
  if (!client.connected()) reconnect_MQTT();    //если соединение разорвано
  client.loop();
  sensor_read();
  if(flag_minuta) publish();                 //отправка сообщения
  if(sec > 55 && !flag_getTemp){             //Запрос температуры
    flag_getTemp = 1;
    Serial.println("Запрос температуры");
    if (!ds.requestTemp()) {
      Serial.println("request error");
    }
  }


  if(WiFi.status() != WL_CONNECTED) WiFi_connect();  //WIFI проверяем палюбому
}
