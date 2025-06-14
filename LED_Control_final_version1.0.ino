// 首先引用库，包括Wifi（连接wifi）、PubSubClient(创建MQTT协议类)、FastLED（灯带特效库）、
// arduinoFFT（傅里叶变换）、ArduinoJson、高斯滤波(Arduinio和ESP32灯嵌入式开发板的一个高效Json解析库)
#include <FastLED.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <arduinoFFT.h>
#include <Preferences.h>
#include <driver/i2s.h>

//+++++++++++++++++++++++++++++++++++++++++++前端参数定义+++++++++++++++++++++++++++++++++++++++++++++++
                                         // 硬件引脚定义
#define NUM_LEDS             36          // 定义灯带LED数目36颗
#define LOW_DATA_PIN          4          // 定义控制低频灯带的Datain针脚为D4
#define HIGH_DATA_PIN         5          // 定义控制高频灯带的Datain针脚为D5
#define ADC_UNIT          ADC_UNIT_1     //指定使用的是哪个ADC单元。ESP32有两个ADC单元， ADC_UNIT_1和ADC_UNIT_2
#define ADC_CHANNEL      ADC1_CHANNEL_0  // ADC1_CH0是GPIO36引脚（vp引脚），这里定义36引脚为ADC_CHANNEL

                              // 灯带参数配置
#define LED_TYPE WS2812       // 定义光带型号
#define COLOR_ORDER GRB       // RGB灯珠中的R\G\B排列顺序
uint8_t brightness = 20;      // LED亮度控制变量，范围是0~255
String  glow_mode = "1";      // 定义了glow_mode字符串作为发光模式，赋值为1，用于后续判断和切换发光模式。
CRGB    leds_Low[NUM_LEDS];   // 建立了光带对象，定义NUM_LEDS为一个数组，数组包括36个元素，每个元素为RGB灯，
CRGB    leds_High[NUM_LEDS];  // 数组名称为leds,类型为CRGB，CRGB可以表示每个元素的颜色。

//++++++++++++++++++++++++++++++++++++++++++Wifi连接函数++++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
const char *ssidList[]     = {"king", "InnerPeace"};                  // const char用于定义一个不需要修改的字符，字符常量，此处wifi名称不需要修改。
const char *passwordList[] = {"16011601", "chinguang"};               // wifi密码也是常量字符。
const int wifiCount        = sizeof(ssidList) / sizeof(ssidList[0]);

//==========================================主体函数部分==============================================
  // 一个更通用的 WiFi 连接函数，支持尝试多个 SSID
void connectWiFi()
{
  Serial.println("Setting WiFi to STA mode and clearing previous configs…");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  for (int i = 0; i < wifiCount; i++)
  {
    const char *ssid = ssidList[i];
    const char *pwd  = passwordList[i];
    Serial.printf("→ 尝试连接到 WiFi: %s / %s\n", ssid, pwd);
    WiFi.begin(ssid, pwd);

    int   attempt         = 0;
    const int maxAttempts = 15;  // 最多等 15 秒
    while (WiFi.status() != WL_CONNECTED && attempt < maxAttempts)
    {
      Serial.print('.');
      delay(1000);
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("\n 已连接到 %s，IP：%s\n", ssid, WiFi.localIP().toString().c_str());
      return;  // 连接成功就退出
    }
    else
    {
      Serial.printf("\n 无法连接到 %s（状态码 %d），尝试下一个...\n",
                    ssid, WiFi.status());
    }
  }
}

//++++++++++++++++++++++++++++++++++++++++++MQTT连接函数+++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
  // 配置Thingscloud云平台信息,信息在Thingscloud云端复制过来就行
#define mqtt_broker "sh-3-mqtt.iot-api.com"
#define mqtt_port 1883
#define PROJECT_KEY "Jdv8zcTWFL"
String ACCESS_TOKEN = "a9u8fe2wf1nfxes7";

WiFiClient   net;                                                        // 给库WIFiclient实例化一个类net，用来管理MQTT的网络连接
void         callback(char *topic, byte *payload, unsigned int length);  // 定义callback函数，收到消息时用callback回调
PubSubClient mqtt(mqtt_broker, mqtt_port, callback, net);                // 使用 PubSubClient 这个“类”，创建了一个叫 mqtt 的“对象”，并传入了构造参数：mqtt_broker、mqtt_port、callback、net。

//==========================================主体函数部分==============================================
void connectMQTT()
{

  if (mqtt.connect("", ACCESS_TOKEN.c_str(), PROJECT_KEY))
  {
    mqtt.subscribe("command/send/+");
    mqtt.subscribe("attributes/push");
    mqtt.subscribe("attributes/get/response/+");
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  unsigned int termination_pos;  // 定义无符号整型变量t_p
  if (strlen(topic) + length + 9 >= mqtt.getBufferSize())
  { // strlen计算第一个字符到\0之间字符个数，getbuffersize函数获取页面缓冲区的大小，默认8KB大小
    termination_pos = length - 1;
  }
  else
    termination_pos = length;

    // 在 payload（有效载荷）的末尾添加字符串终止符（string termination code），然后我们把它转换成一个 String 对象。
  payload[termination_pos] = '\0';
  String payload_str((char *)payload);
  String topic_str(topic);
    // 调用
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, payload_str);
  if (error)
    return;
  JsonObject obj = doc.as<JsonObject>();
  if (String(topic) == "attributes/get/response/1000")
  { // 如果接收到了属性获取的响应
    if (obj.containsKey("result") && obj.containsKey("attributes"))
    {
      JsonObject attributes = doc["attributes"];
      parse_cloud_attributes(attributes);
    }
  }
  if (String(topic) == "attributes/push")
  { // 如果接收下发的属性
    parse_cloud_attributes(obj);
  }
}

//+++++++++++++++++++++++++++++++++++++++++++云端指令分析++++++++++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
  // 定义一些用于APP控制的变量名称
int     red          = 255;
int     green        = 0;
int     blue         = 0;
int     rgb          = 255;
uint8_t hue          = 0;     // 色环转换标志位，CHSV、hue可用
uint8_t speed        = 50;    // 起始的色彩变化速度定义为50
bool    led_state    = true;  // 灯光模式状态变量起始的灯带状态为亮灯
bool    mode_1_index = true;  // 用于检测云端数据是更新，便于模式1重新擦除
//==========================================主体函数部分==============================================
void parse_cloud_attributes(JsonObject obj)
{
  bool changed = false;  // 标记颜色是否变更
  if (obj.containsKey("red"))
  { // obj调用其方法containskey，判断是否包含指定的键值red
    red     = obj["red"];
    changed = true;
  }
  if (obj.containsKey("green"))
  {
    green   = obj["green"];
    changed = true;
  }
  if (obj.containsKey("blue"))
  {
    blue    = obj["blue"];
    changed = true;
  }
  if (obj.containsKey("rgb_number"))
  {
    rgb = obj["rgb_number"];
                                 // 将 rgb_number 转换为独立的 red、green、blue
    red   = (rgb >> 16) & 0xFF;  // 提取红色通道
    green = (rgb >> 8) & 0xFF;   // 提取绿色通道
    blue  = rgb & 0xFF;          // 提取蓝色通道
    changed = true;
  }

  if (obj.containsKey("glow_mode"))
  {
    const char *mode = obj["glow_mode"];
          glow_mode  = String(mode);
          changed    = true;
  }
  if (obj.containsKey("speed"))
  {
    speed   = obj["speed"];
    speed   = 100 - speed;
    changed = true;
  }
  if (obj.containsKey("led_state"))
  {
    led_state = obj["led_state"];
  }
  if (obj.containsKey("led_brightness"))
  {
    brightness = obj["led_brightness"];
    FastLED.setBrightness(brightness);
    FastLED.show();
    changed = true;
  }
  if (changed)
  {
    saveSettings();  // 保存备份
  }
  mode_1_index = true;
}

//+++++++++++++++++++++++++++++++++++++灯光模式及颜色备份函数++++++++++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义==============================================
Preferences preferences;
//==========================================主体函数部分==============================================
void saveSettings()                                            // 存储
{
  preferences.begin     ("led-settings", false);               // write mode
  preferences.putString("glow_mode", glow_mode);               // 写入发光模式
  preferences.putUChar  ("red", red);                          // 写入发光颜色
  preferences.putUChar  ("green", green);                      // 写入发光颜色
  preferences.putUChar  ("blue", blue);                        // 写入发光颜色
  preferences.putUInt   ("speed", speed);                      // 写入变化速度
  preferences.putUInt   ("brightness", brightness);            //写入灯光亮度
  preferences.end();
}
  
void loadSettings()                                             // 加载
{
  preferences.begin("led-settings", true);                      // read-only mode
  glow_mode         = preferences.getString("glow_mode", "3");  // 读取发光模式
  red               = preferences.getUChar("red", 255);         // 读取发光颜色
  green             = preferences.getUChar("green", 0);         // 读取发光颜色
  blue              = preferences.getUChar("blue", 0);          // 读取发光颜色
  speed             = preferences.getUInt("speed", 70);         // 读取变化速度
  brightness        = preferences.getUInt("brightness", 80);    // 读取灯光亮度
  preferences.end();
}

//+++++++++++++++++++++++++++++++++++++++++按钮功能设定++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
                                               // 按钮变量+消抖参数配置
#define  BUTTON_PIN                     33     // 定义按钮接在 GPIO33
const    unsigned long fadeInterval  = 200;    // 用于长按改变灯光颜色
const    unsigned long longPressTime = 800;    // ≥800ms 视为长按
const    unsigned long hueInterval   = 250;    // 每250ms 变一次色
unsigned long debounceDelay          = 50;     // 按键消抖时间（毫秒）
unsigned long lastDebounceTime       = 0;      // debounce用于按钮的防抖
unsigned long pressStartTime         = 0;      // 引入长按识别参数，长按连续变色用
unsigned long lastHueChangeTime      = 0;      // 用于圆环变色，记录上一次Hue变化时间
unsigned long wipeLast               = 0;      // 用于长按变色的节奏控制
unsigned long lastFadeTime           = 0;      // 用于长按改变灯光颜色
bool     inLongPressMode             = false;  // 正在“连续变色”模式
bool     buttonState                 = HIGH;   // 消抖后稳定状态
bool     lastReading                 = HIGH;   // 上一次原始读数
bool     settingChanged              = false;  // 用于记录颜色和亮度是否更改
int      reading                     = HIGH;   // 读取按钮状态

//==========================================主体函数部分==============================================
void button_Function()
{
  reading = digitalRead(BUTTON_PIN);                       // 检测按钮按下状态，是短按还是长按
  if (reading != lastReading)
  {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > debounceDelay)
  {                                                        // 按钮稳定按下时发生变化

    if (reading != buttonState)
    {                                                      // 如果按钮由高变低（按下）
      buttonState = reading;
      if (buttonState == LOW)
      {                                                    // 按下瞬间HIG→LOW，记录按下时间
        pressStartTime    = millis();
        inLongPressMode   = false;                         // 视为短按，不是长按
        lastHueChangeTime = millis();
      }
      else
      {                                                    // 短按情况下按钮用来切换模式
        unsigned long held = millis() - pressStartTime;
        if (held < longPressTime)
        {
          int mode      = glow_mode.toInt();               // 把glow_mode变成整型
              mode      = (mode % 10) + 1;                 // 模式累加，10之后回到1
              glow_mode = String(mode);                    // 再把累加果的mode变成字符型赋值给模式
          if (glow_mode == "1")
          {                                                // 如果模式是1或者10时，单色填充，不进行填充灯带仍停留在上衣模式颜色不变
            fill_solid(leds_Low, NUM_LEDS, CHSV(hue, 255, 255));
            fill_solid(leds_High, NUM_LEDS, CHSV(hue, 255, 255));
            FastLED.show();
          }
          saveSettings();                                  // 存储按钮改变的灯光模式
        }
        else                                               // 如果是长按再松手，保存长按后的设置
        {
          if (settingChanged)
          {
            saveSettings();
            settingChanged = false;                        // 重置settingChanged
          }
        }
      }                                                    
    }
  }
  lastReading = reading;

  if (buttonState == LOW)
  {                                                         // 识别按钮位长按
    unsigned long held = millis() - pressStartTime;
    if (held >= longPressTime)
    {

      if (!inLongPressMode)
      {                                                     // 首次达到长按阈值时，进入长按模式
        inLongPressMode = true;
      }
                                                            // 长按时根据不同模式进行相应变换
      if (glow_mode == "1")
      {                                                     // 模式1时长按变色
        if (inLongPressMode && millis() - lastHueChangeTime >= hueInterval)
        {                                                   // 模式1时 每隔 hueInterval 更新一次色环
          lastHueChangeTime = millis();
          hue               = (hue + 4) % 256;              // 步长 = 8，可调
          fill_solid(leds_Low, NUM_LEDS, CHSV(hue, 255, 255));
          fill_solid(leds_High, NUM_LEDS, CHSV(hue, 255, 255));
          FastLED.show();

          CRGB current = CHSV(hue, 255, 255);               // 将CHSV转换成rgb便于存储备份
               red     = current.r;
               green   = current.g;
               blue    = current.b;

          settingChanged = true;                            //  设置已变更
        }
      }
      else if (glow_mode == "8" || glow_mode == "9")
      {                                                     // 模式9时改变speed，这里speed对反应灯柱高度，以适应不同音量下的灯柱高度

        if (inLongPressMode && millis() - lastHueChangeTime >= hueInterval)
        {
               lastHueChangeTime = millis();
               speed             = (speed + 5) % 100;         // 步长 = 5，可调
        }
      }
      else if (glow_mode == "10")
      {                                                     // 模式10时变色，每隔 hueInterval 更新一次色环

        if (inLongPressMode && millis() - lastHueChangeTime >= hueInterval)
        {
               lastHueChangeTime = millis();
               hue               = (hue + 4) % 256;         // 步长 = 8，可调
          CRGB current           = CHSV(hue, 255, 255);     // 将CHSV转换成rgb便于存储备份
               red               = current.r;
               green             = current.g;
               blue              = current.b;

          settingChanged = true;                            //  设置已变更
        }
      }
      else
      {
        if (millis() - lastFadeTime >= fadeInterval)
        {                                                     // 除了模式1，8,9,10的其他模式时，长按按钮改变灯光亮度
                  lastFadeTime  = millis();                   // 小于 30 用 1，≥30 用 4，uint8_t 溢出会自动回到 0
          uint8_t step          = (brightness < 30) ? 2 : 5;  // step =(brightness < 30) ? 2 : 5，这是一个条件取值的表达式，通过对亮度在0-30和30-255范围的不同来给step赋不同的值
                  brightness   += step;
          FastLED.setBrightness(brightness);
          FastLED.show();

          settingChanged = true;                              //  设置已变更
        }
      }
    }
  }
}

//+++++++++++++++++++++++++++++++++++++++++灯光模式判断++++++++++++++++++++++++++++++++++++++++++++
//==========================================主体函数部分==============================================
void light_Mode()
{
    // 更新灯光状态
  if (led_state)
  {
    if (glow_mode == "1")
    {
      colorWipe(speed);
    }
    else if (glow_mode == "2")
    {
      colorWipeBreath(speed);
    }
    else if (glow_mode == "3")
    {
      rainbowBreath(speed);
    }
    else if (glow_mode == "4")
    {
      rainbowWave(speed);
    }
    else if (glow_mode == "5")
    {
      rainbowFlow(speed);
    }
    else if (glow_mode == "6")
    {
      run_Light(speed);
    }
    else if (glow_mode == "7")
    {
      juggle(speed);
    }
    else if (glow_mode == "8")
    {
      visualization();
    }
    else if (glow_mode == "9")
    {
      visualization();
    }
    else if (glow_mode == "10")
    {
      visualization();
    }
  }
  else
  {
    fill_solid(leds_Low, NUM_LEDS, CRGB::Black);  // 如果 LED 状态为 false，关闭所有灯光
    fill_solid(leds_High, NUM_LEDS, CRGB::Black);
    FastLED.show();
    saveSettings();                               // 存储灯光模式变化
  }
}

//+++++++++++++++++++++++++++++++++++++++++灯光模式设定+++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
  // 见起初配置文件
//==========================================主体函数部分==============================================
  // 定义颜色圆环
CRGB Wheel(byte WheelPos)
{
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85)
  {
    return CRGB(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170)
  {
    WheelPos -= 85;
    return CRGB(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return CRGB(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// 模式1：颜色擦除
void colorWipe(uint8_t internal)
{ // wiprcolor和loop部分用到了hue，这里设置一下
  static unsigned long last = 0;
  static uint8_t wipeIndex  = 0;
  if (mode_1_index)
  { // 检查是否收到新颜色，重置 wipeIndex
    wipeIndex = 0;
    mode_1_index = false;
  }
  if (wipeIndex < NUM_LEDS && millis() - last >= internal)
  {
    last                 =  millis();
    CRGB wipe_color      =  CRGB(red, green, blue);
    leds_Low[wipeIndex]  =  wipe_color;
    leds_High[wipeIndex] =  wipe_color;
    wipeIndex++;
    FastLED.show();
  }
}

// 模式2：纯色呼吸渐变
void colorWipeBreath(uint8_t internal)
{
  static  unsigned long lastMillis = 0;
  static  uint8_t rainbow_j        = 0;
  static  uint8_t breathPhase      = 0;
  uint8_t brightness               = sin8(breathPhase);  // 使用 sin8 实现亮度呼吸效果（波浪式变化）

  if (millis() - lastMillis >= internal)
  {
    lastMillis = millis();
    rainbow_j  = (rainbow_j + 1) % 256;  // 每帧更新色相偏移 + 呼吸进度
    breathPhase++;

    for (uint16_t i = 0; i < NUM_LEDS; i++)
    {
      CRGB color   = Wheel((i + rainbow_j) & 255);
      leds_Low[i]  = color;
      leds_Low[i].nscale8_video(brightness); // 用亮度值缩放RGB
      leds_High[i] = leds_Low[i];            // 镜像设置
    }
    FastLED.show();
  }
}

// 模式3：彩虹呼吸渐变
void rainbowBreath(uint8_t internal)
{                                              // 整体流水彩虹灯
  static uint8_t breathPhase      = 0;         // 控制亮度波动
  static uint8_t hueShift         = 0;         // 控制色彩变化
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate >= internal)
  {
    lastUpdate = millis();
    breathPhase++;
    if (breathPhase % 8 == 0)
      hueShift++;                              // 每隔几帧变一点颜色
  }

  uint8_t brightness = sin8(breathPhase);      // 呼吸波形亮度

  for (int i = 0; i < NUM_LEDS; i++)
  {
    uint8_t hue  = hueShift + i * 4;           // hue随着hueShift缓慢变色
    leds_Low[i]  = CHSV(hue, 255, brightness);
    leds_High[i] = CHSV(hue, 255, brightness); // 或255 - brightness 为镜像渐变
  }
  FastLED.show();
}

// 模式4彩虹流波
void rainbowWave(uint8_t internal)
{
  static int8_t offset = 0;
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= internal)
  {
    lastUpdate = millis();
    offset++;
  }
  for (int i = 0; i < NUM_LEDS; i++)
  {
    float wave   = sin8(i * 10 + offset);          // 产生波动亮度
    leds_Low[i]  = CHSV(i * 5, 255, wave);         // 低频频灯带色调偏移
    leds_High[i] = CHSV(i * 5 + 128, 255, wave);   // 高频灯带色调偏移
  }

  FastLED.show();
}

// 模式5 彩虹流水
void rainbowFlow(uint8_t internal)
{
  static uint8_t startIndex       = 0;
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= internal)
  {
    lastUpdate = millis();
    startIndex++;
  }

  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds_Low[i]                 = CHSV(startIndex + i * 8, 255, 255);
    leds_High[NUM_LEDS - 1 - i] = leds_Low[i];      // 镜像同步低频灯带
  }
  FastLED.show();
}

  // 模式6 彩虹流水贯穿
void run_Light(uint8_t internal)
{
  static unsigned long lastMillis = 0;
  static int16_t currentIndex     = 0;
  static int8_t direction         = 1;             // 控制往返
  const  uint16_t totalLength     = NUM_LEDS * 2;  // 灯带总长度变化

  if (millis() - lastMillis < 30)
    return;
  lastMillis = millis();
                      
  fadeToBlackBy(leds_Low, NUM_LEDS, speed);        // 拖尾渐暗
  fadeToBlackBy(leds_High, NUM_LEDS, speed);

  if (currentIndex < NUM_LEDS)
  {                                                // 映射 currentIndex 到灯带索引
    leds_Low[currentIndex] = CHSV(hue + currentIndex * 2, 255, brightness);
  }
  else
  {                                                // High 灯带，反向
    uint16_t highIndex   = (NUM_LEDS * 2 - 1) - currentIndex;
    leds_High[highIndex] = CHSV(hue + highIndex * 2, 255, brightness);
  }
  FastLED.show();
  currentIndex += direction;                        // 更新 currentIndex

  if (currentIndex >= totalLength - 1 || currentIndex <= 0)
  {
    direction = -direction;                         // 到两端反向
  }
  EVERY_N_MILLISECONDS(10)
  {
    hue++;                                          // 彩虹颜色滚动
  }
}

  // 模式7正弦渐变
void juggle(uint8_t internal)
{                                                                 // 正弦函数灯珠渐变颜色

  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 50)
  {
    lastMillis = millis();

    fadeToBlackBy(leds_Low, NUM_LEDS, internal);                  // 以一定的速度逐渐熄灭
    fadeToBlackBy(leds_High, NUM_LEDS, internal);                 // 以一定的速度逐渐熄灭

    uint8_t dothue = 0;                                           // 颜色变化的变量

    for (int i = 0; i < 8; i++)
    {
      uint8_t   pos_Low = beatsin8(i + 7, 0, NUM_LEDS - 1);       // 使用beatsin8函数生成正弦波位置，控制点亮的灯珠
      uint8_t   pos_High = beatsin8(i + 7 + 4, 0, NUM_LEDS - 1);  // 控制High灯条不同的波动
                                                                  // 点亮LED
      leds_Low [pos_Low] = CHSV(dothue, 200, brightness);         // 灯条Low
      leds_High[pos_High] = CHSV(dothue, 200, brightness);        // 灯条High
      dothue    += 32;                                            // 色调增加，产生渐变色
    }
    FastLED.show();
  }
}

//++++++++++++++++++++++++++++++++++++++++++律动效果函数++++++++++++++++++++++++++++++++++++++++++++
//=========================================参数配置定义===========================================
                                    // FFT配置
#define SAMPLES            512      // Must be a power of 2
#define SAMPLING_FREQUENCY 44100UL  // Hz

  // 双缓冲FFT数组
float vRealBuffer1[SAMPLES];
float vImagBuffer1[SAMPLES];
float vRealBuffer2[SAMPLES];
float vImagBuffer2[SAMPLES];

  // 双缓冲索引和同步
volatile int writeBufferIndex = 0;
volatile int readBufferIndex  = 1;
volatile bool newFFTAvailable = false;
SemaphoreHandle_t xMutexBufferSwitch;

  // 灯光渲染参数
static  int pointJump[2]     = {0, 0};
int     pointCounter[2]      = {0, 0};
int     downCounter[2]       = {0, 0};
static  int uJump[2]         = {0, 0};
static  int dJump[2]         = {0, 0};
float   FFTValue[2]          = {0.0, 0.0};
const   int pointDownSpeed   = 12;
const   int downSpeed        = 5;
uint8_t gHue                 = 0;

TaskHandle_t xSampleFFTTaskHandle = NULL;
ArduinoFFT<float> FFT1(vRealBuffer1, vImagBuffer1, SAMPLES, SAMPLING_FREQUENCY);
ArduinoFFT<float> FFT2(vRealBuffer2, vImagBuffer2, SAMPLES, SAMPLING_FREQUENCY);

  // 模式8、9、10音乐律动白色跳跃点
void sampleFFTTask(void *pvParameters)
{
  while (true)
  {
    sampleAudio();  // 1) Acquire audio samples via I2S,通过I2S获取音频采样点
    performFFT ();  // 2) Compute FFT on the acquired data，FFT计算转换成频谱数据
                    // 保护切换缓冲区索引
    if (xSemaphoreTake(xMutexBufferSwitch, portMAX_DELAY) == pdTRUE)
    {
        // 切换写缓冲区索引
      writeBufferIndex = 1 - writeBufferIndex;
        // 读取缓冲区指向刚写好的缓冲区
      readBufferIndex = 1 - writeBufferIndex;
      newFFTAvailable = true;
      xSemaphoreGive(xMutexBufferSwitch);
    }
  }
}

void initI2S()
{
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
      .sample_rate = (int)SAMPLING_FREQUENCY,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count        = 8,                     
      .dma_buf_len          = 512,                  
      .use_apll             = false,
      .tx_desc_auto_clear   = false,
      .fixed_mclk           = 0};
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_adc_mode(ADC_UNIT, ADC_CHANNEL);           //配置I2S读取ADC采样数据时使用的单元和通道
  i2s_adc_enable(I2S_NUM_0);
}
/*I2S配置说明
.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
   MASTER定义esp32中的i2s为主模式（分主从模式），RX为接受信号，ADC_BUILT_IN为使用内置I2s
.sample_rate = (int)SAMPLING_FREQUENCY,
   采样频率差为信号频率的2倍，这里我们设置为44100hz，有效频率为22050hz，远远够用
.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  采样位数为16bit，但实际adc只有12bit,故在下文sampleAudio函数中对buffer[i] & 0x0FFF，取低12bit为有效位
.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
  这个表示 I2S 采样或者输出时，只使用右声道数据，I2S 支持立体声（左右两个声道），这里只采集或处理右声道数据
.communication_format = I2S_COMM_FORMAT_I2S_MSB,
  表示数据传输格式，这里用的是标准 I2S 格式，数据最高有效位（MSB）先传输。
.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
  这是中断标志，配置 I2S 使用的中断优先级为1，标志决定了中断处理时的优先级和属性，ESP32 中断优先级范围 1~7，数字越大优先级越高。
.dma_buf_count        = 8,                     
  DMA（直接内存访问）缓冲区的数量，这里配置了 8 个缓冲区，采样数据会放在这几个缓冲区里交替使用，提高数据传输效率和稳定性
.dma_buf_len          = 512,                  
  每个 DMA 缓冲区的长度，即每个缓冲区能存放多少个数据采样点。esp32最大支持 1024 个数据。。
.use_apll             = false,
  是否使用 APLL（音频相位锁环）来产生更精确的时钟。false 表示不使用，使用内部时钟
.tx_desc_auto_clear   = false,
  传输时如果发生错误，是否自动清理 DMA 描述符。false 表示不自动清理，开发时一般设 false 以便调试。
.fixed_mclk           = 0};
  固定主时钟（MCLK）的频率，0 表示不固定，由硬件自动配置。如果需要对外输出固定频率的主时钟，比如给音频芯片用，可以配置具体频率（单位 Hz）。
*/

  // Read SAMPLES of raw audio data into vReal/vImag arrays
  
// Kalman 滤波器
class KalmanFilter {
public:
  KalmanFilter() {
    // 初始化卡尔曼滤波器的状态
    Q = 0.02;  // 过程噪声协方差（调节值）
    R = 0.1;   // 测量噪声协方差（调节值）
    P = 1.0;   // 初始估计误差协方差
    X = 0.0;   // 初始估计值
  }
  // 更新滤波器并返回滤波后的值
  double update(double measurement) {  // 预测阶段// X = X;  // 预测状态（这里我们认为速度v = 0，保持状态不变）
    // 更新阶段
    K = P / (P + R);                // 计算卡尔曼增益
    X = X + K * (measurement - X);  // 更新估计值
    P = (1 - K) * P + Q;            // 更新估计误差协方差
    return X;
  }
  // 设置过程噪声协方差
  void setProcessNoise(double q) {
    Q = q;
  }
  // 设置测量噪声协方差
  void setMeasurementNoise(double r) {
    R = r;
  }
private:
  double Q;  // 过程噪声协方差
  double R;  // 测量噪声协方差
  double P;  // 估计误差协方差
  double K;  // 卡尔曼增益
  double X;  // 估计值
};

KalmanFilter kalman;  // 创建卡尔曼滤波器实例

// 初始化滤波器
void initKalman() {
  kalman.setProcessNoise(0.01);     // 过程噪声（调节值）
  kalman.setMeasurementNoise(0.1);  // 测量噪声（调节值）
}
void sampleAudio()
{
  unsigned long start = micros();
  int16_t buffer[SAMPLES];
  size_t bytes_read;
  i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);

  float *vReal = (writeBufferIndex == 0) ? vRealBuffer1 : vRealBuffer2;
  float *vImag = (writeBufferIndex == 0) ? vImagBuffer1 : vImagBuffer2;

  for (int i = 0; i < SAMPLES; i++)
  {
    uint16_t raw     = buffer[i] & 0x0FFF;   // 因为采样的时候是按16bit采样的，这里对采样数据进行& 0x0FFF处理（提取低12位）
    double   voltage = raw * 3.3 / 4095.0f;  // 电压转换
    if (voltage < 0.05)
    voltage  = 0;        // 限幅去噪（阈值可调）
    vReal[i] = kalman.update(voltage); 
    vImag[i] = 0;        // FFT变换对于音频信号只有实部有用，故虚部vImg均定义为0
  }
  //打印采样时长
  unsigned long duration = micros() - start;
  Serial.printf("[采样耗时] sampleAudio(): %lu us\n", duration);
}

void performFFT()
{
  unsigned long start = micros();

  ArduinoFFT<float> *fft = (writeBufferIndex == 0) ? &FFT1 : &FFT2;
  fft->windowing(FFTWindow::Hamming, FFTDirection::Forward);
    // FFT变换前进行Hamming加窗处理，可以更好的防止频谱泄露，但是计算结果会损失约50%的幅度，什么是频谱泄露要记得
  fft->compute(FFTDirection::Forward);
  fft->complexToMagnitude();

  //打印FFT计算时长
  unsigned long duration = micros() - start;
  Serial.printf("[FFT耗时] performFFT(): %lu us\n", duration);
}

  // 可视化函数读取readBufferIndex缓冲区数据渲染
void visualization()
{
  if (!newFFTAvailable) return;

  unsigned long start = micros();

  if (xSemaphoreTake(xMutexBufferSwitch, portMAX_DELAY) == pdTRUE)
  {
    float *vReal = (readBufferIndex == 0) ? vRealBuffer1 : vRealBuffer2;

    FastLED.clear();

    static unsigned long lastHueUpdate = 0;
    if (millis() - lastHueUpdate >= 10)
    {
      gHue++;
      lastHueUpdate = millis();
    }

    float raw_Low = abs(vReal[2] );
      // 经过观察各频段的波形，决定取bin2，为低声律动频率
    float raw_High = abs(vReal[8]);
      // 经过观察各频段的波形，决定取bin8，为人声律动频率，，人声频率分布较广，但我对3-12的bin处理后效果依然不佳，故此处仅用bin8代表

    updateBand(raw_Low, leds_Low, 0);
    updateBand(raw_High, leds_High, 1);

    FastLED.show();

    newFFTAvailable = false;
    xSemaphoreGive(xMutexBufferSwitch);
  }
  //打印灯光渲染时长
  unsigned long duration = micros() - start;
  Serial.printf("[渲染耗时] visualization(): %lu us\n", duration);
}

  // ========== 可视化处理函数 ==========
void updateBand(float raw, CRGB *leds, int band)
{
  float maxFFTValue[2]    = {(float)speed, (float)speed*0.8}; //通过调整speed改变最大映射值来改变灯柱长度，以适应不同音量
  FFTValue[band]          = raw;

  int val = map(FFTValue[band], 0, maxFFTValue[band], 0, NUM_LEDS);
      val = constrain(val, 0, NUM_LEDS);

    // 柱上升
  uJump[band] += 4.0;
  if (val > uJump[band])
    val = uJump[band];
  else
    uJump[band] = val;

    // 柱下降
  if (++downCounter[band] % downSpeed == 0)
  {
    dJump[band]--;
    downCounter[band] = 0;
  }
  if (uJump[band] > dJump[band])
    dJump[band] = uJump[band];
  else
    val = dJump[band];

    // 顶点跳点处理
  if (++pointCounter[band] % pointDownSpeed == 0)
  {
    pointJump[band]--;
    pointCounter[band] = 0;
  }
  if (uJump[band] > pointJump[band])
    pointJump[band] = uJump[band];

    // 安全限制
  uJump[band]     = constrain(uJump[band], 0, NUM_LEDS);
  dJump[band]     = constrain(dJump[band], 0, NUM_LEDS);
  pointJump[band] = constrain(pointJump[band], 0, NUM_LEDS - 1);

    // 渲染
  int pointIdx = pointJump[band];
  if (glow_mode == "8")
  {
    fill_rainbow(leds, val, gHue, 8);
    fill_solid(leds + pointIdx, 1, val > 0 ? CRGB::White : CRGB::Black);
  }
  else if (glow_mode == "9")
  {
    fill_rainbow(leds, val, gHue, 8);
    CRGB peakColor = val > 0 ? leds[val - 1] : CRGB::Black;
    fill_solid(leds + pointIdx, 1, peakColor);
  }
  else if (glow_mode == "10")
  {
    fill_solid(leds, val, CRGB(red, green, blue));
    fill_solid(leds + pointIdx, 1, val > 0 ? CRGB(red, green, blue) : CRGB::Black);
  }
}

//+++++++++++++++++++++++++++++++++++++++++++++//setup及loop循环+++++++++++++++++++++++++++++++++++++++++++++++
void setup()
{
  Serial.begin(115200);  // 启动串口通讯
  initKalman();
  initI2S     ();        // I2S configuration for built-in ADC，配置I2S ADC
  xMutexBufferSwitch = xSemaphoreCreateMutex();
  /*双缓冲区（Double Buffer）场景，一个缓冲区用于数据写入，另一个用于读取。切换缓冲区时需确保原子性操作
  获取（Take）	任务调用 xSemaphoreTake(xMutex, timeout)，若互斥量可用则获取，否则阻塞等待。
  释放（Give）	任务调用 xSemaphoreGive(xMutex)，释放互斥量供其他任务使用。
  */
  xTaskCreatePinnedToCore(sampleFFTTask,"SampleFFTTask", 4096 , NULL,   1  ,&xSampleFFTTaskHandle,   0 );
  //                         任务函数       任务名称      栈大小  参数  优先级        任务句柄        Core 0
  connectWiFi();                           // 连接WiFi
  delay(500);                              // 稳定性等待,setup函数只执行一次，所以可以用delay，loop中要用非阻塞延迟
  connectMQTT();                           // 连接WiFi
  delay(1000);                             // 稳定性等待
  loadSettings ();                         // 加载关机前的灯光状态                                                     
  pinMode (BUTTON_PIN, INPUT_PULLUP);      // 按钮输入，启用内部上拉
  FastLED.addLeds<LED_TYPE, LOW_DATA_PIN, COLOR_ORDER>(leds_Low, NUM_LEDS);    //初始化灯带，
  FastLED.addLeds<LED_TYPE, HIGH_DATA_PIN, COLOR_ORDER>(leds_High, NUM_LEDS);  //初始化灯带
  FastLED.setBrightness(brightness);
  FastLED.show();
  saveSettings();      // 存储按钮改变的灯光模式
}

void loop()
{
  button_Function();                               // 执行按钮信息判断
  light_Mode();                                    // 判断更新灯光状态

 if (WiFi.status() != WL_CONNECTED) {              // 判断wifi状态进行重连
    static unsigned long lastAttemptTime = 0; 
    if (millis() - lastAttemptTime > 10000) {      // 每10秒尝试一次
      Serial.println("WiFi断开，尝试重新连接...");
      connectWiFi();
      lastAttemptTime = millis();
    }
  }

  if (!mqtt.connected())                          // 若mqtt没有连接成功，则重复进行连接
  { 
    connectMQTT();
  }
  mqtt.loop();                                    // 处理消息以及保持心跳
}