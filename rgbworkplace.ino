// frimware Optiboot v6.2 16MHz

// Enable debug prints to serial monitor
#define MY_DEBUG
#define MY_RF24_CE_PIN 8
#define MY_NODE_ID 70
// Enable and select radio type attached
#define MY_RADIO_NRF24
//#define MY_RADIO_RFM69

#define MY_RF24_PA_LEVEL RF24_PA_HIGH

#include <MySensors.h>
#include <elapsedMillis.h>

elapsedMillis timeDHT; // Таймаут опроса датчика DHT22
#define TIMEOUT 600000 // Раз в 10 мин

#include "DHT.h"
#define DHTPIN 7     // what digital pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

struct typeRGB  
{ 
  uint8_t Pin;                // № ножки цвета
  int16_t Fq_color = 0;       // текущая частота ШИМ
  unsigned long StTime = 0;   // стартовое время
  boolean Busy = false;       // переходный процесс еще идет
  int16_t StartValue = 0;     // начальная частота ШИМ
};
typeRGB RGB[4];
  
float R; // equation for dimming curve non linear fading, idea from https://diarmuid.ie/blog/pwm-exponential-led-fading-on-arduino-or-other-platforms/
// массив для записи значений RGB после парсинга (цвет, ШИМ, период, минимум, максимум, кол-во повторов волны)
uint16_t RGB_value[3][6] = {{0,0,1000,0,0,0},{0,0,5000,0,0,0},{0,0,5000,0,0,0}};
boolean up_volna[3];

#define SENSOR1 1      // id для RGBW ленты
#define SENSOR2 2      // id для диммера ленты
#define SENSOR3 3      // id для датчика температуры
#define SENSOR4 4      // id для датчика влажности

MyMessage Sens11RGBWmsg(SENSOR1, V_RGBW);
MyMessage Sens21ONOFFmsg(SENSOR1, V_STATUS);
MyMessage Sens22PERCENTmsg(SENSOR1, V_PERCENTAGE);
MyMessage Sens31TEMPmsg(SENSOR3, V_TEMP);
MyMessage Sens41HUMmsg(SENSOR4, V_HUM);

//-------------------------- функции -----------------------------------------
boolean SHIM_High (uint8_t index, uint16_t Period, int16_t Fq) // функция розжига светодиода определенного цвета
// index - цвет: 0 - красный, 1 - зеленый, 2 - синий, Period - время переходного процесса (мс), Fq - конечное значение ШИМ
{  
//  static typeRGB RGB[3];
  int16_t Value;
  int16_t dT;  
  boolean Finish;         // флаг завершения 
  int16_t delta = 1;  
                      
  Finish = false;
/*  
  if ((Fq<0)||(Fq>=255))
  {
//    Serial.println("Kosyak!");
    Finish = true;
    return Finish;
  }
*/  
  
  Value = RGB[index].Fq_color;
  if (RGB[index].Busy == 0){                // проверяем первый запуск или нет
      RGB[index].Busy = 1;
      RGB[index].StTime = millis();
      RGB[index].StartValue = Value;
  }

  dT = 0;
  if ((RGB[index].StartValue - Fq) != 0){                    // проверка деление на 0
      dT = Period/abs(RGB[index].StartValue - Fq);         // вычисляем интервал времени для каждой итерации
/*      Serial.print("Period = ");Serial.print(Period);
    Serial.print("; StartValue = ");Serial.print(RGB[index].StartValue); 
    Serial.print("; Fq = ");Serial.print(Fq); 
    Serial.print("; abs = ");Serial.print(abs(RGB[index].StartValue - Fq));  
*/
  } 
  if (dT < 5) delta = 5;
  if ((RGB[index].StTime + dT*abs(RGB[index].StartValue-Value)) <= millis()) {
      if (RGB[index].StartValue < Fq) Value = Value + delta; // увеличиваем значение ШИМ, если прошли расчетный интервал времени
        else Value = Value - delta;
  }
  if (Value < 0) Value = 0;
  Serial.print("; Color = ");Serial.print(index);  
  Serial.print("; V = ");Serial.println(Value);
  Serial.print("; Fq= ");Serial.print(Fq);
  Serial.print("; dT= ");Serial.print(dT);
  Serial.print("; Time= ");Serial.print(dT*abs(RGB[index].StartValue-Value));
  Serial.print(" <= ");Serial.println(millis());

  if (((RGB[index].StartValue < Fq)&&(Value >= Fq))||((RGB[index].StartValue >= Fq)&&(Value <= Fq))) {  // Достигли заданной величины
      if ((index == 1)||(index == 3)) analogWrite(RGB[index].Pin, pow (2, ((255 - Fq) / R)) - 1);  //инвертируем для зеленого цвета (драйвер IR4428)
        else analogWrite(RGB[index].Pin, pow (2, (Fq / R)) - 1);
      Finish = true;
      Value = Fq;
  } else 
    // пишем расчетное значение в ШИМ 
    if ((index == 1)||(index == 3)) analogWrite(RGB[index].Pin, pow (2, ((255 - Value) / R)) - 1); //инвертируем для зеленого цвета (драйвер IR4428)
      else analogWrite(RGB[index].Pin, pow (2, (Value / R)) - 1); 

  // сохраняем текущее значение ШИМ для вызванного цвета
  RGB[index].Fq_color = Value;
  if (Finish) RGB[index].Busy = 0;  // сбрасывает статус В работе, если закончили
  
  return Finish;
}

void setup()
{
  RGB[0].Pin = 3;        // № пина для красного цвета
  RGB[1].Pin = 5;        // № пина для зеленого цвета (инвертированный)
  RGB[2].Pin = 6;        // № пина для синего цвета
  RGB[3].Pin = 9;        // № пина для белого цвета
  
  for (uint8_t i=0; i < 4; i++) pinMode(RGB[i].Pin, OUTPUT);
  analogWrite(RGB[1].Pin, 255); //гасим зеленый цвет (т.к. инвертирован вход)
  analogWrite(RGB[3].Pin, 255); //гасим белый цвет (т.к. инвертирован вход)
  
  // set up dimming
  R = (255 * log10(2))/(log10(255));

  dht.begin();
}

void presentation()
{
	//Send the sensor node sketch version information to the gateway
	sendSketchInfo("BedRoom RGB", "1.2");
  present(SENSOR1, S_RGBW_LIGHT, "RGBW Led");
  present(SENSOR2, S_DIMMER, "RGBW Led Dimmer");
  present(SENSOR3, S_TEMP, "Temperature");
  present(SENSOR4, S_HUM, "Humidity");
  wait(30);
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  wait(30);
  //request(SENSOR1, V_RGBW); // Запрашиваем цвета от сервера
}

void loop()
{
// радуга (красный - оранжевый - желтый - зеленый - голубой - синий - фиолетовый)
// raduga_param[4] - начальный цвет, кол-во цветов в цепочке, период, кол-во повторов
static uint16_t raduga_param[4];// = {0,6,1000,5};
const uint8_t raduga[7][3] = {{255,0,0},{255,20,0},{255,120,0},{0,255,0},{0,255,200},{0,0,255},{200,0,255}};
static uint8_t n_cvet;
static boolean RGB_ok[3];

if ((raduga_param[1]>0)&&(raduga_param[3]>0))      // получены настройки для радуги
{
  for (uint8_t i=0; i < 3; i++)
  {
    uint8_t cvet = (n_cvet + raduga_param[0])%7;  // вычисляем текущий цвет (работает в том числе по кругу с переходом через 0)
    boolean OK = SHIM_High(i, raduga_param[2], raduga[cvet][i]);
    if (OK) {
      RGB_ok[i]=true;
    }
  }
  if ((RGB_ok[0])&&(RGB_ok[1])&&(RGB_ok[2]))      // текущий цвет достигнут
  {
    n_cvet++;
    RGB_ok[0]=false;
    RGB_ok[1]=false;
    RGB_ok[2]=false;
  }
  if (n_cvet>=raduga_param[1]) {            // переходим к следующему цвету радуги
    n_cvet = 0;
    raduga_param[3]--;
  }
}
// ---------- RGB. Вызов функции ----------------------------
// массив для записи значений RGB после парсинга (цвет, ШИМ, период, минимум, максимум, кол-во повторов волны)
//static uint16_t RGB_value[3][6] = {{1,0,10000,0,255,2},{1,0,5000,0,0,0},{0,0,5000,0,0,0}};
//static boolean up_volna[3];

for (uint8_t i=0; i < 3; i++)
{
  // Волна
  if ((up_volna[i])&&(RGB_value[i][5] != 0)&&(RGB_value[i][0]>0)){            // вторая полуволна
    boolean volna = SHIM_High(i, RGB_value[i][2], RGB_value[i][3]);
    if (volna) {
      up_volna[i] = false;
      RGB_value[i][5]--;
      if (RGB_value[i][5]==0) {
        RGB_value[i][0] = 0;
      }
    }
  }
  
  if ((!up_volna[i])&&(RGB_value[i][5] != 0)&&(RGB_value[i][0]>0)){           // первая полуволна
    boolean volna = SHIM_High(i, RGB_value[i][2], RGB_value[i][4]);
    if (volna) {
      up_volna[i] = true;
    }
  }
  
  // Розжиг
  if ((RGB_value[i][0])&&(RGB_value[i][5]==0))    // предполагаем, что задан режим обычного розжига до нужного значения (или затухания)
  {
    boolean OK = SHIM_High(i, RGB_value[i][2], RGB_value[i][1]);
    if (OK) {
      RGB_value[i][0] = 0;
      //Serial.println("Rozjig");
    }
  } 
}
  if (timeDHT > TIMEOUT){ //читаем температуру и влажность
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    send(Sens31TEMPmsg.set(t, 2), false);
    wait(30);
    send(Sens41HUMmsg.set(h, 2), false);
    Serial.println("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.println("Temperature: ");
    Serial.print(t);
    Serial.print(" *C ");
    timeDHT = 0;
  }
  delay(1);       // пауза в 1 миллисекунду, чтобы цикл всегда был не менее, чем 1 мс
}

//RGB_value[3][6] = {{0,0,1000,0,255,0},{0,0,5000,0,0,0},{0,0,5000,0,0,0}};
//SHIM_High (uint8_t index, uint16_t Period, int16_t Fq)
void receive(const MyMessage &message) {
  if (message.type==V_RGBW) {
     // Change relay state
     String hexstring = message.getString();
     unsigned long value = strtoul( &hexstring[0], NULL, 16);
     Serial.print("value = ");Serial.println(value, HEX);
     uint8_t redval = (value >> 24) & 0xFF;
     uint8_t greenval = (value >> 16) & 0xFF;
     uint8_t blueval = (value >> 8) & 0xFF;
     uint8_t whiteval = value & 0xFF;
     Serial.print("Red = ");Serial.println(redval, HEX);
     Serial.print("Green = ");Serial.println(greenval, HEX);
     Serial.print("Blue = ");Serial.println(blueval, HEX);
     Serial.print("White = ");Serial.println(whiteval, HEX);
     //SHIM_High (0, 1, redval);
     analogWrite(RGB[0].Pin, pow (2, (redval / R)) - 1);
     if (greenval == 0) analogWrite(RGB[1].Pin, 255);
      else analogWrite(RGB[1].Pin, pow (2, ((255 - greenval) / R)) - 1);
     analogWrite(RGB[2].Pin, pow (2, (blueval / R)) - 1);
     if (whiteval == 0) analogWrite(RGB[3].Pin, 255);
      else analogWrite(RGB[3].Pin, pow (2, ((255 - whiteval) / R)) - 1);
     //SHIM_High (1, 1, greenval);
     //SHIM_High (2, 1, blueval);
     //SHIM_High (3, 1, whiteval);
     
     bool test = send(Sens11RGBWmsg.set(message.getString()), false);
     // Write some debug info
     #ifdef MY_DEBUG
        Serial.print("Send ACK: ");
        Serial.println(test);
        Serial.print("Incoming change for sensor:");
        Serial.print(message.sensor);
        Serial.print(", New status: ");
        Serial.println(message.getString());
     #endif
  }
  /*
  if (message.type==V_STATUS) {
     // Change relay state
     LED_switch = message.getBool()?true:false;
     bool test = send(Sens4msg.set(LED_switch), false);
     // Write some debug info
     #ifdef MY_DEBUG
        Serial.print("Send ACK: ");
        Serial.println(test);
        Serial.print("Incoming change for sensor:");
        Serial.print(message.sensor);
        Serial.print(", New status: ");
        Serial.println(message.getBool());
     #endif
  }
  if (message.type==V_PERCENTAGE) {
     // Change relay state
     LED_switch = message.getBool()?true:false;
     bool test = send(Sens4msg.set(LED_switch), false);
     // Write some debug info
     #ifdef MY_DEBUG
        Serial.print("Send ACK: ");
        Serial.println(test);
        Serial.print("Incoming change for sensor:");
        Serial.print(message.sensor);
        Serial.print(", New status: ");
        Serial.println(message.getBool());
     #endif
  } */
}
