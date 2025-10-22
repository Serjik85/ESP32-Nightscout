/*
  GlucoTrack ESP32-C3 + SSD1306 (SDA=6, SCL=7)
  Источник данных задаётся в портале в текстовом поле src: libre | night
  Кнопки:
    - NEXT  = GPIO4  (на экране Dose: +0.5U, долго — сброс)
    - ACT   = GPIO5  (коротко — следующий экран)
  Секретный вход в портал: удерживать обе кнопки ≥2 сек (покажется экран-подсказка).
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "mbedtls/sha256.h"
#include <time.h>
#include <strings.h>

// ---------- ДИСПЛЕЙ ----------
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
#define I2C_SDA 6
#define I2C_SCL 7
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
bool oledOk=false;

// ---------- КНОПКИ ----------
#define BTN_NEXT   4
#define BTN_ACTION 5
enum BtnEvent { BE_NONE, BE_SHORT, BE_LONG };
struct BtnState { uint8_t pin; bool prevHigh=true; uint32_t downAt=0; bool longFired=false; };
const uint32_t LONG_MS=700, LONG_BOTH_PORTAL_MS=2000;
BtnState bNext{BTN_NEXT}, bAct{BTN_ACTION};

BtnEvent pollBtn(BtnState &b){
  bool nowHigh=digitalRead(b.pin); uint32_t t=millis();
  if(b.prevHigh && !nowHigh){ b.downAt=t; b.longFired=false; b.prevHigh=nowHigh; return BE_NONE; }
  if(!b.prevHigh && !nowHigh){
    if(!b.longFired && (t-b.downAt>=LONG_MS)){ b.longFired=true; return BE_LONG; }
    return BE_NONE;
  }
  if(!b.prevHigh && nowHigh){
    b.prevHigh=nowHigh;
    if(!b.longFired && (t-b.downAt<LONG_MS)) return BE_SHORT;
    return BE_NONE;
  }
  b.prevHigh=nowHigh; return BE_NONE;
}
void reinitButtons(){
  pinMode(BTN_NEXT,INPUT_PULLUP); pinMode(BTN_ACTION,INPUT_PULLUP);
  bNext.prevHigh=digitalRead(BTN_NEXT); bAct.prevHigh=digitalRead(BTN_ACTION);
  bNext.downAt=bAct.downAt=0; bNext.longFired=bAct.longFired=false;
}

// ---------- WiFiManager (портал) ----------
WiFiManager wm;
WiFiManagerParameter p_email("lle","Libre email","",64);
WiFiManagerParameter p_pass ("llp","Libre password","",64);
WiFiManagerParameter p_api  ("api","API base","https://api.libreview.io",64);
WiFiManagerParameter p_ns   ("nsu","Nightscout URL (optional)","",96);
WiFiManagerParameter p_nss  ("nss","NS API-SECRET","",64);
WiFiManagerParameter p_tzo  ("tzo","TZ offset minutes (e.g. 120)","0",8);
// ВАЖНО: стандартный параметр для источника — вводить: libre или night
//WiFiManagerParameter p_src  ("src","Data source (libre|night)","libre",12);
WiFiManagerParameter p_src_hidden("src","", "libre", 12);  // скрытое поле, именно его читаем
WiFiManagerParameter* p_src_custom = nullptr;              // HTML-блок с <select>

Preferences prefs;
String confEmail, confPass, confApi, confNsUrl, confNsSecret;
String confSource="libre"; // libre|night
int tzOffsetMin=0;

// ---------- LibreLinkUp ----------
String token, userId, accountIdSha;

// ---------- ДАННЫЕ ----------
float  lastMmol=NAN;
int    lastTrend=4;
String lastTs="--:--";
uint32_t lastPoll=0;
const uint32_t POLL_MS=30000;
float doseUnits=0.0f;

// ---------- УТИЛИТЫ ----------
String sha256Hex(const String &s){
  uint8_t out[32];
  mbedtls_sha256((const unsigned char*)s.c_str(), s.length(), out, 0);
  static const char HEXCHARS[]="0123456789abcdef";
  String h; h.reserve(64);
  for(int i=0;i<32;i++){ h+=HEXCHARS[(out[i]>>4)&0xF]; h+=HEXCHARS[out[i]&0xF]; }
  return h;
}
String normalizeNsUrl(String s){
  s.trim();
  if(!s.length()) return s;
  if(!s.startsWith("http://") && !s.startsWith("https://")) s="https://"+s;
  while(s.endsWith("/")) s.remove(s.length()-1);
  return s;
}
String trendAscii(int t){
  switch(t){ case 1:return "^^"; case 2:return "^ "; case 3:return "/^";
             case 4:return "->"; case 5:return "v\\"; case 6:return " v"; case 7:return "vv";
             default:return "--"; }
}
int nsDirToCode(const String& dir){
  String d=dir; d.toLowerCase();
  if(d=="doubleup")return 1; if(d=="singleup")return 2; if(d=="fortyfiveup")return 3;
  if(d=="flat")return 4; if(d=="fortyfivedown")return 5; if(d=="singledown")return 6; if(d=="doubledown")return 7;
  return 4;
}
time_t parseIso(const String& iso){ int Y,M,D,h,m,s;
  if(sscanf(iso.c_str(),"%d-%d-%dT%d:%d:%d",&Y,&M,&D,&h,&m,&s)==6){
    struct tm t{}; t.tm_year=Y-1900; t.tm_mon=M-1; t.tm_mday=D; t.tm_hour=h; t.tm_min=m; t.tm_sec=s; return mktime(&t);
  } return 0;
}
time_t parseLibreTs(const String& ts){ int mo,da,yr,hh,mm,ss; char ap[3]={0};
  if(sscanf(ts.c_str(),"%d/%d/%d %d:%d:%d %2s",&mo,&da,&yr,&hh,&mm,&ss,ap)==7){
    if(strcasecmp(ap,"PM")==0 && hh!=12) hh+=12;
    if(strcasecmp(ap,"AM")==0 && hh==12) hh=0;
    struct tm t{}; t.tm_year=yr-1900; t.tm_mon=mo-1; t.tm_mday=da; t.tm_hour=hh; t.tm_min=mm; t.tm_sec=ss; return mktime(&t);
  } return 0;
}
String localHHMM(time_t utc,int offMin){
  if(utc<=0) return "--:--";
  utc+=offMin*60; struct tm tt; gmtime_r(&utc,&tt);
  char b[6]; snprintf(b,sizeof(b),"%02d:%02d",tt.tm_hour,tt.tm_min); return String(b);
}

// ---------- HTTP ----------
bool httpPOST(const String& url,const String& body,String& resp){
  WiFiClientSecure cli; cli.setInsecure(); HTTPClient http;
  if(!http.begin(cli,url)) return false;
  http.addHeader("accept","application/json");
  http.addHeader("content-type","application/json");
  http.addHeader("product","llu.android");
  http.addHeader("version","4.16.0");
  int code=http.POST(body); resp=http.getString(); http.end(); return (code>=200 && code<300);
}
// --- LibreLinkUp GET ---
bool httpGET_libre(const String& url, String& resp, bool auth=true, bool addAcc=false) {
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient https;
  if(!https.begin(cli, url)) return false;

  https.addHeader("accept", "application/json");
  https.addHeader("product", "llu.android");
  https.addHeader("version", "4.16.0");

  if (auth) https.addHeader("Authorization", "Bearer " + token);
  if (addAcc && accountIdSha.length()) https.addHeader("account-id", accountIdSha);

  int code = https.GET();
  resp = https.getString();
  https.end();

  // если сервер вернул 401 — токен истёк
  if (code == 401 || code == 403) {
    Serial.println("[Libre] Token expired or unauthorized");
    token = "";
    return false;
  }

  return (code >= 200 && code < 300);
}
// --- Nightscout GET (только для nightGetLatest) ---
bool httpGET_plain(const String& url, String& resp) {
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient https;
  if(!https.begin(cli, url)) return false;

  // Nightscout требует api-secret, Libre — нет
  if (confNsSecret.length()) {
    https.addHeader("api-secret", confNsSecret);
  }

  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.addHeader("accept", "application/json");

  int code = https.GET();
  resp = https.getString();
  https.end();

  if (code < 200 || code >= 300) {
    Serial.printf("[Nightscout] HTTP error %d\n", code);
  }

  return (code >= 200 && code < 300);
}

// ---------- LIBRELINKUP ----------
bool libreLogin(){
  if(confEmail.isEmpty()||confPass.isEmpty()) return false;
  String url=confApi+"/llu/auth/login";
  StaticJsonDocument<256> d; d["email"]=confEmail; d["password"]=confPass;
  String body; serializeJson(d,body); String resp;
  if(!httpPOST(url,body,resp)) return false;
  StaticJsonDocument<4096> doc; if(deserializeJson(doc,resp)) return false;
  if((int)doc["status"]!=0) return false;
  token =(const char*)doc["data"]["authTicket"]["token"];
  userId=(const char*)doc["data"]["user"]["id"];
  accountIdSha=sha256Hex(userId);
  return token.length();
}
bool libreGetLatest(float &mmol,int &trend,String &ts){
  String resp; if(!httpGET_libre(confApi+"/llu/connections",resp,true,true)) return false;
  StaticJsonDocument<16384> doc; if(deserializeJson(doc,resp)) return false;
  JsonArray arr=doc["data"].as<JsonArray>(); if(!arr||!arr.size()) return false;
  mmol =arr[0]["glucoseMeasurement"]["Value"]|NAN;
  trend=arr[0]["glucoseMeasurement"]["TrendArrow"]|4;
  const char* tsc=arr[0]["glucoseMeasurement"]["Timestamp"]|""; if(isnan(mmol)) return false;
  ts=localHHMM(parseLibreTs(String(tsc)),tzOffsetMin); return true;
}

// ---------- NIGHTSCOUT ----------
bool nightGetLatest(float &mmol,int &trend,String &ts){
  if(confNsUrl.isEmpty()) return false;

  String base = normalizeNsUrl(confNsUrl);
  if(!base.length()) return false;

  // как в рабочем коде: entries.json?count=1
  String url = base + "/api/v1/entries.json?count=1";

  String resp;
  if(!httpGET_plain(url, resp)) return false;

  // разбор как в твоём рабочем коде — берём sgv и direction
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, resp);
  if (err || !doc.is<JsonArray>() || doc.as<JsonArray>().size()==0) return false;

  JsonObject v = doc[0];
  float sgv = v["sgv"] | NAN;
  String direction = v["direction"] | "Flat";
  String iso = v["dateString"] | "";

  if (isnan(sgv)) return false;

  mmol  = sgv / 18.0f;          // перевод из mg/dL
  trend = nsDirToCode(direction);
  ts    = localHHMM(parseIso(iso), tzOffsetMin);

  return true;
}
bool nsPostBolus(float units){
  if(confNsUrl.isEmpty()||units<=0) return false;
  String url=normalizeNsUrl(confNsUrl)+"/api/v1/treatments";
  WiFiClientSecure cli; cli.setInsecure(); HTTPClient http;
  if(!http.begin(cli,url)) return false;
  http.addHeader("Content-Type","application/json");
  if(confNsSecret.length()) http.addHeader("API-SECRET",confNsSecret);
  StaticJsonDocument<256> d; d["eventType"]="Insulin Bolus"; d["insulin"]=units; d["created_at"]="now";
  String body; serializeJson(d,body); int code=http.POST(body); http.end(); return (code>=200 && code<300);
}

// ---------- ЭКРАНЫ ----------
enum Screen{ SC_GLU=0, SC_DOSE, SC_LOG, SC_LAST };
Screen screen=SC_GLU;

void drawHeader(){
  if(!oledOk) return;
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setCursor(0,0);
  display.print(confSource=="night" ? "NS " : "LL ");
  char g[8]; if(isnan(lastMmol)) snprintf(g,sizeof(g),"--.-"); else snprintf(g,sizeof(g),"%.1f",lastMmol);
  display.print(g); display.print(" "); display.print(trendAscii(lastTrend));
  display.setCursor(84,0); display.print(lastTs);
}
void drawGlucose(){
  display.clearDisplay(); drawHeader();
  display.setTextSize(3);
  char num[16]; if(isnan(lastMmol)) snprintf(num,sizeof(num),"--.-"); else snprintf(num,sizeof(num),"%.1f",lastMmol);
  int16_t x1,y1; uint16_t w,h; display.getTextBounds(num,0,0,&x1,&y1,&w,&h);
  display.setCursor((OLED_W-w)/2,22); display.print(num);
  display.setTextSize(1); display.setCursor((OLED_W-36)/2,56); display.print("mmol/L");
  display.display();
}
void drawDose(){
  display.clearDisplay(); drawHeader();
  display.setTextSize(1); display.setCursor(0,16); display.println("Dose");
  display.setTextSize(4); display.setCursor(0,30);
  char b[12]; dtostrf(doseUnits,0,1,b); display.print(b); display.print("U");
  display.setTextSize(1); display.setCursor(0,56); display.print("NEXT:+0.5  LONG:reset");
  display.display();
}
void drawLog(){
  display.clearDisplay(); drawHeader();
  display.setTextSize(1); display.setCursor(0,16); display.println("Send bolus to NS?");
  display.setTextSize(2); display.setCursor(0,34);
  char b[12]; dtostrf(doseUnits,0,1,b); display.print(b); display.print("U");
  display.setTextSize(1); display.setCursor(0,56); display.print("ACT=Next  NEXT:+0.5");
  display.display();
}
void drawStatus(const String& a,const String& b="",const String& c=""){
  if(!oledOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setCursor(0,0);
  display.print(confSource=="night" ? "NS " : "LL ");
  char g[8]; if(isnan(lastMmol)) snprintf(g,sizeof(g),"--.-"); else snprintf(g,sizeof(g),"%.1f",lastMmol);
  display.print(g); display.print(" "); display.print(trendAscii(lastTrend));
  display.setCursor(84,0); display.print(lastTs);
  display.setTextSize(1); display.setCursor(0,16); display.println(a);
  display.setTextSize(2); display.setCursor(0,32); display.println(b);
  display.setTextSize(1); display.setCursor(0,54); display.println(c);
  display.display();
}

// --- Экран-подсказка при удержании обеих кнопок ---
void drawSetupHint(uint32_t heldMs){
  if(!oledOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0,0);
  display.print(confSource=="night" ? "NS " : "LL ");
  char g[8]; if(isnan(lastMmol)) snprintf(g,sizeof(g),"--.-"); else snprintf(g,sizeof(g),"%.1f",lastMmol);
  display.print(g); display.print(" "); display.print(trendAscii(lastTrend));
  display.setCursor(84,0); display.print(lastTs);
  display.setCursor(0,16); display.println("SETUP MODE");
  display.setCursor(0,26); display.println("Hold BOTH buttons ~2s");
  display.setCursor(0,36); display.println("Connect to AP:");
  display.setCursor(0,46); display.println("GlucoTrack_Setup");
  display.setCursor(0,56); display.println("Open: 192.168.4.1");
  int w = map((int)heldMs, 0, (int)LONG_BOTH_PORTAL_MS, 0, (int)OLED_W);
  if(w<0) w=0; if(w>(int)OLED_W) w=OLED_W;
  display.drawRect(0, 14, OLED_W, 1, SSD1306_WHITE);
  display.fillRect(0, 14, w, 1, SSD1306_WHITE);
  display.display();
}

// ---------- SPLASH/ANIM ----------
bool bootSplashShown = false;
uint32_t animTick = 0;
uint8_t animPhase = 0;
const uint32_t ANIM_MS = 220;

void drawCenteredText(int16_t y, const char* text, uint8_t size=2) {
  if(!oledOk) return;
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (OLED_W - w) / 2;
  display.setCursor(x < 0 ? 0 : x, y);
  display.print(text);
}

void drawSplashFrame(const char* subtitle) {
  if(!oledOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(12, "GlucoTrack", 2);

  // подзаголовок
  display.setTextSize(1);
  drawCenteredText(38, subtitle, 1);

  // 4-фазная точка-анимация
  const int baseX = (OLED_W - 24)/2; // три маленьких квадрата
  for(uint8_t i=0;i<3;i++){
    uint8_t b = (animPhase%4 == i) ? 8 : 2;
    display.fillRect(baseX + i*12, 52, b, b, SSD1306_WHITE);
  }
  display.display();
}

void updateConnectingAnim(const char* subtitle){
  if(!oledOk) return;
  uint32_t now = millis();
  if(now - animTick >= ANIM_MS){
    animTick = now;
    animPhase = (animPhase + 1) % 4;
    drawSplashFrame(subtitle);
  }
}

// ---------- СБРОС/ОПРОС ----------
void clearData(){
  lastMmol=NAN; lastTrend=4; lastTs="--:--";
}
bool fetchOnce(){
  float mm; int tr; String ts;
  bool ok=(confSource=="night")? nightGetLatest(mm,tr,ts) : libreGetLatest(mm,tr,ts);
  if(ok){ lastMmol=mm; lastTrend=tr; lastTs=ts; return true; }
  clearData(); return false;
}

// ---------- КОНФИГ ----------
void saveConfig(){
  prefs.begin("gluco",false);
  prefs.putString("lle",confEmail);
  prefs.putString("llp",confPass);
  prefs.putString("api",confApi);
  prefs.putString("nsu",confNsUrl);
  prefs.putString("nss",confNsSecret);
  prefs.putString("src",confSource);
  prefs.putInt("tzo",tzOffsetMin);
  prefs.end();
}
void loadConfig(){
  prefs.begin("gluco",true);
  confEmail=prefs.getString("lle","");
  confPass =prefs.getString("llp","");
  confApi  =prefs.getString("api","https://api.libreview.io");
  confNsUrl=prefs.getString("nsu","");
  confNsSecret=prefs.getString("nss","");
  confSource=prefs.getString("src","libre"); // default libre
  tzOffsetMin=prefs.getInt("tzo",0);
  prefs.end();
}

void runPortal(){
 // правильные setValue - указываем именно МАКСИМАЛЬНУЮ длину поля
p_email.setValue(confEmail.c_str(), 64);       // <= размер из конструктора p_email
p_pass .setValue(confPass.c_str(),  64);       // <= p_pass
p_api  .setValue(confApi.c_str(),   64);       // <= p_api
p_ns   .setValue(confNsUrl.c_str(), 96);       // <= p_ns
p_nss  .setValue(confNsSecret.c_str(), 64);    // <= p_nss

char buf[12];
snprintf(buf, sizeof(buf), "%d", tzOffsetMin);
p_tzo.setValue(buf, 8);                        // <= p_tzo

p_src_hidden.setValue(confSource.c_str(), 12); // <= p_src_hidden

  // HTML выпадающий список
  static char src_html[512];
  const bool isLibre = (confSource == "libre");
  const bool isNight = (confSource == "night");
  snprintf(src_html, sizeof(src_html),
    "<br/><label for='src_select'>Data source</label>"
    "<select id='src_select' onchange='document.getElementById(\"src\").value=this.value;'>"
      "<option value='libre' %s>LibreLinkUp</option>"
      "<option value='night' %s>Nightscout</option>"
    "</select>"
    "<input type='hidden' id='src' name='src' value='%s'>",
    isLibre ? "selected" : "",
    isNight ? "selected" : "",
    confSource.c_str()
  );
  static WiFiManagerParameter src_custom_block(src_html);
  p_src_custom = &src_custom_block;

  // локальный менеджер (чтобы не копились параметры)
  WiFiManager wmLocal;
  wmLocal.addParameter(&p_email);
  wmLocal.addParameter(&p_pass);
  wmLocal.addParameter(&p_api);
  wmLocal.addParameter(&p_ns);
  wmLocal.addParameter(&p_nss);
  wmLocal.addParameter(&p_tzo);
  wmLocal.addParameter(p_src_custom);      // сначала HTML-блок
  wmLocal.addParameter(&p_src_hidden);     // затем скрытое поле

  wmLocal.setConfigPortalBlocking(true);
  wmLocal.setConfigPortalTimeout(0);
  wmLocal.setConnectTimeout(0);
  wmLocal.setBreakAfterConfig(true);

  WiFi.mode(WIFI_AP_STA);
  wmLocal.startConfigPortal("GlucoTrack_Setup");

  String prevSource = confSource;

  // читаем сохранённые значения
  confEmail     = p_email.getValue();
  { String pw   = p_pass.getValue(); if(pw.length()) confPass = pw; }
  confApi       = p_api.getValue();
  confNsUrl     = normalizeNsUrl(p_ns.getValue());
  confNsSecret  = p_nss.getValue();
  tzOffsetMin   = atoi(p_tzo.getValue());
  confSource    = String(p_src_hidden.getValue()); // выбор из <select>
  confSource.toLowerCase();
  if(confSource!="libre" && confSource!="night") confSource="libre";

  wmLocal.stopConfigPortal();
  delay(50);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  uint32_t t0=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(100); yield(); }

  if(confSource != prevSource){ lastMmol=NAN; lastTrend=4; lastTs="--:--"; token=""; userId=""; accountIdSha=""; }

  saveConfig();
  reinitButtons();
  screen = SC_GLU;

  // подтянуть данные и показать
  float mm; int tr; String ts;
  bool ok = (confSource=="night") ? nightGetLatest(mm,tr,ts) : libreGetLatest(mm,tr,ts);
  if(ok){ lastMmol=mm; lastTrend=tr; lastTs=ts; }
  drawGlucose();
}
// ---------- WiFi ----------
bool ensureWiFi(){
  if(WiFi.status()==WL_CONNECTED) return true;
  WiFi.begin();
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){
    updateConnectingAnim("connecting Wi-Fi...");
    delay(30);
    yield();
  }
  return WiFi.status()==WL_CONNECTED;
}

// ---------- SETUP / LOOP ----------
void setup(){
  pinMode(BTN_NEXT,INPUT_PULLUP); pinMode(BTN_ACTION,INPUT_PULLUP);
  Serial.begin(115200);
  Wire.begin(I2C_SDA,I2C_SCL);
  if(display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR)) oledOk=true;
  if(oledOk){ display.clearDisplay(); display.display(); }

  loadConfig();
  Serial.printf("[BOOT] source=%s, nsUrl=%s, api=%s\n", confSource.c_str(), confNsUrl.c_str(), confApi.c_str());

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  if(!ensureWiFi()) runPortal();

  // логинимся в LibreLinkUp только если выбран libre
  if(confSource=="libre"){
    if(!libreLogin()){
      runPortal(); ensureWiFi();
      if(confSource=="libre") libreLogin();
    }
    if(oledOk){
  drawSplashFrame("booting...");
  bootSplashShown = true;
}
  }

  // первый опрос выбранного источника
  fetchOnce();
  drawGlucose(); lastPoll=millis();
}

void loop(){
  // секретный вход в портал — обе кнопки ≥2с (подсказка+прогресс)
  static uint32_t bothAt=0; static bool both=false;
  bool n=(digitalRead(BTN_NEXT)==LOW), a=(digitalRead(BTN_ACTION)==LOW);
  if(n && a){
    if(!both){ both=true; bothAt=millis(); }
    uint32_t held = millis() - bothAt;
    drawSetupHint(held);
    if(held >= LONG_BOTH_PORTAL_MS){
      while(digitalRead(BTN_NEXT)==LOW || digitalRead(BTN_ACTION)==LOW){ delay(10); yield(); }
      drawStatus("Opening setup...","","");
      delay(200);
      runPortal();
      both=false;
      delay(150);
      return;
    }
  } else both=false;

  BtnEvent eN=pollBtn(bNext), eA=pollBtn(bAct);

  // Экран дозы/лога (как у тебя было)
  if(screen==SC_DOSE){
    if(eN==BE_SHORT){ doseUnits+=0.5f; if(doseUnits>50.0f) doseUnits=0.0f; drawDose(); }
    if(eN==BE_LONG){  doseUnits=0.0f; drawDose(); }
  }
  if(screen==SC_LOG){
    if(eN==BE_SHORT){ doseUnits+=0.5f; if(doseUnits>50.0f) doseUnits=0.0f; drawLog(); }
  }

  if(eA==BE_SHORT){
    if(screen==SC_LOG){
      if(doseUnits>0.0f && ensureWiFi() && confNsUrl.length()){
        bool sent = nsPostBolus(doseUnits);
        drawStatus(sent?"Logged to NS":"Logged (local)", String(doseUnits,1)+"U","");
        delay(800);
      }
      doseUnits=0.0f;
    }
    screen = (Screen)((screen+1)%SC_LAST);
    switch(screen){
      case SC_GLU:  drawGlucose(); break;
      case SC_DOSE: drawDose();    break;
      case SC_LOG:  drawLog();     break;
      default: break;
    }
  }

  // периодический опрос источника
  if(millis()-lastPoll>=POLL_MS){
    bool ok = fetchOnce();
    if(ok && screen==SC_GLU) drawGlucose();
    lastPoll=millis();
  }

  delay(40);
}