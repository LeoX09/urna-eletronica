#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial bt(10, 11); // RX, TX

class SaidaDupla : public Print {
  public:
    size_t write(uint8_t b) override {
      Serial.write(b);
      return bt.write(b);
    }
};

SaidaDupla saida;

const byte LINHAS = 4;
const byte COLUNAS = 3;

char teclas[LINHAS][COLUNAS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'<', '0', 'c'}
};

// Teclado
byte pinosLinhas[LINHAS] = {3, 8, 7, 5};
byte pinosColunas[COLUNAS] = {4, 9, 6};
Keypad  teclado = Keypad(makeKeymap(teclas), pinosLinhas, pinosColunas, LINHAS, COLUNAS);

// Botões
const byte BTN_CORRIGE = A0;
const byte BTN_BRANCO = A1;
const byte BTN_CONFIRMA = A2;

// LEDs e Buzzer
const int PINO_BUZZER = 42;
const byte LED_VERMELHO = 28;
const byte LED_AMARELO = 32;
const byte LED_VERDE = 34;

const byte pinosLed[3] = {LED_VERDE, LED_AMARELO, LED_VERMELHO};
enum ModoLed : byte { LED_OFF, LED_ON, LED_PISCA };
byte modoLed[3] = {LED_OFF, LED_OFF, LED_OFF};

const unsigned int PERIODO_PISCA = 500;

bool erroAtivo = false;
unsigned long erroInicio = 0;

enum Estado : byte {
  AGUARDANDO_CONFIG,
  PRONTA,
  BLOQUEADA,
  LIBERADA,
  ENCERRADA
};

Estado estado = AGUARDANDO_CONFIG;

const byte MAX_CANDIDATOS = 38;
const byte TAM_NUM = 5;
const byte TAM_NOME = 16;
const byte MAGIC = 0xA7;

struct Cabecalho {
  byte magic;
  byte estado;
  char modo;
  byte totalCandidatos;
  uint16_t totalEleitores;
  uint16_t brancos[5];
  uint16_t nulos[5];
};

struct Candidato {
  char cargo;
  char numero[TAM_NUM + 1];
  char nome[TAM_NOME + 1];
  uint16_t votos;
};

char modo = ' ';
byte totalCandidatos = 0;
uint16_t totalEleitores = 0;
uint16_t brancos[5] = {0, 0, 0, 0, 0};
uint16_t nulos[5] = {0, 0, 0, 0, 0};

byte etapaAtual = 0;
char numero[TAM_NUM + 1] = "";
byte qtd = 0;
int8_t candidatoAtualIdx = -1;
int8_t votosPendentes[5] = {-1, -1, -1, -1, -1};

byte totalEtapas() {
  if (modo == 'M') return 2;
  if (modo == 'F') return 5;
  return 0;
}

char cargoDaEtapa(byte etapa) {
  if (modo == 'M') {
    return (etapa == 0) ? 'V' : 'P';
  }
  const char cargosFed[5] = {'F', 'E', 'S', 'G', 'R'};
  return cargosFed[etapa];
}

byte digitosDaEtapa(byte etapa) {
  char cargo = cargoDaEtapa(etapa);
  if (cargo == 'V' || cargo == 'E') return 5;
  if (cargo == 'F') return 4;
  if (cargo == 'S') return 3;
  return 2;
}

bool cargoValidoParaModo(char c) {
  if (modo == 'M') return (c == 'V' || c == 'P');
  if (modo == 'F') return (c == 'F' || c == 'E' || c == 'S' || c == 'G' || c == 'R');
  return false;
}

const __FlashStringHelper* labelCargoLCD(char cargo) {
  switch (cargo) {
    case 'V': return F("Vereador: ");
    case 'P': return F("Prefeito: ");
    case 'F': return F("Dep. Fed.: ");
    case 'E': return F("Dep. Est.: ");
    case 'S': return F("Senador: ");
    case 'G': return F("Governador: ");
    case 'R': return F("Presidente: ");
  }
  return F("Votos: ");
}

byte colunaInicialLCD(char cargo) {
  if (cargo == 'S') return 9;
  if (cargo == 'V' || cargo == 'P') return 10;
  if (cargo == 'F' || cargo == 'E') return 11;
  return 12;
}

int enderecoCandidato(byte idx) {
  return sizeof(Cabecalho) + (idx * sizeof(Candidato));
}

bool carregarEEPROM() {
  Cabecalho cab;
  EEPROM.get(0, cab);

  if (cab.magic != MAGIC) return false;
  if (cab.estado > ENCERRADA) return false;
  if (cab.totalCandidatos > MAX_CANDIDATOS) return false;

  estado = (Estado)cab.estado;
  modo = cab.modo;
  totalCandidatos = cab.totalCandidatos;
  totalEleitores = cab.totalEleitores;
  for (byte i = 0; i < 5; i++) {
    brancos[i] = cab.brancos[i];
    nulos[i] = cab.nulos[i];
  }
  return true;
}

void salvarEEPROM() {
  Cabecalho cab;
  cab.magic = MAGIC;
  cab.estado = (estado == LIBERADA) ? BLOQUEADA : estado;
  cab.modo = modo;
  cab.totalCandidatos = totalCandidatos;
  cab.totalEleitores = totalEleitores;
  for (byte i = 0; i < 5; i++) {
    cab.brancos[i] = brancos[i];
    cab.nulos[i] = nulos[i];
  }

  EEPROM.put(0, cab);
}

int8_t buscarCandidatoEEPROM(char cargo, const char* num, Candidato &destino) {
  for (byte i = 0; i < totalCandidatos; i++) {
    EEPROM.get(enderecoCandidato(i), destino);
    if (destino.cargo == cargo && strcmp(destino.numero, num) == 0) {
      return i;
    }
  }
  return -1;
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
  delay(300);
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
      definirLeds((qtd == digitosDaEtapa(etapaAtual)) ? LED_PISCA : LED_ON, LED_OFF, LED_OFF);
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
  char cargo = cargoDaEtapa(etapaAtual);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(labelCargoLCD(cargo));
  qtd = 0;
  numero[0] = '\0';
  candidatoAtualIdx = -1;
  atualizarLeds();
}

void mostrarTela() {
  lcd.clear();
  lcd.setCursor(0, 0);

  switch (estado) {
    case AGUARDANDO_CONFIG:
      lcd.print(F("Urna Sem Modo"));
      lcd.setCursor(0, 1);
      lcd.print(F("Aguardando.."));
      break;
    case PRONTA:
      lcd.print(F("MODO: "));
      lcd.print(modo == 'M' ? F("Municipal") : F("Federal"));
      lcd.setCursor(0, 1);
      lcd.print(F("Cand: "));
      lcd.print(totalCandidatos);
      break;
    case BLOQUEADA:
      lcd.print(F("Aguardando"));
      lcd.setCursor(0, 1);
      lcd.print(F("liberacao"));
      break;
    case ENCERRADA:
      lcd.print(F("Votacao fechada"));
      lcd.setCursor(0, 1);
      lcd.print(F("Eleitores: "));
      lcd.print(totalEleitores);
      break;
    default:
      break;
  }
}

void setEstado(byte novo) {
  estado = (Estado)novo;
  salvarEEPROM();

  if (estado == LIBERADA) {
    etapaAtual = 0;
    telaDigitando();
  } else {
    mostrarTela();
  }

  atualizarLeds();
  aplicarLeds();

  saida.print(F("ESTADO:"));
  saida.println(nomeEstado(estado));
}

void erroComando() {
  saida.print(F("ERRO: Comando Invalido no estado "));
  saida.println(nomeEstado(estado));
  iniciarErroLed();
}

void cadastrarCandidato(Stream &canal) {
  char linha[36];
  byte len = canal.readBytesUntil('\n', linha, sizeof(linha) - 1);
  linha[len] = '\0';

  while (len > 0 && (linha[len - 1] == '\r' || linha[len - 1] == ' ')) {
    linha[--len] = '\0';
  }

  if (estado != PRONTA || totalCandidatos >= MAX_CANDIDATOS) {
    erroComando();
    return;
  }

  char* virgula2 = strchr(linha + 2, ',');
  if (linha[1] != ',' || virgula2 == NULL) {
    erroComando();
    return;
  }

  *virgula2 = '\0';

  Candidato temp;
  temp.cargo = toupper(linha[0]);
  strncpy(temp.numero, linha + 2, TAM_NUM);
  temp.numero[TAM_NUM] = '\0';
  strncpy(temp.nome, virgula2 + 1, TAM_NOME);
  temp.nome[TAM_NOME] = '\0';
  temp.votos = 0;

  EEPROM.put(enderecoCandidato(totalCandidatos), temp);
  totalCandidatos++;
  salvarEEPROM();
  mostrarTela();

  saida.print(F("OK CANDIDATO:"));
  saida.println(temp.numero);
}

void processarComando(char c, Stream &canal) {
  switch (c) {
    case 'M':
    case 'F':
      if (estado == AGUARDANDO_CONFIG || estado == PRONTA) {
        modo = c;
        totalCandidatos = 0;
        totalEleitores = 0;
        for (byte i = 0; i < 5; i++) { brancos[i] = 0; nulos[i] = 0; }
        saida.print(F("OK MODO:"));
        saida.println(modo);
        setEstado(PRONTA);
      } else {
        erroComando();
      }
      break;

    case 'P':
      cadastrarCandidato(canal);
      break;

    case 'A':
      if (estado == PRONTA) {
        saida.println(F("OK ABRIR"));
        setEstado(BLOQUEADA);
      } else {
        erroComando();
      }
      break;

    case 'L':
      if (estado == BLOQUEADA) {
        saida.println(F("OK LIBERAR"));
        setEstado(LIBERADA);
      } else {
        erroComando();
      }
      break;

    case 'C':
      if (estado == LIBERADA) {
        saida.println(F("OK CANCELAR"));
        setEstado(BLOQUEADA);
      } else {
        erroComando();
      }
      break;

    case 'E':
      if (estado == BLOQUEADA) {
        saida.print(F("OK ENCERRAR TOTAL:"));
        saida.println(totalEleitores);
        setEstado(ENCERRADA);
      } else {
        erroComando();
      }
      break;

    case 'R':
      if (estado == ENCERRADA) {
        saida.println(F("RESULTADO"));
        saida.print(F("MODO:"));
        saida.println(modo);
        saida.print(F("ELEITORES:"));
        saida.println(totalEleitores);

        Candidato temp;
        for (byte e = 0; e < totalEtapas(); e++) {
          char cargo = cargoDaEtapa(e);
          saida.print(F("CARGO:"));
          saida.println(cargo);

          for (byte i = 0; i < totalCandidatos; i++) {
            EEPROM.get(enderecoCandidato(i), temp);
            if (temp.cargo == cargo) {
              saida.print(temp.numero);
              saida.print(F(" - "));
              saida.print(temp.nome);
              saida.print(F(": "));
              saida.println(temp.votos);
            }
          }

          saida.print(F("BRANCOS:"));
          saida.println(brancos[e]);
          saida.print(F("NULOS:"));
          saida.println(nulos[e]);
        }

        saida.println(F("FIM"));
      } else {
        erroComando();
      }
      break;

    case 'Z':
      if (estado == ENCERRADA) {
        modo = ' ';
        totalCandidatos = 0;
        totalEleitores = 0;
        for (byte i = 0; i < 5; i++) { brancos[i] = 0; nulos[i] = 0; }
        saida.println(F("OK ZERAR"));
        setEstado(AGUARDANDO_CONFIG);
      } else {
        erroComando();
      }
      break;

    case 'S':
      saida.print(F("STATUS ESTADO:"));
      saida.print(nomeEstado(estado));
      saida.print(F(" MODO:"));
      saida.print(modo);
      saida.print(F(" CANDIDATOS:"));
      saida.print(totalCandidatos);
      saida.print(F(" ELEITORES:"));
      saida.println(totalEleitores);
      break;

    default:
      saida.print(F("ERRO: Comando desconhecido 0x"));
      saida.print((byte)c, HEX);
      saida.print(canal.available() >= 0 && &canal == &Serial ? F(" (USB)") : F(" (BT)"));
      saida.println();
      iniciarErroLed();
      break;
  }
}

void avancarEtapa() {
  etapaAtual++;

  if (etapaAtual < totalEtapas()) {
    tocarBipeIntermediario();
    telaDigitando();
    return;
  }

  Candidato temp;
  for (byte e = 0; e < totalEtapas(); e++) {
    int8_t escolha = votosPendentes[e];
    if (escolha == -2) {
      brancos[e]++;
    } else if (escolha == -1) {
      nulos[e]++;
    } else if (escolha >= 0) {
      EEPROM.get(enderecoCandidato(escolha), temp);
      temp.votos++;
      EEPROM.put(enderecoCandidato(escolha), temp);
    }
  }

  totalEleitores++;
  salvarEEPROM();

  saida.println(F("VOTO OK"));
  lcd.clear();
  lcd.setCursor(6, 0);
  lcd.print(F("FIM"));
  definirLeds(LED_ON, LED_OFF, LED_OFF);
  aplicarLeds();
  tocarBuzzer();
  delay(1000);
  setEstado(BLOQUEADA);
}

void tratarVoto() {
  char cargo = cargoDaEtapa(etapaAtual);
  byte digEsperados = digitosDaEtapa(etapaAtual);
  byte colBase = colunaInicialLCD(cargo);

  char tecla = teclado.getKey();
  if (tecla >= '0' && tecla <= '9' && qtd < digEsperados) {
    numero[qtd] = tecla;
    lcd.setCursor(colBase + qtd, 0);
    lcd.print(tecla);
    qtd++;
    numero[qtd] = '\0';

    if (qtd == digEsperados) {
      Candidato temp;
      candidatoAtualIdx = buscarCandidatoEEPROM(cargo, numero, temp);
      lcd.setCursor(0, 1);
      if (candidatoAtualIdx >= 0) {
        lcd.print(temp.nome);
      } else {
        lcd.print(F("VOTO NULO"));
      }
      atualizarLeds();
    }
  }

  if (apertou(BTN_CORRIGE)) {
    telaDigitando();
  }

  if (apertou(BTN_BRANCO)) {
    votosPendentes[etapaAtual] = -2;
    lcd.clear();
    lcd.print(F("Voto em BRANCO"));
    delay(600);
    avancarEtapa();
    return;
  }

  if (apertou(BTN_CONFIRMA) && qtd == digEsperados) {
    votosPendentes[etapaAtual] = candidatoAtualIdx;
    avancarEtapa();
  }
}

void lerCanal(Stream &canal) {
  while (canal.available() > 0) {
    char c = canal.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    if (c < 32 || c > 126) continue;
    processarComando(toupper(c), canal);
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);
  bt.begin(9600);
  bt.setTimeout(100);

  pinMode(BTN_CORRIGE, INPUT);
  pinMode(BTN_BRANCO, INPUT);
  pinMode(BTN_CONFIRMA, INPUT);
  pinMode(PINO_BUZZER, OUTPUT);

  for (byte i = 0; i < 3; i++) {
    pinMode(pinosLed[i], OUTPUT);
  }

  lcd.init();
  lcd.backlight();

  Serial.println(F("Comandos: M F P<cargo,num,nome> A L C E R Z S"));

  if (carregarEEPROM()) {
    Serial.print(F("EEPROM: recuperado ESTADO:"));
    Serial.print(nomeEstado(estado));
    Serial.print(F(" CAND:"));
    Serial.print(totalCandidatos);
    Serial.print(F(" ELEITORES:"));
    Serial.println(totalEleitores);
  } else {
    Serial.println(F("EEPROM: vazia ou invalida, iniciando do zero"));
  }

  setEstado(estado);
}

void loop() {
  lerCanal(Serial);
  lerCanal(bt);

  if (estado == LIBERADA) {
    tratarVoto();
  }

  aplicarLeds();
}