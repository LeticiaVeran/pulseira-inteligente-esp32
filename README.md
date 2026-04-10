# 🩺 Pulseira Inteligente para Monitoramento e Segurança de Idosos

> Detecção automática de quedas com ESP32 + MPU6050, alertas via MQTT e notificação persistente no celular pelo Home Assistant.

![ESP32](https://img.shields.io/badge/ESP32-WROOM--32-blue)
![Arduino](https://img.shields.io/badge/Arduino-IDE%202-teal)
![MQTT](https://img.shields.io/badge/MQTT-Mosquitto-orange)
![Home Assistant](https://img.shields.io/badge/Home%20Assistant-OS%2017-41BDF5)
![License](https://img.shields.io/badge/License-MIT-green)

---

## 📌 Sobre o projeto

Projeto desenvolvido na disciplina **Projeto de Sistemas Ubíquos e Embarcados — 2026.1** da UFSC Araranguá.

O sistema detecta automaticamente quedas de idosos através de um sensor inercial (MPU6050) acoplado ao pulso. Quando uma queda é confirmada, um alarme sonoro (buzzer) dispara imediatamente na pulseira e uma notificação persistente é enviada ao celular do cuidador via Home Assistant. O idoso também pode acionar um botão de emergência manual.

### Como funciona

```
Sensor MPU6050 (pulso)
        ↓  I2C
ESP32 detecta queda (algoritmo 3 fases)
        ↓  WiFi + MQTT
Mosquitto Broker (Raspberry Pi)
        ↓
Home Assistant
        ↓
Notificação persistente no celular do cuidador
```

---

## ✅ Funcionalidades implementadas

- **Detecção automática de quedas** — algoritmo de 3 fases: queda livre → impacto → repouso
- **Alarme sonoro local** — buzzer com padrões distintos: 3 bipes (confiança média), 5 bipes (alta confiança), 10 bipes (emergência)
- **Botão de emergência manual** — o próprio idoso pode acionar
- **Notificação persistente no celular** — via app Home Assistant, não some até confirmar
- **Auto-recuperação do sensor** — reinicialização automática se o MPU6050 entrar em modo sleep
- **Dois níveis de confiança** — `alta_confianca` (queda livre + impacto + repouso + rotação) e `media_confianca` (queda livre + impacto + repouso)

---

## 🧰 Hardware necessário

| Componente | Quantidade | Observação |
|---|---|---|
| ESP32-WROOM-32 (+ expansion board) | 1 | Qualquer DevKit ESP32 |
| Sensor MPU6050 (módulo GY-521) | 1 | Acelerômetro + giroscópio |
| Buzzer ativo 5V | 1 | Com oscilador interno |
| Push button | 1 | 4 pinos |
| Raspberry Pi 3B+ | 1 | Servidor Home Assistant |
| Cartão SD 32GB | 1 | Para o Home Assistant OS |
| Jumpers fêmea-fêmea | ~8 | Para conexão sem solda |
| Power bank ou bateria LiPo | 1 | Alimentação portátil |

---

## 🔌 Esquema de conexões

```
MPU6050 (GY-521)         ESP32
VCC  ─────────────────── 3,3V
GND  ─────────────────── GND
SCL  ─────────────────── GPIO 22
SDA  ─────────────────── GPIO 21

Buzzer ativo             ESP32
+ (positivo) ─────────── VIN (5V)
− (negativo) ─────────── GPIO 19  ← lógica invertida: LOW = ligado

Push button              ESP32
Pino 1 ───────────────── GPIO 18  ← INPUT_PULLUP
Pino 2 ───────────────── GND
Pinos 3 e 4 ────────────── não utilizados
```

> Ver diagrama visual completo em `docs/esquema_conexoes.png`

---

## ⚙️ Como instalar e configurar

### 1. Preparar o Arduino IDE

1. Instalar o [Arduino IDE 2](https://arduino.cc/en/software)
2. Em **Preferências**, adicionar a URL do ESP32:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Instalar o pacote **esp32** via Gerenciador de Placas
4. Instalar a biblioteca **PubSubClient** via Gerenciador de Bibliotecas
5. Selecionar placa: **ESP32 Dev Module**

### 2. Configurar credenciais

Copie o arquivo de exemplo e preencha com seus dados:
```bash
cp firmware/config.h.example firmware/config.h
```

Edite `firmware/config.h` com suas credenciais:
```cpp
#define WIFI_SSID     "Nome_da_sua_rede"
#define WIFI_SENHA    "Senha_da_sua_rede"
#define MQTT_SERVIDOR "IP_do_Raspberry_Pi"
#define MQTT_USUARIO  "seu_usuario_mqtt"
#define MQTT_SENHA    "sua_senha_mqtt"
```

> ⚠️ O arquivo `config.h` está no `.gitignore` — suas credenciais nunca serão enviadas ao GitHub.

### 3. Gravar o firmware

1. Conecte o ESP32 via USB
2. Abra `firmware/pulseira_principal.ino` no Arduino IDE
3. Selecione a porta COM correta
4. Clique em **→ Carregar**

### 4. Instalar o Home Assistant

Siga o guia completo em [`home_assistant/README_HA.md`](home_assistant/README_HA.md)

---

## 📡 Tópicos MQTT

| Tópico | Payload | Quando |
|---|---|---|
| `pulseira/queda` | `alta_confianca` | Queda com rotação detectada |
| `pulseira/queda` | `media_confianca` | Queda sem rotação detectada |
| `pulseira/emergencia` | `botao_manual` | Botão de emergência pressionado |
| `pulseira/status` | `online` | ESP32 conectado ao broker |

---

## 🧠 Algoritmo de detecção de quedas

O algoritmo é baseado em literatura científica (Bourke & Lyons, 2008; Al-Dahan et al., 2016) e detecta três fases características de uma queda real:

```
FASE 1 — Queda livre
Magnitude < 2.500 LSB (~0,30g) + jerk alto
↓ (dentro de 1 segundo)
FASE 2 — Impacto
Magnitude > 9.500 LSB (~1,16g)
↓ (aguarda 3 segundos)
FASE 3 — Repouso
Magnitude < 9.500 LSB (corpo imóvel)
↓
QUEDA CONFIRMADA → buzzer + MQTT
```

Gestos cotidianos **não** têm fase de queda livre → nunca disparam a Fase 1.

### Parâmetros calibrados (escala ±4g)

| Parâmetro | Valor | Justificativa |
|---|---|---|
| LIMIAR_QUEDA_LIVRE | 2.500 LSB | Dados reais: 1.691–2.833 LSB |
| LIMIAR_IMPACTO | 9.500 LSB | Dados reais: 10.047–15.994 LSB |
| LIMIAR_REPOUSO | 9.500 LSB | Deve ser > 8.192 (repouso = 1g) |
| JANELA_TEMPO | 1.000 ms | Fase 1 → Fase 2 |
| DURACAO_REPOUSO | 3.000 ms | Confirmação final |

---

## 📁 Estrutura do repositório

```
pulseira-inteligente-esp32/
├── README.md
├── .gitignore
├── firmware/
│   ├── pulseira_principal.ino   ← código principal
│   ├── config.h.example         ← modelo de configuração
│   └── libraries.txt            ← bibliotecas necessárias
├── docs/
│   ├── Guia_ABNT_Pulseira.docx  ← guia técnico completo
│   └── esquema_conexoes.png     ← diagrama de fios
├── home_assistant/
│   ├── automacao_queda.yaml
│   ├── automacao_emergencia.yaml
│   └── README_HA.md
└── fotos/
```

---

## 📚 Referências científicas

- BOURKE, A. K.; LYONS, G. M. A threshold-based fall-detection algorithm using a bi-axial gyroscope sensor. *Medical Engineering & Physics*, v. 30, n. 1, p. 84–90, 2008.
- AL-DAHAN, Z. T. et al. Design and Implementation of Fall Detection System Using MPU6050 Arduino. *ICOST 2016*, Springer, 2016.
- INVENSENSE. *MPU-6050 Product Specification Rev. 3.4*. 2013.
- ESPRESSIF SYSTEMS. *ESP32 Technical Reference Manual*. 2023.

---

## 👥 Equipe

| Nome | Papel |
|---|---|
| [Nome 1] | Firmware e algoritmo |
| [Nome 2] | Hardware e eletrônica |
| [Nome 3] | Servidor e automações |

**Orientador:** Prof. Alessandro / Profa. — UFSC Araranguá

---

## 📄 Licença

MIT License — veja o arquivo [LICENSE](LICENSE) para detalhes.
