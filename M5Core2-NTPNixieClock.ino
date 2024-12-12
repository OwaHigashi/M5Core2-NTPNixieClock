#include <M5Core2.h>
#include <WiFi.h>
#include <time.h>
#include "image.h"
#include "GDTouchKeyboard.h"

// for SD-Updater
#define SDU_ENABLE_GZ
#include <M5StackUpdater.h>

String ssid;
String password;
const char* ntpServer = "ntp.nict.jp";
const long  gmtOffset_sec = 9 * 3600;
const int   daylightOffset_sec = 0;
WiFiClient  client;

using namespace std;

struct tm timeinfo;
uint8_t secLastReport = 255; // 初期値を255などにして、最初必ず更新
bool gotNTP = false;       // NTP取得が成功した場合true
bool rtcUpdated = false;   // RTCにNTP時刻を同期したかどうか

RTC_TimeTypeDef RTCtime;  
RTC_DateTypeDef RTCdate;

#ifdef IMAGE_FROM_SD
const char* image_data[17] = {
  "/0.jpg", "/1.jpg", "/2.jpg", "/3.jpg", "/4.jpg",
  "/5.jpg", "/6.jpg", "/7.jpg", "/8.jpg", "/9.jpg",
  "/sun.jpg", "/mon.jpg", "/tue.jpg", "/wed.jpg", "/thu.jpg", "/fri.jpg", "/sat.jpg"
};
#else
const unsigned char* image_data[17] = {
  image0, image1, image2, image3, image4,
  image5, image6, image7, image8, image9,
  sun, mon, tue, wed, thu, fri, sat
};

const uint32_t image_size[17] = {
  sizeof image0, sizeof image1, sizeof image2, sizeof image3, sizeof image4,
  sizeof image5, sizeof image6, sizeof image7, sizeof image8, sizeof image9,
  sizeof sun, sizeof mon, sizeof tue, sizeof wed, sizeof thu, sizeof fri, sizeof sat
};
#endif

// 数字・曜日画像表示関数
void PutJpg(uint16_t x, uint16_t y, uint16_t number) {
#ifdef IMAGE_FROM_SD
  M5.Lcd.drawJpgFile(SD, image_data[number], x, y);
#else
  M5.Lcd.drawJpg(image_data[number], image_size[number], x, y);
#endif
}

void PutNum(uint16_t x, uint16_t y, uint16_t x_offset, uint8_t digit, uint16_t number) {
  int temp = number;
  for (int loop = digit; loop > 0; loop--) {
    PutJpg(x + x_offset * (digit - loop), y, temp / (int)pow(10, (loop - 1)));
    temp = temp % (int)pow(10, (loop - 1));
  }
}

// ユーザー入力値で数値を更新(空なら変更なし)
void updateValueFromInput(String inputStr, uint16_t &num) {
  inputStr.trim();
  if (inputStr.length() > 0) {
    Serial.print("Length:");
    Serial.println(inputStr.length());
    Serial.println(inputStr);
    num = (uint16_t)inputStr.toInt();
  }
}

void updateValueFromInput(String inputStr, uint8_t &num) {
  inputStr.trim();
  if (inputStr.length() > 0) {
    Serial.print("Length:");
    Serial.println(inputStr.length());
    Serial.println(inputStr);
    num = (uint8_t)inputStr.toInt();
  }
}

// 曜日計算関数(Zellerの公式)
// 0:日曜,1:月曜,...6:土曜を返す
int calcWeekday(int year, int month, int day) {
  // ここでのyearは4桁の西暦年
  if (month < 3) {
    month += 12;
    year -= 1;
  }
  int k = year % 100;
  int j = year / 100;
  int h = (day + (13 * (month + 1))/5 + k + k/4 + j/4 + 5*j) % 7;
  // Zellerの公式でh=0が土曜、h=1が日曜,...
  // h=1(日)を0に合わせるため(h+6)%7
  return (h + 6) % 7; // 0=日曜日
}

// RTC時刻をユーザーに確認・修正する関数
void confirmAndAdjustRTC() {
  M5.Rtc.GetDate(&RTCdate);
  M5.Rtc.GetTime(&RTCtime);

  // RTCが未設定(0値)なら有効な初期値を設定 (2024/1/1など)
  if (RTCdate.Year == 0 && RTCdate.Month == 0 && RTCdate.Date == 0) {
    // たとえば2024年を初期値として設定
    RTCdate.Year = (uint8_t)(2024 - 2000);   
    RTCdate.Month = 1;
    RTCdate.Date = 1;
    RTCtime.Hours = 0;
    RTCtime.Minutes = 0;
    RTCtime.Seconds = 0;
    M5.Rtc.SetDate(&RTCdate);
    M5.Rtc.SetTime(&RTCtime);
  }

  // 現在のRTC値取得(ここでは4桁年に変換して表示)
  uint16_t year = RTCdate.Year;
  uint8_t month = RTCdate.Month;
  uint8_t day = RTCdate.Date;
  uint8_t hour = RTCtime.Hours;
  uint8_t minute = RTCtime.Minutes;
  uint8_t second = RTCtime.Seconds;

  // 現在のRTC値表示
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.printf("Current RTC Date/Time:\n");
  M5.Lcd.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                year, month, day,
                hour, minute, second);
  M5.Lcd.printf("Adjust if needed...");
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                year, month, day,
                hour, minute, second);
  delay(2000);

  String yearStr = GDTK.run("Year(xxxx):", String(year), true);
  Serial.println(yearStr);
  String monthStr = GDTK.run("Month(1-12)", String(month), true);
  Serial.println(monthStr);
  String dayStr = GDTK.run("Day(1-31)", String(day), true);
  Serial.println(dayStr);
  String hourStr = GDTK.run("Hour(0-23)", String(hour), true);
  Serial.println(hourStr);
  String minuteStr = GDTK.run("Minute(0-59)", String(minute), true);
  Serial.println(minuteStr);
  String secondStr = GDTK.run("Second(0-59/", String(second), true);
  Serial.println(secondStr);

  updateValueFromInput(yearStr, year);
  updateValueFromInput(monthStr, month);
  updateValueFromInput(dayStr, day);
  updateValueFromInput(hourStr, hour);
  updateValueFromInput(minuteStr, minute);
  updateValueFromInput(secondStr, second);

  // 入力がなかった場合は元の値を維持。その結果0が残る場合は補正
  if (year == 0) year = (RTCdate.Year); 
  if (month == 0) month = 1;
  if (day == 0) day = 1;

  // RTCへ書き込む際はyearから2000を引いた値を格納
  RTCdate.Year = year;
  RTCdate.Month = month;
  RTCdate.Date = day;
  RTCtime.Hours = hour;
  RTCtime.Minutes = minute;
  RTCtime.Seconds = second;

  M5.Rtc.SetDate(&RTCdate);
  M5.Rtc.SetTime(&RTCtime);

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.printf("RTC Updated to:\n");
  M5.Lcd.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                (RTCdate.Year), RTCdate.Month, RTCdate.Date,
                RTCtime.Hours, RTCtime.Minutes, RTCtime.Seconds);
  delay(2000);
}

// NTP取得関数
int ntp() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setCursor(0, 10);

  ssid.trim();
  password.trim();
  M5.Lcd.printf("Connecting to %s\n", ssid.c_str());
  Serial.printf("Attempting WiFi: SSID=%s, PASS=%s\n", ssid.c_str(), password.c_str());

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint8_t wifi_retry_cnt = 20;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.printf("*");
    if (--wifi_retry_cnt == 0) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      M5.Lcd.printf("\nCONNECTION FAIL\n");
      Serial.println("CONNECTION FAIL");
      return false;
    }
  }

  M5.Lcd.printf("\nCONNECTED\n");
  Serial.println("WiFi CONNECTED!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(&timeinfo)) {
    M5.Lcd.printf("\nFailed to obtain time\n");
    Serial.println("Failed to obtain time from NTP");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  M5.Lcd.fillScreen(TFT_BLACK);
  return true;
}

void setup() {
  M5.begin();
  Serial.begin(115200);

  // for SD-Updater
  checkSDUpdater( SD, MENU_BIN, 5000 );

  Serial.println("Setup start...");

  // ユーザー入力(SSID/PW)とNTP取得試行
  ssid = GDTK.run("Wi-Fi SSID:");
  password = GDTK.run("Wi-Fi Password:");
  
  if (ntp()) {
    Serial.println("NTP time obtained successfully!");
    gotNTP = true;
    // 初回のみRTCをNTP時刻に同期
    if (!rtcUpdated) {
      int ntpYear = timeinfo.tm_year + 1900; // NTPは4桁年が取得可能
      RTCdate.Year = (uint8_t)(ntpYear - 2000);
      RTCdate.Month = (uint8_t)(timeinfo.tm_mon + 1);
      RTCdate.Date = (uint8_t)timeinfo.tm_mday;

      RTCtime.Hours = (uint8_t)timeinfo.tm_hour;
      RTCtime.Minutes = (uint8_t)timeinfo.tm_min;
      RTCtime.Seconds = (uint8_t)timeinfo.tm_sec;

      M5.Rtc.SetDate(&RTCdate);
      M5.Rtc.SetTime(&RTCtime);
      rtcUpdated = true;
    }
  } else {
    // NTP取得失敗時はRTC調整
    gotNTP = false;
    confirmAndAdjustRTC();
  }

  // 固定表示部分を描画（以後ループで再描画しない）
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextDatum(TL_DATUM);

  // "DATE"
  M5.Lcd.setCursor(0, 14);
  M5.Lcd.printf("DATE");

  // "TIME"
  M5.Lcd.setCursor(0, 136);
  M5.Lcd.printf("TIME");

  // "NTP Clock" or "RTC Clock"
  M5.Lcd.setCursor(76, 14);
  if (gotNTP) {
    M5.Lcd.printf("NTP Clock");
  } else {
    M5.Lcd.printf("RTC Clock");
  }
}

void loop() {
  if (gotNTP) {
    getLocalTime(&timeinfo);
    // 毎日午前2時にNTP再取得
    if ((timeinfo.tm_hour == 2) && (timeinfo.tm_min == 0) && (timeinfo.tm_sec == 0)) {
      if (!ntp()) {
        gotNTP = false;
      } 
    }
  } else {
    // RTCから取得 (内部はオフセット年、使用時は2000年加算)
    M5.Rtc.GetDate(&RTCdate);
    M5.Rtc.GetTime(&RTCtime);
  }

  uint8_t currentSec = gotNTP ? timeinfo.tm_sec : RTCtime.Seconds;
  if (secLastReport != currentSec) {
    secLastReport = currentSec;
    delay(10);

    int year, month, day, hour, minute, second;
    int wday;
    if (gotNTP) {
      year = timeinfo.tm_year + 1900;
      month = timeinfo.tm_mon + 1;
      day = timeinfo.tm_mday;
      hour = timeinfo.tm_hour;
      minute = timeinfo.tm_min;
      second = timeinfo.tm_sec;
      wday = timeinfo.tm_wday; // 0=日曜
    } else {
      year = RTCdate.Year + 2000;
      month = RTCdate.Month;
      day = RTCdate.Date;
      hour = RTCtime.Hours;
      minute = RTCtime.Minutes;
      second = RTCtime.Seconds;
      wday = calcWeekday(year, month, day); // 0=日曜
    }

    // 数値表示を再描画（上書き）
    PutNum(0, 36, 52, 2, month);
    PutNum(108, 36, 52, 2, day);
    PutJpg(216, 63, wday + 10);
    PutNum(0, 156, 52, 2, hour);
    PutNum(108, 156, 52, 2, minute);
    PutNum(216, 156, 52, 2, second);
  }

  delay(100);
}
