# 🚜 Sistema de Irrigação Automático IoT com ESP32 (8 Estações + Bomba)

Sistema de **automação e controle de irrigação agrícola/residencial** via celular de alta confiabilidade. O projeto foi projetado de forma independente com o auxílio de **Inteligência Artificial (IA)** para oferecer uma alternativa robusta, flexível e de baixo custo frente aos controladores comerciais de mercado.

## 🚀 Diferenciais do Projeto
* **Controle Total via Celular:** Interface intuitiva acessível via navegador Web ou aplicativo, permitindo configurar tempos e horários de rega de qualquer lugar da propriedade.
* **Alta Capacidade (8 Estações):** Gerenciamento individual de até 8 setores de irrigação independentes através de válvulas solenoides.
* **Segurança Hidráulica (9º Relé):** Acionamento automatizado de um 9º relé dedicado exclusivamente para ligar e desligar a bomba de água ou a válvula mestra, protegendo o sistema contra pressurização indevida.
* **Alimentação Flexível (Dual Power):** O circuito foi desenhado para aceitar fontes de **12VDC** ou sistemas de **24VAC** (padrão de mercado para a maioria das válvulas solenoides de irrigação).

## 🛠️ Hardware Utilizado
* **Microcontrolador Central:** ESP32 WROOM (garantindo estabilidade, processamento rápido e excelente alcance WiFi).
* **Módulo de Saída:** Módulo de relés de 8 canais + 1 relé auxiliar para a bomba.
* **Fonte de Alimentação:** Circuito preparado para comutação de energia (12VDC / 24VAC).

## 📁 Estrutura do Código e Funcionamento
O código embarcado no ESP32 gera um servidor web local e gerencia o relógio interno para garantir que a programação de rega funcione de maneira precisa.
1. O ESP32 aciona o **9º relé (Bomba)** sempre que houver qualquer uma das 8 estações ativas.
2. Cada uma das 8 estações possui temporizadores independentes e configuráveis pela tela do celular.
3. Protótipo validado com sucesso e funcionando em bancada, pronto para implementação e testes práticos no campo.

## 🤝 Contribuições e Legado
Este projeto é **Open Source** (Código Aberto) voltado para o desenvolvimento da agricultura familiar, automação de estufas e democratização da tecnologia IoT no campo brasileiro. Sinta-se livre para abrir *Issues*, sugerir melhorias no código ou criar *Pull Requests*.

---
*Desenvolvido com orgulho por um desenvolvedor sênior de 67 anos e entusiasta de IA.* 🛠️🌾
