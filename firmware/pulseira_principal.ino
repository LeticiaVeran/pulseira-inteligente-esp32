// ══════════════════════════════════════════════════════════════
// pulseira_principal.ino
// Pulseira Inteligente para Monitoramento e Segurança de Idosos
// ──────────────────────────────────────────────────────────────
// Disciplina: Projeto de Sistemas Ubíquos e Embarcados — 2026.1
// UFSC Araranguá
// ──────────────────────────────────────────────────────────────
// Hardware:
//   - ESP32-WROOM-32
//   - Sensor MPU6050 (GY-521) — GPIO 21 (SDA), GPIO 22 (SCL)
//   - Buzzer ativo 5V         — GPIO 19 (lógica invertida)
//   - Push button             — GPIO 18 (INPUT_PULLUP)
// ──────────────────────────────────────────────────────────────
// Algoritmo baseado em:
//   Bourke & Lyons (2008) — Medical Engineering & Physics
//   Al-Dahan et al. (2016) — ICOST, Springer
// ══════════════════════════════════════════════════════════════

#include <Wire.h>         // I2C com o MPU6050 (nativa do ESP32)
#include <WiFi.h>         // WiFi (nativa do ESP32)
#include <PubSubClient.h> // Cliente MQTT — instalar via Gerenciador de Bibliotecas

// ── CREDENCIAIS ───────────────────────────────────────────────
// Não edite aqui — copie config.h.example para config.h e edite lá
#include "config.h"

// ── OBJETOS WIFI E MQTT ───────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// ── PARÂMETROS DO ALGORITMO (escala ±4g — sensibilidade 8.192 LSB/g) ──
// Todos os valores foram calibrados com dados reais do sensor.
// Em repouso a magnitude é ~8.192 (equivalente a 1g de gravidade).

// Fase 1 — Queda livre
// Dados reais: queda livre registrou 1.691–2.833 LSB
const float LIMIAR_QUEDA_LIVRE  = 2500;

// Fase 2 — Impacto
// Dados reais: impacto registrou 10.047–15.994 LSB
const float LIMIAR_IMPACTO      = 9500;

// Fase 3 — Repouso
// DEVE ser maior que 8.192 (valor de repouso) para funcionar
const float LIMIAR_REPOUSO      = 9500;

// Giroscópio — bônus de confiança (rotação durante queda real)
// ~152°/s na escala ±500°/s (65,5 LSB/°/s × 152 ≈ 10.000)
const float LIMIAR_GIRO         = 10000;

// Jerk — variação brusca de aceleração que indica início da queda
// Calculado como |magnitude(t) - magnitude(t-1)| / tempo
const float LIMIAR_JERK         = 35000;

// Janela de tempo máxima entre queda livre e impacto (ms)
const int   JANELA_TEMPO        = 1000;

// Tempo mínimo de repouso para confirmar queda real (ms)
// 3s distingue queda real (corpo imóvel) de gesto (corpo continua)
const int   DURACAO_REPOUSO     = 3000;

// Silêncio após alerta — evita alertas duplicados (ms)
const int   BLOQUEIO_APOS_QUEDA = 4000;

// Leituras iniciais ignoradas — sensor precisa estabilizar
const int   WARMUP_LEITURAS     = 15;

// Falhas I2C consecutivas antes de reinicializar o sensor
// O MPU6050 pode entrar em sleep após picos de corrente
// 5 falhas × 50ms = ~250ms sem resposta → reinicializa
const int   MAX_FALHAS          = 5;

// ── DEFINIÇÕES DE HARDWARE ────────────────────────────────────
#define MPU_ADDR    0x68  // endereço I2C com AD0 em GND
#define PINO_BOTAO  18    // INPUT_PULLUP: HIGH=solto, LOW=pressionado
#define PINO_BUZZER 19    // Lógica invertida: LOW=ligado, HIGH=desligado

// ── VARIÁVEIS DE ESTADO DO ALGORITMO ─────────────────────────
bool quedaLivreDetectada  = false; // true quando fase 1 ativa
bool giroDetectadoNaQueda = false; // true se giroscópio > limiar na fase 1
bool impactoDetectado     = false; // true quando fase 2 ativa
int  contagemWarmup       = 0;     // conta leituras do warmup
int  contagemFalhasI2C    = 0;     // conta falhas consecutivas de leitura

// Timestamps (unsigned long para suportar millis() indefinidamente)
unsigned long tempoQuedaLivre  = 0; // momento que a fase 1 foi detectada
unsigned long tempoImpacto     = 0; // momento que a fase 2 foi detectada
unsigned long tempoUltimaQueda = 0; // momento do último alerta enviado
unsigned long tempoUltimoBotao = 0; // momento do último acionamento do botão

// Para cálculo do jerk (variação entre leituras consecutivas)
// -1 indica "não inicializado" — evita jerk falso na primeira leitura
float         magRawAnterior = -1;
unsigned long tempoAnterior  = 0;

// ═════════════════════════════════════════════════════════════
// FUNÇÕES AUXILIARES
// ═════════════════════════════════════════════════════════════

// Aciona o buzzer N vezes
// Lógica invertida: LOW liga o buzzer (controla a conexão ao GND)
// Padrões: 1=inicialização | 3=confiança média | 5=alta | 10=emergência
void acionaBuzzer(int vezes) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(PINO_BUZZER, LOW);  // liga
    delay(500);
    digitalWrite(PINO_BUZZER, HIGH); // desliga
    delay(300);
  }
}

// Configura todos os registradores do MPU6050
// Chamada no setup() e automaticamente quando o sensor para de responder
void inicializaMPU() {
  // 1. Acorda o sensor — padrão de fábrica é modo sleep
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // registrador PWR_MGMT_1
  Wire.write(0x00);  // 0x00 = desativa sleep
  Wire.endTransmission(true);
  delay(100); // aguarda estabilizar

  // 2. Configura filtro digital interno (DLPF)
  // 0x02 = 94Hz de largura de banda, 3ms de atraso
  // 94Hz preserva o pico de impacto (evento de 50–100ms)
  // Filtros mais restritivos (ex: 10Hz) atenuariam o sinal de queda
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);  // registrador CONFIG
  Wire.write(0x02);  // DLPF_CFG = 2 → 94Hz
  Wire.endTransmission(true);

  // 3. Escala do acelerômetro: ±4g
  // ±4g escolhido porque quedas reais produzem 2,5g–3,8g
  // ±2g saturaria em 32.767, distorcendo a leitura do impacto
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);  // registrador ACCEL_CONFIG
  Wire.write(0x08);  // AFS_SEL = 1 → ±4g (sensibilidade: 8.192 LSB/g)
  Wire.endTransmission(true);

  // 4. Escala do giroscópio: ±500°/s
  // Sensibilidade resultante: 65,5 LSB por grau/segundo
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);  // registrador GYRO_CONFIG
  Wire.write(0x08);  // FS_SEL = 1 → ±500°/s
  Wire.endTransmission(true);

  Serial.println("MPU6050 inicializado.");
}

// Lê 14 bytes do sensor em uma única transação I2C:
// Bytes 1-6:  aceleração (ax, ay, az — 2 bytes cada, big-endian)
// Bytes 7-8:  temperatura interna (descartada — obrigatório ler)
// Bytes 9-14: giroscópio (gx, gy, gz — 2 bytes cada, big-endian)
// Retorna false se menos de 14 bytes foram recebidos (falha I2C)
bool leSensor(int16_t &ax, int16_t &ay, int16_t &az,
              int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);            // registrador ACCEL_XOUT_H — início dos dados
  Wire.endTransmission(false); // false = não envia STOP, mantém barramento
  uint8_t n = Wire.requestFrom(MPU_ADDR, 14, true);
  if (n < 14) return false;    // falha: sensor não respondeu corretamente

  // Reconstrói cada valor de 16 bits a partir de 2 bytes (big-endian):
  // Wire.read() << 8 = desloca o byte mais significativo (MSB) 8 posições
  // | Wire.read()    = combina com o byte menos significativo (LSB)
  ax = Wire.read() << 8 | Wire.read(); // eixo X (lateral)
  ay = Wire.read() << 8 | Wire.read(); // eixo Y (frente/trás)
  az = Wire.read() << 8 | Wire.read(); // eixo Z (vertical/gravidade)

  // Temperatura (2 bytes): obrigatório ler — sem isso o giroscópio fica deslocado
  Wire.read(); Wire.read();

  gx = Wire.read() << 8 | Wire.read(); // giroscópio eixo X
  gy = Wire.read() << 8 | Wire.read(); // giroscópio eixo Y
  gz = Wire.read() << 8 | Wire.read(); // giroscópio eixo Z
  return true;
}

// Reseta todas as flags do algoritmo
// Chamado após: queda confirmada, falso positivo ou timeout
void resetaEstado() {
  quedaLivreDetectada  = false;
  giroDetectadoNaQueda = false;
  impactoDetectado     = false;
}

// Conecta ao WiFi — bloqueante no setup, aceitável pois só ocorre uma vez
void conectaWiFi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(" Conectado! IP: ");
  Serial.println(WiFi.localIP());
}

// Conecta ao broker MQTT — tenta até conseguir
void conectaMQTT() {
  mqtt.setServer(MQTT_SERVIDOR, MQTT_PORTA);
  while (!mqtt.connected()) {
    Serial.print("Conectando MQTT...");
    if (mqtt.connect("ESP32_Pulseira", MQTT_USUARIO, MQTT_SENHA)) {
      Serial.println(" Conectado!");
      // Publica status online para o Home Assistant saber que está ativo
      mqtt.publish("pulseira/status", "online");
    } else {
      Serial.print(" Falhou, rc="); Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// ═════════════════════════════════════════════════════════════
// SETUP — executado UMA VEZ ao ligar o ESP32
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // I2C nos pinos padrão do ESP32: SDA=GPIO21, SCL=GPIO22
  Wire.begin(21, 22);

  // Botão: pull-up interno — HIGH em repouso, LOW ao pressionar
  pinMode(PINO_BOTAO, INPUT_PULLUP);

  // Buzzer: começa DESLIGADO (lógica invertida: HIGH = desligado)
  pinMode(PINO_BUZZER, OUTPUT);
  digitalWrite(PINO_BUZZER, HIGH);

  inicializaMPU();
  conectaWiFi();
  conectaMQTT();

  tempoAnterior = millis();

  // 1 bipe de inicialização — confirma que o buzzer está funcionando
  acionaBuzzer(1);

  Serial.println("Inicializando... aguarde warmup.");
}

// ═════════════════════════════════════════════════════════════
// LOOP — executado CONTINUAMENTE (~20 vezes por segundo)
// ═════════════════════════════════════════════════════════════
void loop() {
  unsigned long agora = millis();
  int16_t ax, ay, az, gx, gy, gz;

  // Mantém a conexão MQTT ativa (reconecta automaticamente se cair)
  if (!mqtt.connected()) conectaMQTT();
  mqtt.loop(); // processa mensagens MQTT recebidas

  // ── BOTÃO DE EMERGÊNCIA MANUAL ────────────────────────────
  // Debounce de 3s por software — evita múltiplos disparos por vibração
  if (digitalRead(PINO_BOTAO) == LOW &&
      agora - tempoUltimoBotao > 3000) {
    Serial.println(">>> BOTAO DE EMERGENCIA ACIONADO! <<<");
    mqtt.publish("pulseira/emergencia", "botao_manual");
    acionaBuzzer(10); // 10 bipes = padrão de emergência
    tempoUltimoBotao = agora;
  }

  // ── BLOQUEIO PÓS-QUEDA ────────────────────────────────────
  // Após alerta, ignora novas detecções por BLOQUEIO_APOS_QUEDA ms
  // IMPORTANTE: mesmo durante o bloqueio, atualiza magRawAnterior
  // para evitar jerk falso quando o bloqueio terminar
  if (agora - tempoUltimaQueda < BLOQUEIO_APOS_QUEDA) {
    if (leSensor(ax, ay, az, gx, gy, gz)) {
      magRawAnterior    = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);
      tempoAnterior     = agora;
      contagemFalhasI2C = 0;
    }
    delay(50);
    return;
  }

  // ── LEITURA COM AUTO-RECUPERAÇÃO ─────────────────────────
  // MPU6050 pode entrar em sleep após picos de corrente (impactos)
  // Após MAX_FALHAS consecutivas, reinicializa automaticamente
  if (!leSensor(ax, ay, az, gx, gy, gz)) {
    contagemFalhasI2C++;
    tempoAnterior = agora; // evita dt acumulado na próxima leitura
    Serial.print("!! Falha I2C #"); Serial.println(contagemFalhasI2C);
    if (contagemFalhasI2C >= MAX_FALHAS) {
      Serial.println("!! Sensor dormiu — reinicializando MPU...");
      inicializaMPU();
      contagemFalhasI2C = 0;
      contagemWarmup    = 0; // refaz warmup após reinicializar
      magRawAnterior    = -1;
      resetaEstado();
    }
    delay(50);
    return;
  }
  contagemFalhasI2C = 0; // leitura ok — reseta contador

  // ── CÁLCULOS ──────────────────────────────────────────────

  // Magnitude = √(ax² + ay² + az²)
  // Representa a intensidade total da aceleração independente da orientação
  float magRaw  = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);

  // Magnitude do giroscópio = velocidade angular total
  float magGiro = sqrt((float)gx*gx + (float)gy*gy + (float)gz*gz);

  // Jerk = variação da magnitude por segundo
  // max(..., 0.001f) garante dt > 0, evitando divisão por zero
  float dt   = max((float)(agora - tempoAnterior) / 1000.0f, 0.001f);
  float jerk = (magRawAnterior >= 0) ? abs(magRaw - magRawAnterior) / dt : 0;
  magRawAnterior = magRaw;
  tempoAnterior  = agora;

  // ── WARMUP ────────────────────────────────────────────────
  // Ignora as primeiras WARMUP_LEITURAS leituras
  // Sem isso, o primeiro jerk pode ser alto e disparar falsa queda
  if (contagemWarmup < WARMUP_LEITURAS) {
    contagemWarmup++;
    if (contagemWarmup == WARMUP_LEITURAS)
      Serial.println("Sistema pronto para monitoramento.");
    delay(50);
    return;
  }

  // Debug — visível no Monitor Serial (115200 baud)
  Serial.print("Acel:"); Serial.print(magRaw, 0);
  Serial.print(" Giro:"); Serial.print(magGiro, 0);
  Serial.print(" Jerk:"); Serial.print(jerk, 0);
  Serial.print(" QL:"); Serial.print(quedaLivreDetectada);
  Serial.print(" GQ:"); Serial.print(giroDetectadoNaQueda);
  Serial.print(" IMP:"); Serial.println(impactoDetectado);

  // ══════════════════════════════════════════════════════════
  // ALGORITMO DE DETECÇÃO DE QUEDAS — 3 FASES
  // ══════════════════════════════════════════════════════════

  // ── FASE 1: QUEDA LIVRE ───────────────────────────────────
  // Magnitude baixa + jerk alto = transição brusca para queda
  // O jerk alto garante que não é simplesmente o sensor inclinado
  if (!quedaLivreDetectada &&
      magRaw < LIMIAR_QUEDA_LIVRE &&
      jerk   > LIMIAR_JERK) {
    quedaLivreDetectada  = true;
    giroDetectadoNaQueda = false;
    impactoDetectado     = false;
    tempoQuedaLivre      = agora;
    Serial.println("-- Fase 1: queda livre detectada");
  }

  // ── DURANTE QUEDA LIVRE ───────────────────────────────────
  if (quedaLivreDetectada && !impactoDetectado) {

    // Giroscópio alto durante queda livre = rotação corporal
    // Aumenta a confiança mas não é obrigatório
    // (quedas lentas de idosos podem ter pouca rotação)
    if (magGiro > LIMIAR_GIRO) giroDetectadoNaQueda = true;

    // ── FASE 2: IMPACTO ────────────────────────────────────
    // Magnitude alta dentro da janela de tempo após queda livre
    if (magRaw > LIMIAR_IMPACTO &&
        agora - tempoQuedaLivre < JANELA_TEMPO) {
      impactoDetectado = true;
      tempoImpacto     = agora;
      Serial.println("-- Fase 2: impacto detectado");
    }

    // Timeout: queda livre sem impacto na janela = falso positivo
    if (agora - tempoQuedaLivre > JANELA_TEMPO) {
      Serial.println("-- Timeout: sem impacto, cancelando");
      resetaEstado();
    }
  }

  // ── FASE 3: CONFIRMAÇÃO POR REPOUSO ──────────────────────
  // Após o impacto, aguarda corpo ficar parado por DURACAO_REPOUSO
  // Gestos bruscos: corpo continua em movimento → descartado
  // Quedas reais: corpo imóvel no chão → confirmado
  if (impactoDetectado &&
      agora - tempoImpacto > DURACAO_REPOUSO) {

    bool corpoParado = (magRaw < LIMIAR_REPOUSO);

    if (corpoParado) {
      // QUEDA CONFIRMADA
      if (giroDetectadoNaQueda) {
        // Alta confiança: todas as 3 fases + rotação detectada
        Serial.println(">>> QUEDA CONFIRMADA (alta confiança) <<<");
        mqtt.publish("pulseira/queda", "alta_confianca");
        acionaBuzzer(5); // 5 bipes
      } else {
        // Confiança média: 3 fases sem rotação — ainda confirmamos
        // Pode ser queda lenta de idoso com pouca rotação corporal
        Serial.println(">>> QUEDA CONFIRMADA (confiança média) <<<");
        mqtt.publish("pulseira/queda", "media_confianca");
        acionaBuzzer(3); // 3 bipes
      }
      tempoUltimaQueda = agora; // inicia bloqueio pós-queda
    } else {
      // Corpo em movimento após o tempo de repouso = falso positivo
      Serial.println("-- Descartado: corpo não parou após impacto");
    }

    // Em ambos os casos, reseta para o próximo monitoramento
    resetaEstado();
  }

  // Cada ciclo dura ~50ms = 20 leituras por segundo
  delay(50);
}
