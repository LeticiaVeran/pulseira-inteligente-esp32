# Configuração do Home Assistant

Guia passo a passo para configurar o servidor Home Assistant no Raspberry Pi 3B+.

---

## 1. Instalar o Home Assistant OS no Raspberry Pi

### O que você precisa
- Raspberry Pi 3B+
- Cartão SD de 32GB
- Cabo de rede (RJ45)
- Fonte de alimentação micro USB

### Passos

1. Baixe o **Raspberry Pi Imager** em [raspberrypi.com/software](https://raspberrypi.com/software)
2. Abra o Imager e selecione:
   - **Device:** Raspberry Pi 3
   - **OS:** Other specific purpose OS → Home assistants and home automation → **Home Assistant OS (RPI 3)**
   - **Storage:** seu cartão SD
3. Clique em **Next → Write** e aguarde (~15 minutos)
4. Insira o SD no Raspberry Pi, conecte o cabo de rede e ligue a alimentação
5. Aguarde **10 minutos** para o primeiro boot

### Acessar pela primeira vez

No navegador do PC (mesma rede):
```
http://homeassistant.local:8123
```

Se não funcionar, descubra o IP com:
```bash
# No Prompt de Comando do Windows
arp -a
# Procure pelo MAC address B8:27:EB:XX:XX:XX (prefixo do Raspberry Pi)
```

---

## 2. Configurar rede via cartão SD (se necessário)

Se o Raspberry Pi não conectar automaticamente, crie os arquivos de rede no cartão SD:

**Pasta:** `hassos-boot/CONFIG/network/`

**Arquivo `my-network` (WiFi):**
```ini
[connection]
id=my-network
type=802-11-wireless

[802-11-wireless]
ssid=NomeDaSuaRede
mode=infrastructure

[802-11-wireless-security]
auth-alg=open
key-mgmt=wpa-psk
psk=SenhaDaSuaRede

[ipv4]
method=auto

[ipv6]
addr-gen-mode=stable-privacy
method=auto
```

**Arquivo `my-ethernet` (cabo de rede):**
```ini
[connection]
id=my-ethernet
type=ethernet

[ethernet]
mac-address=B8:27:EB:XX:XX:XX

[ipv4]
method=auto

[ipv6]
addr-gen-mode=stable-privacy
method=auto
```

---

## 3. Instalar o broker MQTT (Mosquitto)

1. No Home Assistant, acesse: **Configurações → Complementos**
2. Clique em **Loja de complementos** (canto inferior direito)
3. Pesquise **Mosquitto broker** e instale
4. Ative as opções:
   - ✅ Iniciar na inicialização
   - ✅ Watchdog
5. Clique em **Iniciar**

---

## 4. Criar usuário MQTT

1. Acesse: **Configurações → Pessoas → Adicionar pessoa**
2. Crie um usuário (ex: `mqtt_user`) com uma senha
3. Anote as credenciais — serão usadas no `config.h` do ESP32

---

## 5. Integrar MQTT ao Home Assistant

1. Acesse: **Configurações → Dispositivos e serviços**
2. Clique em **Adicionar integração**
3. Pesquise **MQTT**
4. Preencha:
   - Broker: `core-mosquitto`
   - Porta: `1883`
   - Usuário e senha criados no passo anterior
5. Clique em **Enviar**

---

## 6. Criar automações de alerta

### Opção A — Via interface (recomendado)
1. Acesse: **Configurações → Automações → Criar automação → Criar em branco**
2. Configure conforme os arquivos `automacao_queda.yaml` e `automacao_emergencia.yaml`

### Opção B — Importar YAML
Cole o conteúdo dos arquivos YAML diretamente no editor YAML da automação.

> ⚠️ Substitua `mobile_app_SEU_CELULAR` pelo ID do seu dispositivo. O ID aparece em: **Configurações → Aplicativos para dispositivos móveis**

---

## 7. Instalar o app Home Assistant no celular

- **Android:** [Play Store — Home Assistant](https://play.google.com/store/apps/details?id=io.homeassistant.companion.android)
- **iOS:** [App Store — Home Assistant](https://apps.apple.com/app/home-assistant/id1099568401)

Após instalar:
1. Abra o app e conecte ao seu servidor: `http://IP_DO_RASPBERRY:8123`
2. Permita o envio de notificações
3. O dispositivo aparecerá em: **Configurações → Aplicativos para dispositivos móveis**

---

## 8. Testar a comunicação

No Home Assistant, acesse: **Configurações → Dispositivos e serviços → MQTT → Configurar → Escutar um tópico**

Digite `pulseira/queda` e clique em **Iniciar escuta**.

Simule uma queda com o sensor — deve aparecer `alta_confianca` ou `media_confianca`.

---

## 9. Configurar para apresentação (hotspot)

Para usar em redes que isolam dispositivos (como a rede da UFSC):

1. Ative o hotspot no celular
2. Conecte o Raspberry Pi ao hotspot:
   ```
   # No terminal do HA (via HDMI ou SSH)
   network update wlan0 --ipv4-method auto --wifi-ssid "NomeHotspot" --wifi-psk "SenhaHotspot"
   ```
3. Atualize `MQTT_SERVIDOR` no `config.h` do ESP32 com o novo IP do Raspberry Pi
4. Recompile e grave o firmware

---

## Resolução de problemas

| Problema | Solução |
|---|---|
| `homeassistant.local` não abre | Use o IP direto: verifique com `arp -a` no PC |
| MQTT não conecta (rc=-2) | Verifique o IP do Raspberry Pi e se o Mosquitto está rodando |
| Sensor volta a zero | Auto-reinicialização após 5 falhas — é normal se o cabo estiver solto |
| Notificação não chega | Verifique se o app tem permissão de notificação no celular |
