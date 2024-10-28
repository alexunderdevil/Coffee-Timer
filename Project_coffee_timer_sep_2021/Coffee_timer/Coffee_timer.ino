/*
   начат 8 сентября 2021
   10 сентября 2021
      - вывод времени включения
      - мигание параметров
      - автоматический выход на главный экран (если не нажимать кнопку)
      - сработка реле по установленному времени
      - вывод времени активного реле (только после включения по времени)
        при клике - отключение реле и остановка времени работы для инфо
      - экран ручного включения/выключения реле
      - при включении вручную нельзя запустить таймер
      - если таймер запущен и включить вручную реле, то в нужное время запустится счетчик
   11 сентября 2021
      - анимация запуска таймера миганием (точки тоже мигают)
   16 сентября 2021
      - при достижении 60 минут в режиме включенного реле - автовыклюение
        (защита от перегрева) и переход на начальный экран
      - добавлен переход в "спящий режим" с показом реальных часов и
        понижением яркости, для выхода нажать или повращать энкодер
    3 октября 2021
      - при выходе из сна отображается последний режим (установка таймера или ручное управление)
    10 сентября 2022
      - было отключено питание больше 10 дней, сбилось время, установка
    6 ноября 2022
      - добавлена установка времени при удержании энкодера и подачи питания, для установки - удержать
*/

// ================== НАСТРОЙКИ ==================
#define DEBUG 0
#define ENC_A 3
#define ENC_B 4
#define ENC_KEY 2
#define DISP_CLK 6
#define DISP_DIO 7
#define RELAY_PIN 8
#define EB_HOLD 1000                    //  таймаут удержания кнопки, мс
#define TIME_TO_SLEEP 30000             //  таймаут перехода в сон, мс

// ================ БИБЛИОТЕКИ ================
// библиотека энкодера
#include <EncButton.h>
EncButton<EB_TICK, ENC_A, ENC_B, ENC_KEY> enc;
// библиотека дисплея
#include <GyverTM1637.h>
GyverTM1637 disp(DISP_CLK, DISP_DIO);

#include "GyverTimer.h"
GTimer blink_timeout(MS);               //  таймер мигания часов
GTimer no_blink(MS);                    //  таймер выхода из мигания при бездействии
GTimer sleep_timer(MS);                 //  таймер ухода в сон с показом реального времени

#include <EEPROM.h>
//#include <RTClib.h>
//RTC_DS3231 clock;

#include <Wire.h>
#include <DS3231.h>
DS3231 clock;
RTCDateTime dt;

// ================ ПЕРЕМЕННЫЕ ================
int8_t h, m, s, d, mes;              //  с часов в переменные
int y;
int8_t prev_s;                          //  предыдущее значение секунды с модуля
bool blink_clock;                       //  мигание установки времени
bool state_relay = HIGH;                //  состояние реле
bool switch_relay = false;              //  флаг переключения реле
unsigned long counter_s;                //  счетчик секунд работы реле
int8_t min;                             //  для счетчика включенного реле
int8_t time_on_h;                       //  час вкл - в отличие от byte имеет значения -128 + 127
int8_t time_on_m;                       //  мин вкл
byte on[4] = {_o, _n, _empty, _empty};  //  состояние реле для ручного режима
byte off[4] = {_O, _f, _f, _empty};     //
int8_t fade_timer_default = 2;          //  кол-во миганий (анимация) при запуске таймера
int8_t fade_timer = fade_timer_default;
bool sleep_flag;                        //  флаг включения экрана сна по таймеру
byte sleep_bright = 3;                  //  яркость в режиме сна
byte bright = 7;                        //  рабочая яркость

bool state;                             //  статус (0 ожидание, 1 таймер запущен)
byte prev_mode;                         //  для выхода из сна в предыдущий режим, а не в главное окно установки времени
byte mode;                              //  режимы экрана: 0 время, 1 уст. часы, 2 уст. минуты
                                        //                 3 ручное реле
                                        //                 4 счетчик с момента включения реле по таймеру
                                        //                10 экран сна (показывает время)
byte level = 0;                         //  режимы экрана установки часов: 1 часы, 2 минуты, 3 день, 4 месяц, 5 год

void setup()
{
#if ( DEBUG == 1 )
  Serial.begin(115200);
#endif

  time_on_h = EEPROM.read(0);
  time_on_m = EEPROM.read(1);
  clock.begin();

  blink_timeout.setTimeout(1300);           //  настроить таймаут 1300 мс для изменения времени
  blink_timeout.stop();                     //  остановить таймер
  no_blink.setTimeout(10000);               //  таймаут если не трогать энкодер
  no_blink.stop();                          //  остановить таймер
  sleep_timer.setTimeout(TIME_TO_SLEEP);    //  таймер перехода в сон

  if (!digitalRead(ENC_KEY))
  {
    bool switch_btn = false;                //  смена состояния кнопки
    disp.clear();
    disp.brightness(bright);
    while ( level < 6 )
    {
      if (!digitalRead(ENC_KEY) && switch_btn == false )
      {
        switch_btn = true;
        if ( level == 0 )
        {
          byte troll[4] = {_dash, _dash, _dash, _dash};
          disp.displayByte(troll);
        }
#if ( DEBUG == 1 )
        Serial.print("Нажата "); Serial.print("level "); Serial.println(level);
        Serial.print(h); Serial.print(" ");
        Serial.print(m); Serial.print(" ");
        Serial.print(d); Serial.print(" ");
        Serial.print(mes); Serial.print(" ");
        Serial.print(y); Serial.print(" ");
#endif
      }

      if (digitalRead(ENC_KEY) && switch_btn == true )
      {
        switch_btn = false;
        level++;
        if ( d == 0 ) d = 1;
        if ( mes == 0 ) mes = 1;
        if ( y == 0 ) y = 2022;
        blink_clock = true;
        if ( level > 5 ) 
        {
         level = 1;
        }
        updDispSetup();
#if ( DEBUG == 1 )
        Serial.print("Отпущена ");
        Serial.print("level ");
        Serial.println(level);
        Serial.print("blink_clock ");
        Serial.println(blink_clock);
#endif
      }
      controlTickSetup();
      if ( blink_clock ) blink_time_setup();
    }
    clock.setDateTime(y, mes, d, h, m, 00);
  }

  //  time_on_h = EEPROM.read(0);
  //  time_on_m = EEPROM.read(1);
  //  clock.begin();
  //  установка времени системы
  //  clock.adjust(DateTime(F(__DATE__), F(__TIME__)));
  //  January 21, 2014 at 3am you would call:
  //  clock.adjust(DateTime(2021, 9, 11, 1, 6, 40));
  //  clock.setDateTime(__DATE__, __TIME__);
  //  clock.setDateTime(2022, 11, 6, 16, 23, 00);

  //  blink_timeout.setTimeout(1300);           //  настроить таймаут 1300 мс для изменения времени
  //  blink_timeout.stop();                     //  остановить таймер
  //  no_blink.setTimeout(10000);               //  таймаут если не трогать энкодер
  //  no_blink.stop();                          //  остановить таймер
  //  sleep_timer.setTimeout(TIME_TO_SLEEP);    //  таймер перехода в сон

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, state_relay);

  // на всякий случай очистим дисплей и установим яркость
  disp.clear();
  disp.brightness(bright);
  updDisp();                        // обновить дисплей
}

void loop()
{
  DS_3231();                        //  время из модуля
  controlTick();                    //  опрос энкодера
  if ( blink_clock )  blink_time(); //  мигание при установке времени
  if (state) timerTick();           //  если запущен таймер
  if (sleep_flag) sleep_screen();   //  сон по таймеру
}

// ================== ФУНКЦИИ ==================

void DS_3231()                      //  время с модуля
{
  //  DateTime dt = clock.now();
  dt = clock.getDateTime();
  //  Serial.print(dt.dayOfTheWeek());
  //  Serial.print(" ");
  prev_s = s;
  //  h = dt.hour();
  //  m = dt.minute();
  //  s = dt.second();
  h = dt.hour;
  m = dt.minute;
  s = dt.second;

#if ( DEBUG == 1 )
  if ( s != prev_s )
  {
    Serial.print(h); Serial.print(":");
    Serial.print(m); Serial.print(":");
    Serial.print(s); Serial.print("   ");

    Serial.print("blink_clock");  Serial.print("\t  mode"); Serial.print("\t  prev_mode"); Serial.print("\t state");  Serial.println("\t  sleep_flag");
    Serial.print("\t\t"); Serial.print(blink_clock);  Serial.print("\t    "); Serial.print(mode); Serial.print("\t     "); Serial.print(prev_mode);  Serial.print("\t\t   "); Serial.print(state); Serial.print("\t     "); Serial.println(sleep_flag);
    Serial.println();
  }
#endif
}

void controlTick()                  //  опрос энкодера
{
  enc.tick();                       // опрос энкодера
  if ( enc.turn() )
  {
    if ( mode == 1 || mode == 2 )
    {
      blink_clock = false;
      blink_timeout.start();
      no_blink.start();
    }

    if (enc.right())
    {
      if ( mode == 1 )
      {
        time_on_h++;
        if ( time_on_h > 23 ) time_on_h = 0;
      }
      else if ( mode == 2 )
      {
        time_on_m++;
        if ( time_on_m > 59 ) time_on_m = 0;
      }
    }

    if (enc.left())
    {
      if ( mode == 1 )
      {
        time_on_h--;
        if ( time_on_h < 0 ) time_on_h = 23;
      }
      else if ( mode == 2 )
      {
        time_on_m--;
        if ( time_on_m < 0 ) time_on_m = 59;
      }
    }
    if ( sleep_flag )
    {
      sleep_flag = false;
      mode = prev_mode;
      disp.brightness(bright);
    }
    sleep_timer.start();
    updDisp();
  }

  if (enc.click())                  // если кликнули
  {
    if ( state == true && switch_relay == true )    //  если таймер и реле работает
    {
      fade_timer = fade_timer_default;              //  сбросить анимацию запуска таймера
      state = false;                                //  отключить таймер ( timerTick )
      switch_relay = false;                         //  сбросить флаг сработки реле
      state_relay = !state_relay;                   //  отключить реле
      digitalWrite(RELAY_PIN, state_relay);
      disp.point(1);                                //  вывести точки
    }
    else
    {
      mode++;
      if ( mode == 1 || mode == 2 )
      {
        blink_clock = true;
        no_blink.start();
      }
      if ( mode > 2 )
      {
        if ( blink_clock == true ) blink_clock = false;
      }
      if ( mode >= 4 )
      {
        mode = 0;
        counter_s = 0;
        min = 0;
      }
    }
    if ( sleep_flag )
    {
      sleep_flag = false;
      mode = prev_mode;
      disp.brightness(bright);
    }
    sleep_timer.start();
    updDisp();                                      //  вывести на экран согласно mode
  }

  if (enc.held())                   // кнопка энкодера удержана
  {
    if ( state_relay == HIGH && !state && mode < 3 )  // если реле не включено вручную, таймер не запущен, не в меню ручного вкл и не в меню показа таймера
    {
      state = true;                 // запускаем
      if ( blink_clock == true )  blink_clock = false;              //  выключаем мигание, если устанавливали время
      disp.displayClock(time_on_h, time_on_m);                      //  показываем что устанавливали
      mode = 0;                                                     //  начальный экран для прокрутки экранов с начала
    }
    if ( mode == 3 )                                                //  если режим ручного управления
    {
      state_relay = !state_relay;                                   //  включаем или выключаем реле
      digitalWrite(RELAY_PIN, state_relay);
      updDisp();                                                    //  вывести на экран согласно mode
    }
  }

  if ( sleep_timer.isReady() && (mode == 0 || mode == 3) )
  {
    prev_mode = mode;
    mode = 10;
    sleep_flag = true;              //  запуск часов на экране сна
    disp.brightness(sleep_bright);
    updDisp();
  }
  if ( (mode == 1 || mode == 2) && blink_timeout.isReady() )  blink_clock = true;   //  возобновить мигание параметра по таймеру blink_timeout
  if ( (mode == 1 || mode == 2) && blink_clock == true && no_blink.isReady() )      //  если в режиме установки и таймер no_blink вышел
  {
    mode = 0;                                                       //  стартовый экран
    if ( blink_clock == true )  blink_clock = false;                //  выключаем мигание
    updDisp();                                                      //  вывести на экран согласно mode
#if ( DEBUG == 1 )
    Serial.println("Таймер 10");
#endif
  }
}

void timerTick()                    //  мигание точек, вкл реле и счетчик при запущенном таймере
{
  while ( fade_timer > 0 )
  {
    fade_timer--;
    for (int i = bright; i > 0; i--) {
      disp.brightness(i);   // меняем яркость
      delay(40);
    }
    for (int i = 0; i < bright + 1; i++) {
      disp.brightness(i);   // меняем яркость
      delay(40);
    }
  }

  if ( s != prev_s && mode != 3 )
  {
    static bool dotsFlag = true;
    dotsFlag = !dotsFlag;
    disp.point(dotsFlag);           // обновляем точки
  }

  if ( time_on_h == h && time_on_m == m && switch_relay == false )  //  вкл реле по достижению времени
  {
    if ( sleep_flag )
    {
      sleep_flag = false;
      disp.brightness(bright);
    }
    switch_relay = true;
    if ( state_relay == HIGH ) state_relay = !state_relay;
    else state_relay = LOW;
    digitalWrite(RELAY_PIN, state_relay);
    mode = 4;
    disp.clear();
  }

  if ( switch_relay == true && ( s != prev_s ) )
  {
    if ( counter_s > 59 )
    {
      counter_s = 0;
      min++;
    }
    if ( min == 60 )
    {
      fade_timer = fade_timer_default;              //  сбросить анимацию запуска таймера
      state = false;                                //  отключить таймер ( timerTick )
      switch_relay = false;                         //  сбросить флаг сработки реле
      state_relay = !state_relay;                   //  отключить реле
      digitalWrite(RELAY_PIN, state_relay);
      mode = 0;
      counter_s = 0;
      min = 0;
      sleep_timer.start();
    }
    updDisp();
    counter_s++;
  }
}

// обновить дисплей в зависимости от текущего режима отображения
void updDisp()
{
  switch (mode)
  {
    case 0:
      disp.point(1);
      disp.displayClockScroll(time_on_h, time_on_m, 100);
      if ( EEPROM.read(0) != time_on_h )  EEPROM.write(0, time_on_h);
      if ( EEPROM.read(1) != time_on_m )  EEPROM.write(1, time_on_m);
      break;

    case 1:
      disp.point(0);
      disp.displayClockScroll(time_on_h, time_on_m, 10);
      break;

    case 2:
      disp.point(0);
      disp.displayClockScroll(time_on_h, time_on_m, 10);
      break;

    case 3:
      disp.point(0);
      if (state_relay)
        //disp.displayByte(_O, _f, _f, _empty);
        disp.twistByte(off, 20);
      else
        //disp.displayByte(_o, _n, _empty, _empty);
        disp.twistByte(on, 20);
      break;

    case 4:
      disp.displayClockTwist(min, counter_s, 15);
      break;

    case 10:
      disp.displayClockScroll(h, m, 100);
      break;
  }
}

void blink_time()
{
  static bool flag_h, flag_m;
  if ( s % 2 == 0 )
  {
    if ( mode == 1 && flag_h == false )
    {
      flag_h = true;
      disp.displayByte(0, _empty);
      disp.displayByte(1, _empty);
    }
    else if ( mode == 2 && flag_m == false )
    {
      flag_m = true;
      disp.displayByte(2, _empty);
      disp.displayByte(3, _empty);
    }
  }
  else if ( (mode == 1 && flag_h == true) || (mode == 2  && flag_m == true) )
  {
    flag_h = false;
    flag_m = false;
    disp.displayClock(time_on_h, time_on_m);
  }
}

void sleep_screen()
{
  if ( s != prev_s )
  {
    static bool dotsFlag = true;
    dotsFlag = !dotsFlag;
    disp.point(dotsFlag);           // обновляем точки
    updDisp();
  }
}

void controlTickSetup()
{
  enc.tick();
  if ( enc.turn() )
  {
    if ( level == 1 || level == 2 || level == 3 || level == 4 || level == 5 )
    {
      blink_clock = false;
      blink_timeout.start();
    }
    if (enc.right())
    {
      if ( level == 1 )
      {
        h++;
        if ( h > 23 ) h = 0;
      }
      else if ( level == 2 )
      {
        m++;
        if ( m > 59 ) m = 0;
      }
      else if ( level == 3 )
      {
        d++;
        if ( d > 31 ) d = 1;
      }
      else if ( level == 4 )
      {
        mes++;
        if ( mes > 12 ) mes = 1;
      }
      else if ( level == 5 )
      {
        y++;
        if ( y > 2099 ) y = 2022;
      }
    }
    if (enc.left())
    {
      if ( level == 1 )
      {
        h--;
        if ( h < 0 ) h = 23;
      }
      else if ( level == 2 )
      {
        m--;
        if ( m < 0 ) m = 59;
      }
      else if ( level == 3 )
      {
        d--;
        if ( d < 1 ) d = 31;
      }
      else if ( level == 4 )
      {
        mes--;
        if ( mes < 1 ) mes = 12;
      }
      else if ( level == 5 )
      {
        y--;
        if ( y < 2022 ) y = 2099;
      }
    }
    updDispSetup();
  }

  if (enc.held())
  {
    disp.point(0);
    runningText();
    if ( blink_clock )  blink_clock = false;
    sleep_timer.start();
    level = 6;
  }

  if ( (level == 1 || level == 2 || level == 3 || level == 4 || level == 5) && blink_timeout.isReady() )  blink_clock = true;
}

// обновить дисплей в зависимости от текущего режима настроек
void updDispSetup()
{
  switch (level)
  {
    case 1:
      disp.point(1);
      disp.displayClockScroll(h, m, 3);
      break;

    case 2:
      disp.point(1);
      disp.displayClockScroll(h, m, 3);
      break;

    case 3:
      disp.point(0);
      disp.displayClockScroll(d, mes, 3);
      break;

    case 4:
      disp.point(0);
      disp.displayClockScroll(d, mes, 3);
      break;

    case 5:
      disp.point(0);
      disp.displayInt(y);
      break;

  }
}

void blink_time_setup()
{
  static bool flag_h, flag_m, flag_d, flag_mes, flag_y;
  if ( millis()/1000 % 2 == 0 )
  {
    if ( level == 1 && flag_h == false )
    {
      flag_h = true;
      disp.displayByte(0, _empty);
      disp.displayByte(1, _empty);
    }
    else if ( level == 2 && flag_m == false )
    {
      flag_m = true;
      disp.displayByte(2, _empty);
      disp.displayByte(3, _empty);
    }
    else if ( level == 3 && flag_d == false )
    {
      flag_d = true;
      disp.displayByte(0, _empty);
      disp.displayByte(1, _empty);
    }
    else if ( level == 4 && flag_mes == false )
    {
      flag_mes = true;
      disp.displayByte(2, _empty);
      disp.displayByte(3, _empty);
    }
    else if ( level == 5 && flag_y == false )
    {
      flag_y = true;
      disp.displayByte(0, _empty);
      disp.displayByte(1, _empty);
      disp.displayByte(2, _empty);
      disp.displayByte(3, _empty);
    }
  }
  else if ( (level == 1 && flag_h == true) || (level == 2  && flag_m == true) )
  {
    flag_h = false;
    flag_m = false;
    disp.displayClock(h, m);
  }
  else if ( (level == 3  && flag_d == true) || (level == 4  && flag_mes == true) )
  {
    flag_d = false;
    flag_mes = false;
    disp.displayClock(d, mes);
  }
  else if ( level == 5  && flag_y == true )
  {
    flag_y = false;
    disp.displayInt(y);
  }
}

void runningText()
{
  byte welcome_banner[] = {_C, _O, _F, _F, _E, _E
                          };
                          disp.runningString(welcome_banner, sizeof(welcome_banner), 200);  // 200 это время в миллисекундах!
}
