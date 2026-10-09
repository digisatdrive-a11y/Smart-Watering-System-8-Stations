#include <WiFi.h>
#include <WebServer.h> 
#include <Preferences.h>
#include <Wire.h>
#include <RTClib.h> 
#include <DNSServer.h>
#include "interface.h"

// 1. CONFIGURAÇÕES E CONSTANTES GLOBAIS (APENAS PONTO DE ACESSO)
const char* ap_ssid = "Rega_Config";
const char* ap_password = "rega1234";

WebServer server(80);
DNSServer dnsServer; 
Preferences pref;
RTC_DS3231 rtc;

const int pinoJumper = 23;     // Jump para escolher a versão (HIGH)12VDC e (LOW)24VAC 
bool modo24VAC = false;        // Variável dinâmica: false = 12VDC | true = 24VAC

const int pinosReles[] = {13, 12, 14, 27, 26, 18, 33, 32};
const int pinoSensorUmidade = 34; 
const int pinoBomba = 4; 

struct Estacao {
  int ini_minutos; 
  int fim_minutos;
};

struct Canal {
  bool dias[7]; 
  Estacao estacoes[3];
};

Canal canais[8];
int limiteUmidadeConfig = 5; 

volatile bool manualAtivo[8] = {false, false, false, false, false, false, false, false};
volatile unsigned long manualTempoInicio[8] = {0, 0, 0, 0, 0, 0, 0, 0};
const unsigned long LIMITE_TEMPO_MANUAL = 10 * 60 * 1000; 

SemaphoreHandle_t xMutexCanais = NULL;
TaskHandle_t TaskRegaHandle = NULL;

void TaskRegaCode( void * pvParameters );
void carregarConfiguracoes();
void handleDados();
void handleSave();
void handleManual();
void handleAjustarHora();

String minutosParaHoraString(int minutosTotais) {
  if (minutosTotais <= 0) return "";
  int horas = minutosTotais / 60;
  int minutos = minutosTotais % 60;
  char buffer[6];
  sprintf(buffer, "%02d:%02d", horas, minutos);
  return String(buffer);
}

int obterNivelUmidade(bool &presente) {
  int leitura = analogRead(pinoSensorUmidade);
  if (leitura >= 4000) {
    presente = false; 
    return 1; 
  }
  presente = true;
  int nivel = map(leitura, 2400, 1800, 1, 10);
  if (nivel < 1) nivel = 1;
  if (nivel > 10) nivel = 10;
  return nivel;
}

void carregarConfiguracoes() {
  if (xSemaphoreTake(xMutexCanais, portMAX_DELAY) == pdTRUE) {
    pref.begin("rega_sistema", true); 
    limiteUmidadeConfig = pref.getInt("lim_umid", 5);
    for (int c = 0; c < 8; c++) {
      String prefixo = "c" + String(c + 1) + "_";
      for (int d = 0; d < 7; d++) {
        canais[c].dias[d] = pref.getBool((prefixo + "d" + String(d)).c_str(), false);
      }
      for (int e = 0; e < 3; e++) {
        String est_pref = prefixo + "e" + String(e + 1) + "_";
        canais[c].estacoes[e].ini_minutos = pref.getInt((est_pref + "ini").c_str(), 0);
        canais[c].estacoes[e].fim_minutos = pref.getInt((est_pref + "fim").c_str(), 0);
      }
    }
    pref.end();
    xSemaphoreGive(xMutexCanais);
  }
}

// 2. TAREFA CORE 0: CONTROLE DE REGA E BOMBA
// =========================================================================
// TAREFA CORE 0: CONTROLE DE REGA ATUALIZADO PARA 8 SETORES + 9º RELÉ CORINGA
// =========================================================================
void TaskRegaCode(void * pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequenciaCheck = pdMS_TO_TICKS(1000); // Varredura de 1 segundo

  // Armazena o estado anterior das válvulas para detectar novas aberturas
  bool estadoAnteriorReles[8] = {false, false, false, false, false, false, false, false};
  
  // Variáveis da máquina de estados - Perfil 12VDC (Push & Hold no 9º Relé)
  bool pulso12VAtivo = false;
  unsigned long tempoInicioPulso12V = 0;
  const unsigned long DURACAO_PULSO_12V = 3000; // 3 segundos de pico de atração

  // Variáveis da máquina de estados - Perfil 24VAC (Motobomba no 9º Relé)
  bool bombaDesejada = false;
  bool bombaLigadaFisicamente = false;
  unsigned long tempoSolicitacaoBomba = 0;
  const unsigned long DELAY_LIGAR_BOMBA = 2000; // 2 segundos de delay de segurança

  for(;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequenciaCheck);

    DateTime agora = rtc.now();
    int diaSemanaAtual = agora.dayOfTheWeek(); 
    int minutosAtuais = agora.hour() * 60 + agora.minute();

    bool sensorPresente = false;
    int umidadeAtual = obterNivelUmidade(sensorPresente);
    bool interromperPorUmidade = (sensorPresente && (umidadeAtual >= limiteUmidadeConfig));

    unsigned long tempoAtualMillis = millis();
    bool algumaRegaAtiva = false;
    bool novaValvulaAbriu = false;

    // AGORA VARRE OS 8 CANAIS COMPLETOS EM AMBOS OS MODOS!
    if (xSemaphoreTake(xMutexCanais, pdMS_TO_TICKS(50)) == pdTRUE) {
      for (int c = 0; c < 8; c++) {
        bool deveRegarEsteCanal = false;
        
        // A. Processamento do Modo Manual
        if (manualAtivo[c]) {
          if (tempoAtualMillis - manualTempoInicio[c] >= LIMITE_TEMPO_MANUAL) {
            manualAtivo[c] = false; 
          } else {
            deveRegarEsteCanal = true;
            algumaRegaAtiva = true;           
          }
        }
        // B. Processamento do Modo Automático
        else if (!interromperPorUmidade) {
          if (canais[c].dias[diaSemanaAtual]) {
            for (int e = 0; e < 3; e++) {
              int ini = canais[c].estacoes[e].ini_minutos;
              int fim = canais[c].estacoes[e].fim_minutos;
              
              if (ini != 0 && fim != 0 && minutosAtuais >= ini && minutosAtuais < fim) {
                deveRegarEsteCanal = true;
                algumaRegaAtiva = true; 
                break;
              }
            }
          }
        }
        
        // Detecta transição de desligado para ligado em qualquer uma das 8 válvulas
        if (deveRegarEsteCanal && !estadoAnteriorReles[c]) {
          novaValvulaAbriu = true; 
        }
        estadoAnteriorReles[c] = deveRegarEsteCanal;

        // Atualiza a saída física do relé correspondente do módulo de 8
        digitalWrite(pinosReles[c], deveRegarEsteCanal ? HIGH : LOW); 
      }
      xSemaphoreGive(xMutexCanais);
    }

    // ====================================================
    // MÁQUINA DE ESTADOS DO 9º RELÉ (pinoBomba / GPIO 4)
    // ====================================================
    if (modo24VAC) {
      // PERFIL 24VAC: GERENCIAMENTO SEGURO DA MOTOBOMBA (Atraso ao ligar)
      if (algumaRegaAtiva) {
        if (!bombaDesejada) {
          bombaDesejada = true;
          tempoSolicitacaoBomba = millis();
        }
        if (!bombaLigadaFisicamente && (millis() - tempoSolicitacaoBomba >= DELAY_LIGAR_BOMBA)) {
          digitalWrite(pinoBomba, HIGH); // Liga o 9º relé acionando a bomba d'água
          bombaLigadaFisicamente = true;
        }
      } else {
        digitalWrite(pinoBomba, LOW); // Corta a bomba imediatamente se tudo fechar
        bombaDesejada = false;
        bombaLigadaFisicamente = false;
      }
      pulso12VAtivo = false;

    } else {
      // PERFIL 12VDC: COMUTADOR DE IGNIÇÃO / MANUTENÇÃO (Hold temporizado)
      if (algumaRegaAtiva) {
        if (novaValvulaAbriu) {
          pulso12VAtivo = true;
          tempoInicioPulso12V = millis();
          digitalWrite(pinoBomba, HIGH); // Liga o 9º relé injetando 12V de Start (NA)
        }
        
        if (pulso12VAtivo && (millis() - tempoInicioPulso12V >= DURACAO_PULSO_12V)) {
          pulso12VAtivo = false;
          digitalWrite(pinoBomba, LOW); // Desliga o 9º relé derrubando para 7V/5V (NF)
        }
      } else {
        digitalWrite(pinoBomba, LOW); // Garante barramento em repouso (12V) se tudo desligar
        pulso12VAtivo = false;
      }
      bombaDesejada = false;
      bombaLigadaFisicamente = false;
    }
  }
}

// 3. SETUP PRINCIPAL (ATUALIZADO PARA SELEÇÃO DE JUMPER)
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // =========================================================================
  // LINHAS ADICIONADAS: CONFIGURAÇÃO E LEITURA DO JUMPER FÍSICO
  // =========================================================================
  pinMode(pinoJumper, INPUT_PULLUP);
  delay(15); // Aguarda estabilização elétrica do Pull-up interno
  
  // Jumper FECHADO ao GND (LOW) = Modo 24VAC | Jumper ABERTO (HIGH) = Modo 12VDC
  if (digitalRead(pinoJumper) == LOW) {
    modo24VAC = true;
    Serial.println(">>> [BANCADA] MODO DETECTADO: 24VAC (Bomba com Atraso) <<<");
  } else {
    modo24VAC = false;
    Serial.println(">>> [BANCADA] MODO DETECTADO: 12VDC (Push & Hold Eletrônico) <<<");
  }
  // =========================================================================

  xMutexCanais = xSemaphoreCreateMutex(); 

  for (int i = 0; i < 8; i++) {
    pinMode(pinosReles[i], OUTPUT);
    digitalWrite(pinosReles[i], LOW); 
  }
  
  pinMode(pinoBomba, OUTPUT);
  digitalWrite(pinoBomba, LOW); 

  pinMode(pinoSensorUmidade, INPUT);

  if (!rtc.begin()) {
    Serial.println("Erro crítico: RTC não encontrado!");
  }
  
  carregarConfiguracoes();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("Painel pronto! Conecte em Rega_Config. IP: "); Serial.println(WiFi.softAPIP());
  
  dnsServer.start(53, "*", WiFi.softAPIP()); // Redireciona qualquer digitação para o portal
  
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/dados", HTTP_GET, handleDados);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/manual", HTTP_POST, handleManual); 
  server.on("/ajustarHora", HTTP_POST, handleAjustarHora); 

  server.begin();

  xTaskCreatePinnedToCore(
    TaskRegaCode, "TaskRega", 4096, NULL, 2, &TaskRegaHandle, 0
  );
  Serial.println("Servidor HTTP pronto.");
}

// 4. LOOP PRINCIPAL
void loop() {
  dnsServer.processNextRequest();
  server.handleClient(); 
  vTaskDelay(pdMS_TO_TICKS(2)); 
}

// 5. IMPLEMENTAÇÃO DAS ROTAS WEB 
void handleDados() {
  if (xSemaphoreTake(xMutexCanais, pdMS_TO_TICKS(10)) != pdTRUE) {
    server.send(503, "text/plain", "Sistema ocupado");
    return;
  }

  bool sensorPresente = false;
  int umidade = obterNivelUmidade(sensorPresente);
  bool interromperPorUmidade = (sensorPresente && (umidade >= limiteUmidadeConfig)); 
   
  DateTime agora = rtc.now();
  char bufferData[6];
  sprintf(bufferData, "%02d:%02d", agora.hour(), agora.minute());

  String json = "{";
  json += "\"hora_atual\":\"" + String(bufferData) + "\",";
  json += "\"limite_umidade\":" + String(limiteUmidadeConfig) + ",";
  json += "\"umidade_atual\":" + String(umidade) + ",";
  json += "\"sensor_presente\":" + String(sensorPresente ? "1" : "0") + ",";
  json += "\"bloqueio_umidade\":" + String(interromperPorUmidade ? "1" : "0") + ","; 
  
  for (int c = 0; c < 8; c++) {
    int statusRele = (digitalRead(pinosReles[c]) == HIGH) ? 1 : 0;
    json += "\"rele_c" + String(c + 1) + "\":" + String(statusRele) + ",";
    
    for (int d = 0; d < 7; d++) {
      json += "\"c" + String(c + 1) + "_d" + String(d) + "\":" + String(canais[c].dias[d] ? "1" : "0") + ",";
    }
    for (int e = 0; e < 3; e++) {
      json += "\"c" + String(c + 1) + "_e" + String(e + 1) + "_ini\":\"" + minutosParaHoraString(canais[c].estacoes[e].ini_minutos) + "\",";
      json += "\"c" + String(c + 1) + "_e" + String(e + 1) + "_fim\":\"" + minutosParaHoraString(canais[c].estacoes[e].fim_minutos) + "\"";
      if (c == 7 && e == 2) {} else { json += ","; }
    }
  }
  json += "}";

  xSemaphoreGive(xMutexCanais);
  server.send(200, "application/json", json);
}

void handleSave() {
  if (xSemaphoreTake(xMutexCanais, pdMS_TO_TICKS(50)) != pdTRUE) {
    server.send(503, "text/plain", "Erro: Ocupado");
    return;
  }

  pref.begin("rega_sistema", false);

  if (server.hasArg("lim_umid")) {
    limiteUmidadeConfig = server.arg("lim_umid").toInt();
    pref.putInt("lim_umid", limiteUmidadeConfig);
  }

  for (int c = 0; c < 8; c++) {
    String prefixo = "c" + String(c + 1) + "_";
    for (int d = 0; d < 7; d++) {
      String argDia = prefixo + "d" + String(d);
      if (server.hasArg(argDia)) {
        bool ativo = (server.arg(argDia) == "1");
        canais[c].dias[d] = ativo;
        pref.putBool((prefixo + "d" + String(d)).c_str(), ativo);
      }
    }
    for (int e = 0; e < 3; e++) {
      String prefixoEst = prefixo + "e" + String(e + 1) + "_";
      if (server.hasArg(prefixoEst + "ini") && server.hasArg(prefixoEst + "fim")) {
        String iniStr = server.arg(prefixoEst + "ini");
        String fimStr = server.arg(prefixoEst + "fim");
        
        int iniMin = 0, fimMin = 0;
        if (iniStr.length() == 5) {
          iniMin = (iniStr.substring(0, 2).toInt() * 60) + iniStr.substring(3, 5).toInt();
        }
        if (fimStr.length() == 5) {
          fimMin = (fimStr.substring(0, 2).toInt() * 60) + fimStr.substring(3, 5).toInt();
        }

        canais[c].estacoes[e].ini_minutos = iniMin;
        canais[c].estacoes[e].fim_minutos = fimMin;
        pref.putInt((prefixoEst + "ini").c_str(), iniMin);
        pref.putInt((prefixoEst + "fim").c_str(), fimMin);
      }
    }
  }

  pref.end();
  xSemaphoreGive(xMutexCanais);
  server.send(200, "text/plain", "OK");
}

void handleManual() {
  if (!server.hasArg("canal") || !server.hasArg("acao")) {
    server.send(400, "text/plain", "Erro");
    return;
  }

  int canalIdx = server.arg("canal").toInt() - 1;
  String acao = server.arg("acao");

  if (canalIdx < 0 || canalIdx >= 8) {
    server.send(400, "text/plain", "Inexistente");
        return;
  }

  if (xSemaphoreTake(xMutexCanais, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (acao == "ligar") {
      manualAtivo[canalIdx] = true;
      manualTempoInicio[canalIdx] = millis();
      digitalWrite(pinosReles[canalIdx], HIGH);
    } else {
      manualAtivo[canalIdx] = false;
      digitalWrite(pinosReles[canalIdx], LOW);
    }
    xSemaphoreGive(xMutexCanais);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(503, "text/plain", "Ocupado");
  }
}

void handleAjustarHora() {
  if (!server.hasArg("ano") || !server.hasArg("mes") || !server.hasArg("dia") ||
      !server.hasArg("hora") || !server.hasArg("minuto")) {
    server.send(400, "text/plain", "Incompleto");
    return;
  }

  int ano = server.arg("ano").toInt();
  int mes = server.arg("mes").toInt();
  int dia = server.arg("dia").toInt();
  int hora = server.arg("hora").toInt();
  int minuto = server.arg("minuto").toInt();

  rtc.adjust(DateTime(ano, mes, dia, hora, minuto, 0));
  server.send(200, "text/plain", "OK");
}
