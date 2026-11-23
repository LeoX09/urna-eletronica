#include <Adafruit_LiquidCrystal.h>
#include <EEPROM.h>

Adafruit_LiquidCrystal lcd(0);

const byte LINHAS = 4;
const byte COLUNAS = 3;

char teclas[LINHAS][COLUNAS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'<', '0', 'c'}
};

byte pinosLinhas[LINHAS] = {13, 12, 11, 10};
byte pinosColunas[COLUNAS] = {9, 8, 7};
char ultimaTecla = '\0';

// Botões e buzzer
const byte BTN_CORRIGE = A1;
const byte BTN_BRANCO = A0;
const byte BTN_CONFIRMA = A2;
const int PINO_BUZZER = 2;

// LEDs
const byte LED_VERMELHO = 6;
const byte LED_AMARELO = 5;
const byte LED_VERDE = 4;

const byte pinosLed[3] = {LED_VERDE, LED_AMARELO, LED_VERMELHO};
enum ModoLed : byte { LED_OFF, LED_ON, LED_PISCA };
byte modoLed[3] = {LED_OFF, LED_OFF, LED_OFF};

const unsigned int PERIODO_PISCA = 500;

// Efeito temporário de erro
bool erroAtivo = false;
unsigned long erroInicio = 0;

// Máquina de Estados
enum Estado : byte {
  AGUARDANDO_CONFIG,
  PRONTA,
  BLOQUEADA,
  LIBERADA,
  ENCERRADA
};

Estado estado = AGUARDANDO_CONFIG;

// Modo e dígitos
const byte TAM_MAX = 6;
const byte DIGITOS_MUNICIPAL = 2;
const byte DIGITOS_FEDERAL = 4;

char modo = ' ';
byte digitos = 0;

char numero[TAM_MAX + 1] = "";
byte qtd = 0;
bool aguardandoConfirmacao = false;

// Contador em RAM
unsigned int totalVotos = 0;
unsigned int totalBrancos = 0;

const byte MAGIC = 0xA5;

struct Registro {
  byte magic;
  byte estado;
  char modo;
  byte digitos;
  uint16_t totalVotos;
  uint16_t totalBrancos;
};

bool carregarEEPROM() {
  Registro reg;
  EEPROM.get(0, reg);

  if (reg.magic != MAGIC) return false;
  if (reg.estado > ENCERRADA) return false;

  estado = (Estado)reg.estado;
  modo = reg.modo;
  digitos = reg.digitos;
  totalVotos = reg.totalVotos;
  totalBrancos = reg.totalBrancos;
  return true;
}

void salvarEEPROM() {
  Registro reg;
  reg.magic = MAGIC;
  reg.estado = (estado == LIBERADA) ? BLOQUEADA : estado;
  reg.modo = modo;
  reg.digitos = digitos;
  reg.totalVotos = totalVotos;
  reg.totalBrancos = totalBrancos;

  EEPROM.put(0, reg);
}

// Leitura do teclado
char lerTeclado() {
  char teclaAtual = '\0';

  for (byte l = 0; l < LINHAS; l++) {
    digitalWrite(pinosLinhas[l], LOW);
    for (byte c = 0; c < COLUNAS; c++) {
      if (digitalRead(pinosColunas[c]) == LOW) {
        teclaAtual = teclas[l][c];
      }
    }
    digitalWrite(pinosLinhas[l], HIGH);
  }

  if (teclaAtual != '\0' && ultimaTecla == '\0') {
    ultimaTecla = teclaAtual;
    delay(20);
    return teclaAtual;
  }

  if (teclaAtual == '\0') {
    ultimaTecla = '\0';
  }

  return '\0';
}

bool apertou(byte pino) {
  if (digitalRead(pino) == HIGH) {
    delay(30);
    while (digitalRead(pino) == HIGH);
    return true;
  }
  return false;
}

void tocarBuzzer() {
  const int freqBase = 1337;
  const int freqAlta = 1437;
  const int duracaoPulso = 90;

  for (int i = 0; i < 5; i++) {
    tone(PINO_BUZZER, freqBase);
    delay(duracaoPulso);
    tone(PINO_BUZZER, freqAlta);
    delay(duracaoPulso);
  }

  tone(PINO_BUZZER, freqBase);
  delay(120);
  noTone(PINO_BUZZER);
}

void tocarBipeIntermediario() {
  tone(PINO_BUZZER, 1200);
  delay(120);
  noTone(PINO_BUZZER);
}

void definirLeds(byte verde, byte amarelo, byte vermelho) {
  modoLed[0] = verde;
  modoLed[1] = amarelo;
  modoLed[2] = vermelho;
}

void atualizarLeds() {
  switch (estado) {
    case AGUARDANDO_CONFIG:
      definirLeds(LED_OFF, LED_PISCA, LED_OFF);
      break;
    case PRONTA:
      definirLeds(LED_OFF, LED_ON, LED_OFF);
      break;
    case BLOQUEADA:
      definirLeds(LED_OFF, LED_OFF, LED_ON);
      break;
    case LIBERADA:
      definirLeds(aguardandoConfirmacao ? LED_PISCA : LED_ON, LED_OFF, LED_OFF);
      break;
    case ENCERRADA:
      definirLeds(LED_OFF, LED_OFF, LED_PISCA);
      break;
  }
}

void aplicarLeds() {
  bool faseLigada = ((millis() / PERIODO_PISCA) % 2) == 0;

  for (byte i = 0; i < 3; i++) {
    bool ligado = (modoLed[i] == LED_ON) || (modoLed[i] == LED_PISCA && faseLigada);

    if (i == 1 && erroAtivo) {
      unsigned long t = millis() - erroInicio;
      if (t >= 400) {
        erroAtivo = false;
      } else {
        ligado = (t < 100) || (t >= 200 && t < 300);
      }
    }

    digitalWrite(pinosLed[i], ligado ? HIGH : LOW);
  }
}

void iniciarErroLed() {
  erroAtivo = true;
  erroInicio = millis();
}

const __FlashStringHelper* nomeEstado(byte e) {
  switch (e) {
    case AGUARDANDO_CONFIG: return F("AGUARDANDO_CONFIG");
    case PRONTA:            return F("PRONTA");
    case BLOQUEADA:         return F("BLOQUEADA");
    case LIBERADA:          return F("LIBERADA");
    case ENCERRADA:         return F("ENCERRADA");
  }
  return F("?");
}

void telaDigitando() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Seu voto:");
  qtd = 0;
  numero[0] = '\0';
  aguardandoConfirmacao = false;
}

void mostrarTela() {
  lcd.clear();
  lcd.setCursor(0, 0);

  switch (estado) {
    case AGUARDANDO_CONFIG:
      lcd.print("Urna Sem Modo");
      lcd.setCursor(0, 1);
      lcd.print("Aguardando..");
      break;
    case PRONTA:
      lcd.print("MODO: ");
      lcd.print(modo == 'M' ? "Municipal" : "Federal");
      lcd.setCursor(0, 1);
      lcd.print("Aguard. abertura");
      break;
    case BLOQUEADA:
      lcd.print("Aguardando");
      lcd.setCursor(0, 1);
      lcd.print("liberacao");
      break;
    case ENCERRADA:
      lcd.print("Votacao fechada");
      lcd.setCursor(0, 1);
      lcd.print("Votos: ");
      lcd.print(totalVotos);
      break;
    default:
      break;
  }
}

void setEstado(byte novo) {
  estado = (Estado)novo;
  salvarEEPROM();

  if (estado == LIBERADA) {
    telaDigitando();
  } else {
    mostrarTela();
  }

  atualizarLeds();
  aplicarLeds();

  Serial.print(F("ESTADO:"));
  Serial.println(nomeEstado(estado));
}

void erroComando() {
  Serial.print(F("ERRO: Comando Invalido no estado "));
  Serial.println(nomeEstado(estado));
  iniciarErroLed();
}

void processarComando(char c) {
  switch (c) {
    case 'M':
    case 'F':
      if (estado == AGUARDANDO_CONFIG || estado == PRONTA) {
        modo = c;
        digitos = (c == 'M') ? DIGITOS_MUNICIPAL : DIGITOS_FEDERAL;
        Serial.print(F("OK MODO:"));
        Serial.print(modo);
        Serial.print(F(" DIGITOS:"));
        Serial.println(digitos);
        setEstado(PRONTA);
      } else {
        erroComando();
      }
      break;

    case 'A':
      if (estado == PRONTA) {
        Serial.println(F("OK ABRIR"));
        setEstado(BLOQUEADA);
      } else {
        erroComando();
      }
      break;

    case 'L':
      if (estado == BLOQUEADA) {
        Serial.println(F("OK LIBERAR"));
        setEstado(LIBERADA);
      } else {
        erroComando();
      }
      break;

    case 'C':
      if (estado == LIBERADA) {
        Serial.println(F("OK CANCELAR"));
        setEstado(BLOQUEADA);
      } else {
        erroComando();
      }
      break;

    case 'E':
      if (estado == BLOQUEADA) {
        Serial.print(F("OK ENCERRAR TOTAL:"));
        Serial.print(totalVotos);
        Serial.print(F(" BRANCOS:"));
        Serial.println(totalBrancos);
        setEstado(ENCERRADA);
      } else {
        erroComando();
      }
      break;

    case 'Z':
      if (estado == ENCERRADA) {
        modo = ' ';
        digitos = 0;
        totalVotos = 0;
        totalBrancos = 0;
        Serial.println(F("OK ZERAR"));
        setEstado(AGUARDANDO_CONFIG);
      } else {
        erroComando();
      }
      break;

    case 'S':
      Serial.print(F("STATUS ESTADO:"));
      Serial.print(nomeEstado(estado));
      Serial.print(F(" MODO:"));
      Serial.print(modo);
      Serial.print(F(" DIGITOS:"));
      Serial.print(digitos);
      Serial.print(F(" VOTOS:"));
      Serial.print(totalVotos);
      Serial.print(F(" BRANCOS:"));
      Serial.println(totalBrancos);
      break;

    default:
      Serial.println(F("ERRO: Comando desconhecido"));
      iniciarErroLed();
      break;
  }
}

void tratarVoto() {
  char tecla = lerTeclado();

  if (tecla >= '0' && tecla <= '9' && qtd < digitos) {
    numero[qtd] = tecla;
    lcd.setCursor(qtd, 1);
    lcd.print(tecla);
    qtd++;
    numero[qtd] = '\0';
  }

  if (apertou(BTN_CORRIGE)) {
    telaDigitando();
    atualizarLeds();
  }

  if (apertou(BTN_BRANCO)) {
    totalVotos++;
    totalBrancos++;
    salvarEEPROM();
    Serial.println(F("VOTO OK"));
    lcd.clear();
    lcd.print("Voto em BRANCO");
    definirLeds(LED_ON, LED_OFF, LED_OFF);
    aplicarLeds();
    tocarBuzzer();
    delay(1000);
    setEstado(BLOQUEADA);
    return;
  }

  if (apertou(BTN_CONFIRMA) && qtd == digitos) {
    if (!aguardandoConfirmacao) {
      aguardandoConfirmacao = true;
      totalVotos++;
      salvarEEPROM();
      atualizarLeds();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Voto computado!");
      lcd.setCursor(0, 1);
      lcd.print("Numero: ");
      lcd.print(numero);
      definirLeds(LED_ON, LED_OFF, LED_OFF);
      aplicarLeds();
      tocarBuzzer();
      delay(1000);
      setEstado(BLOQUEADA);
    }
  }
}

void setup() {
  Serial.begin(9600);

  for (byte l = 0; l < LINHAS; l++) {
    pinMode(pinosLinhas[l], OUTPUT);
    digitalWrite(pinosLinhas[l], HIGH);
  }
  for (byte c = 0; c < COLUNAS; c++) {
    pinMode(pinosColunas[c], INPUT_PULLUP);
  }

  pinMode(BTN_CORRIGE, INPUT);
  pinMode(BTN_BRANCO, INPUT);
  pinMode(BTN_CONFIRMA, INPUT);
  pinMode(PINO_BUZZER, OUTPUT);

  for (byte i = 0; i < 3; i++) {
    pinMode(pinosLed[i], OUTPUT);
  }

  lcd.begin(16, 2);
  lcd.setBacklight(1);

  Serial.println(F("Comandos: M F A L C E Z S"));
  setEstado(AGUARDANDO_CONFIG);
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    processarComando(toupper(c));
  }

  if (estado == LIBERADA) {
    tratarVoto();
  }

  aplicarLeds();
}