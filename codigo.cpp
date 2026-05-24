#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Wire.h>
#include <EEPROM.h>
#include "DHT.h"

#define DHTPIN 2
#define DHTTYPE DHT22
#define LDR_PIN A0

#define LED_R 9
#define LED_G 10
#define LED_B 11
#define BUZZER 13

#define BTN_TEMP    5
#define BTN_UMID    6
#define BTN_LUZ     7
#define BTN_WELCOME 4

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS1307 RTC;

const float TEMP_MIN = 12.0;
const float TEMP_MAX = 14.0;
const float UMID_MIN = 60.0;
const float UMID_MAX = 80.0;
const int   LUZ_MAX  = 40;

const int EEPROM_FLAG_ADDR = 0;
const int LOG_START   = 10;
const int LOG_RECORDS = 50;
const int LOG_SIZE    = 10;
int currentLogAddr = LOG_START;

enum Tela { TELA_PADRAO, TELA_TEMP, TELA_UMID, TELA_LUZ };
Tela telaAtual = TELA_PADRAO;
Tela telaAnterior = TELA_PADRAO;
unsigned long ultimaTroca = 0;
const unsigned long TEMPO_TELA = 5000;

bool welcomeAtivo = true;
int lastLoggedMinute = -1;

float gTemp = 0;
float gUmid = 0;
int   gLuz  = 0;

unsigned long ultimaLeitura = 0;

bool btnTempAnterior    = HIGH;
bool btnUmidAnterior    = HIGH;
bool btnLuzAnterior     = HIGH;
bool btnWelcomeAnterior = HIGH;

// ===== CHARS PADRÃO / BOAS-VINDAS =====
byte losangoCheio[8] = { B00100,B01110,B11111,B11111,B11111,B01110,B00100,B00000 };
byte losangoVazio[8] = { B00100,B01010,B10001,B10001,B10001,B01010,B00100,B00000 };
byte grauChar[8]     = { B01100,B10010,B10010,B01100,B00000,B00000,B00000,B00000 };

// ===== CHARS TEMPERATURA =====
byte termoFrio[8]   = { B00100,B01010,B01010,B01010,B01110,B01110,B00100,B00000 };
byte termoOk[8]     = { B00100,B01010,B01110,B01110,B01110,B01110,B00100,B00000 };
byte termoQuente[8] = { B00100,B01110,B01110,B01110,B01110,B01110,B00100,B00000 };

// ===== CHARS UMIDADE =====
byte gotaSeca[8]  = { B00100,B00100,B01010,B01010,B10001,B10001,B01110,B00000 };
byte gotaOk[8]    = { B00100,B00100,B01010,B01110,B11111,B11111,B01110,B00000 };
byte gotaCheia[8] = { B00100,B00110,B01111,B01111,B11111,B11111,B01110,B00000 };

// ===== CHARS LUZ =====
byte luzOk[8] = { B01110,B10001,B10001,B10001,B01110,B00100,B00100,B00000 };
byte luzAlerta[8]      = { B01110,B11011,B10101,B11011,B01110,B00100,B00100,B00000 };
byte luzForte[8]   = { B10101,B01110,B11111,B01110,B10101,B00100,B00000,B00000 };

// ===== RGB =====
void setRGB(int r, int g, int b) {
  analogWrite(LED_R, r);
  analogWrite(LED_G, g);
  analogWrite(LED_B, b);
}

void corVerde()    { setRGB(0, 255, 0); }
void corAmarelo()  { setRGB(255, 255, 0); }
void corVermelho() { setRGB(255, 0, 0); }
void corApagado()  { setRGB(0, 0, 0); }

// ===== DEBOUNCE =====
bool foiPressionado(int pino, bool &anterior) {
  bool atual = digitalRead(pino);
  if (atual == LOW && anterior == HIGH) {
    anterior = LOW;
    return true;
  }
  if (atual == HIGH) anterior = HIGH;
  return false;
}

// ===== RECARREGA CHARS CONFORME TELA =====
void carregarChars(Tela t) {
  switch (t) {
    case TELA_TEMP:
      lcd.createChar(3, termoFrio);
      lcd.createChar(4, termoOk);
      lcd.createChar(5, termoQuente);
      break;
    case TELA_UMID:
      lcd.createChar(3, gotaSeca);
      lcd.createChar(4, gotaOk);
      lcd.createChar(5, gotaCheia);
      break;
    case TELA_LUZ:
      lcd.createChar(3, luzOk);
      lcd.createChar(4, luzAlerta);
      lcd.createChar(5, luzForte);
      break;
    case TELA_PADRAO:
    default:
      lcd.createChar(0, losangoCheio);
      lcd.createChar(1, losangoVazio);
      lcd.createChar(2, grauChar);
      break;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(9600);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(BTN_TEMP,    INPUT_PULLUP);
  pinMode(BTN_UMID,    INPUT_PULLUP);
  pinMode(BTN_LUZ,     INPUT_PULLUP);
  pinMode(BTN_WELCOME, INPUT_PULLUP);

  dht.begin();
  Wire.begin();
  lcd.init();
  lcd.backlight();

  // Carrega chars padrão
  lcd.createChar(0, losangoCheio);
  lcd.createChar(1, losangoVazio);
  lcd.createChar(2, grauChar);

  RTC.begin();
  if (!RTC.isrunning()) RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));

  byte flag = EEPROM.read(EEPROM_FLAG_ADDR);
  if (flag == 0xFF) {
    welcomeAtivo = true;
    EEPROM.write(EEPROM_FLAG_ADDR, 1);
  } else {
    welcomeAtivo = (flag == 1);
  }

  if (welcomeAtivo) mostrarBoasVindas();
  lcd.clear();
  // Recarrega chars padrão após boas-vindas
  lcd.createChar(0, losangoCheio);
  lcd.createChar(1, losangoVazio);
  lcd.createChar(2, grauChar);
}

// ===== LOOP =====
void loop() {
  if (millis() - ultimaLeitura > 3000) {
    float t = dht.readTemperature();
    float u = dht.readHumidity();
    if (!isnan(t)) gTemp = t;
    if (!isnan(u)) gUmid = u;
    ultimaLeitura = millis();
    gLuz = map(analogRead(LDR_PIN), 0, 1023, 100, 0);
  }

  if (foiPressionado(BTN_TEMP,    btnTempAnterior))    trocarTela(TELA_TEMP);
  if (foiPressionado(BTN_UMID,    btnUmidAnterior))    trocarTela(TELA_UMID);
  if (foiPressionado(BTN_LUZ,     btnLuzAnterior))     trocarTela(TELA_LUZ);
  if (foiPressionado(BTN_WELCOME, btnWelcomeAnterior)) toggleWelcome();

  atualizarDisplay();

  if (telaAtual != TELA_PADRAO && millis() - ultimaTroca > TEMPO_TELA) {
    telaAtual = TELA_PADRAO;
    carregarChars(TELA_PADRAO);
    lcd.clear();
  }

  verificarAlertas();

  DateTime now = RTC.now();
  if (now.minute() != lastLoggedMinute) {
    if (gTemp < TEMP_MIN || gTemp > TEMP_MAX ||
        gUmid < UMID_MIN || gUmid > UMID_MAX ||
        gLuz  > LUZ_MAX) {
      gravarLog(now);
      lastLoggedMinute = now.minute();
    }
  }
}

// ===== BOAS-VINDAS =====
void mostrarBoasVindas() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.write(byte(0));
  String nome = " PRISMA ";
  for (unsigned int i = 0; i < nome.length(); i++) {
    lcd.print(nome[i]);
    delay(100);
  }
  lcd.write(byte(0));

  lcd.setCursor(0, 1);
  String msg = "Seja bem-vindo!";
  for (unsigned int i = 0; i < msg.length(); i++) {
    lcd.print(msg[i]);
    delay(80);
  }

  for (int i = 0; i < 14; i++) {
    lcd.setCursor(0, 0); lcd.write(byte(i % 2));
    lcd.setCursor(9, 0); lcd.write(byte(i % 2));
    switch (i % 4) {
      case 0: corVermelho(); break;
      case 1: setRGB(255, 100, 0); break;
      case 2: corAmarelo(); break;
      case 3: corVerde(); break;
    }
    delay(300);
  }

  lcd.clear();
  corApagado();
}

// ===== TROCAR TELA =====
void trocarTela(Tela t) {
  telaAtual = t;
  ultimaTroca = millis();
  carregarChars(t);
  lcd.clear();
}

// ===== TOGGLE WELCOME =====
void toggleWelcome() {
  welcomeAtivo = !welcomeAtivo;
  EEPROM.write(EEPROM_FLAG_ADDR, welcomeAtivo ? 1 : 0);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Logo:");
  lcd.setCursor(0, 1); lcd.print(welcomeAtivo ? "  ATIVADO" : " DESATIVADO");
  delay(1500);
  lcd.clear();
  carregarChars(telaAtual);
}

// ===== STATUS =====
String statusTemp() {
  if (gTemp < TEMP_MIN) return "BAIXA ";
  if (gTemp > TEMP_MAX) return "ALTA  ";
  return "OK    ";
}
String statusUmid() {
  if (gUmid < UMID_MIN) return "BAIXA ";
  if (gUmid > UMID_MAX) return "ALTA  ";
  return "OK    ";
}
String statusLuz() {
  if (gLuz <= LUZ_MAX)        return "OK     ";
  if (gLuz <= LUZ_MAX + 20)  return "ALERTA ";
  return "ALTA   ";
}

// ===== ÍCONE CONFORME VALOR =====
byte iconeTemp() {
  if (gTemp < TEMP_MIN) return 3;  // frio
  if (gTemp > TEMP_MAX) return 5;  // quente
  return 4;                         // ok
}
byte iconeUmid() {
  if (gUmid < UMID_MIN) return 3;  // seca
  if (gUmid > UMID_MAX) return 5;  // cheia
  return 4;                         // ok
}
byte iconeLuz() {
  if (gLuz <= LUZ_MAX)        return 3;
  if (gLuz <= LUZ_MAX + 20)  return 4;
  return 5;
}

// ===== DISPLAY =====
void atualizarDisplay() {
  switch (telaAtual) {

    case TELA_TEMP:
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(gTemp, 1);
      lcd.write(byte(2));
      lcd.print("C ");
      lcd.write(iconeTemp());   // ← ícone no final
      lcd.setCursor(0, 1);
      lcd.print("Status: ");
      lcd.print(statusTemp());
      break;

    case TELA_UMID:
      lcd.setCursor(0, 0);
      lcd.print("Umid: ");
      lcd.print(gUmid, 1);
      lcd.print("% ");
      lcd.write(iconeUmid());   // ← ícone no final
      lcd.setCursor(0, 1);
      lcd.print("Status: ");
      lcd.print(statusUmid());
      break;

    case TELA_LUZ:
      lcd.setCursor(0, 0);
      lcd.print("Luz: ");
      lcd.print(gLuz);
      lcd.print("% ");
      lcd.write(iconeLuz());    // ← ícone no final
      lcd.setCursor(0, 1);
      lcd.print("Status: ");
      lcd.print(statusLuz());
      break;

    case TELA_PADRAO:
    default:
      DateTime now = RTC.now();
      lcd.setCursor(0, 0);
      lcd.print("PRISMA ");
      if (now.hour() < 10) lcd.print("0");
      lcd.print(now.hour());
      lcd.print(":");
      if (now.minute() < 10) lcd.print("0");
      lcd.print(now.minute());
      lcd.print("   ");

      lcd.setCursor(0, 1);
      lcd.print("T:");
      lcd.print((int)gTemp);
      lcd.write(byte(2));
      lcd.print(" U:");
      lcd.print((int)gUmid);
      lcd.print("% L:");
      lcd.print(gLuz);
      lcd.print("  ");
      break;
  }
}

// ===== ALERTAS RGB =====
void verificarAlertas() {
  bool critico = (gTemp < TEMP_MIN - 2 || gTemp > TEMP_MAX + 2 ||
                  gUmid < UMID_MIN - 10 || gUmid > UMID_MAX + 10 ||
                  gLuz  > LUZ_MAX + 20);

  bool atencao = (gTemp < TEMP_MIN || gTemp > TEMP_MAX ||
                  gUmid < UMID_MIN || gUmid > UMID_MAX ||
                  gLuz  > LUZ_MAX);

  if (critico) {
    corVermelho();
    tone(BUZZER, 500);
  } else if (atencao) {
    corAmarelo();
    tone(BUZZER, 250);
    delay(300);
    noTone(BUZZER);
    corApagado();
    delay(500);
  } else {
    corVerde();
    noTone(BUZZER);
  }
}

// ===== GRAVA LOG =====
void gravarLog(DateTime now) {
  EEPROM.put(currentLogAddr,     (long)now.unixtime());
  EEPROM.put(currentLogAddr + 4, (int)(gTemp * 100));
  EEPROM.put(currentLogAddr + 6, (int)(gUmid * 100));
  EEPROM.put(currentLogAddr + 8, (int)gLuz);

  currentLogAddr += LOG_SIZE;
  if (currentLogAddr >= LOG_START + (LOG_RECORDS * LOG_SIZE))
    currentLogAddr = LOG_START;

  Serial.print(now.day());
  Serial.print("/");
  Serial.print(now.month());
  Serial.print(" ");
  Serial.print(now.hour());
  Serial.print(":");
  if (now.minute() < 10) Serial.print("0");
  Serial.print(now.minute());
  Serial.print(" ");
  Serial.print(gTemp);
  Serial.print("C ");
  Serial.print(gUmid);
  Serial.print("% L:");
  Serial.print(gLuz);
  Serial.println("%");
}