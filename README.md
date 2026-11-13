# Urna Eletrônica

Urna eletrônica baseada em Arduino. Os votos são registrados por botões físicos, e a configuração e a leitura dos resultados são feitas por Bluetooth, com um placar exibido em uma página web.

## Como Funciona

1. O eleitor votra pelos botões ligados ao Arduino. O LCD mostra o estado da votação e firma o voto.

2. O Arduino guarda a contagem por cadidato na EEPROM, então os votos não se perdem se a alimentação de energia cair.

3. O Arduino envia a contagem pelo módulo HC-05

4. Um script Python no notebook lê essa linha pela porta serial Bluetooth e expõe os dados em um endpoit (Flask).

5. A página consulta o endpoit a cada poucos segundos e atualiza o placar.

## Tecnologias

**Firmware (Arduino)**
- C++ (framework Arduino)
- SoftwareSerial
- EEPROM
- LiquidCrystal_I2C

**Ponte (Maquina)**
- Python (pyserial)
- PySerial
- Flask

**Página Web**
- HTML, CSS, JavaScript
- JSON (formato dos dados)

**Hardware**
- Arduino Mega
- Módulo Bluetooth HC-05
- LCD 16x2 + Conversor I2C
- Teclado Matricial 3X4