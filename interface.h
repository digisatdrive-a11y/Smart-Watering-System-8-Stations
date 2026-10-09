#ifndef interface_h
#define interface_h

#include <Arduino.h>

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Painel de Rega ESP32</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1, h2 { color: #007bff; text-align: center; }
        .section { border: 1px solid #ddd; padding: 15px; margin-bottom: 20px; border-radius: 5px; background: #f9f9f9; }
        .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 10px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; font-weight: bold; margin-bottom: 5px; }
        input[type="text"], input[type="time"], select { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
        .days-container { display: flex; flex-wrap: wrap; gap: 10px; margin: 10px 0; }
        .day-btn { padding: 8px 12px; background: #e0e0e0; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }
        .day-btn.active { background: #28a745; color: white; }
        button.save-btn { width: 100%; padding: 12px; background: #007bff; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; font-weight: bold; }
        button.save-btn:hover { background: #0056b3; }
        .status-sensor { font-weight: bold; color: #555; margin-top: 5px; font-size: 0.9em; }
        .manual-btn { width: 100%; padding: 10px; margin-top: 15px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; font-size: 0.9em; transition: background 0.2s; }
        .manual-btn.active { background: #dc2626; animation: piscar 1.5s infinite; }
        @keyframes piscar { 0% { opacity: 1; } 50% { opacity: 0.6; } 100% { opacity: 1; } }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌧️ SMART WATERING SYSTEM </h1>
        <div style="text-align: center; margin-bottom: 20px; font-weight: bold; color: #007bff; background: #e3f2fd; padding: 10px; border-radius: 5px; border: 1px solid #bbdefb;">
            📲 Modo Ponto de Acesso Ativo | Relógio Sistema: <span id="txt_hora_sistema">00:00</span>
        </div>

        <form id="regaForm" onsubmit="return enviarFormulario(event)">
            <div class="section">
                <h2>⚙️ Parâmetros de Segurança</h2>
                <div class="form-group">
                    <label>Bloquear rega se umidade for maior que:</label>
                    <select name="lim_umid" id="limite_umidade" onchange="salvarLimiteUmidadeImediato(this.value)">
                        <option value="1">1 (Praticamente Seco)</option>
                        <option value="2">2</option>
                        <option value="3">3</option>
                        <option value="4">4</option>
                        <option value="5">5 (Umidade Média)</option>
                        <option value="6">6</option>
                        <option value="7">7</option>
                        <option value="8">8</option>
                        <option value="9">9</option>
                        <option value="10">10 (Muito Úmido / Encharcado)</option>
                    </select>
                    <div class="status-sensor" id="status_sensor">Status do Sensor: Carregando...</div>
                </div>
            </div>
            <div id="canaisContainer"></div>
            <button type="submit" class="save-btn">💾 Salvar Cronograma dos Canais</button>
        </form>
    </div>
    <script>
const diasSemana = ["Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"];
const container = document.getElementById('canaisContainer');

let horaAtualJS = 0;
let minutoAtualJS = 0;
let formularioCarregado = false;

for (let c = 1; c <= 8; c++) {
    let canalHtml = `
        <div class="section">
            <h2>🚰 Station ${c}</h2>
            <label>Dias de Ativação:</label>
            <div class="days-container">
                <button type="button" class="day-btn" onclick="toggleTodos(${c}, this)">Todos</button>
    `;
    diasSemana.forEach((dia, index) => {
        canalHtml += `
            <button type="button" class="day-btn canal-${c}-dia" data-dia="${index}" id="btn_c${c}_d${index}" onclick="toggleDia(this)">${dia}</button>
            <input type="hidden" name="c${c}_d${index}" id="c${c}_d${index}" value="0">
        `;
    });
    canalHtml += `</div><div class="grid-3">`;
    for (let e = 1; e <= 3; e++) {
        canalHtml += `
            <div style="border: 1px solid #eee; padding: 10px; background: white; border-radius: 4px;">
                <strong>Watering ${e}</strong>
                <div class="form-group" style="margin-top:5px;">
                    <label>Início:</label>
                    <input type="time" name="c${c}_e${e}_ini" id="c${c}_e${e}_ini">
                </div>
                <div class="form-group">
                    <label>Fim:</label>
                    <input type="time" name="c${c}_e${e}_fim" id="c${c}_e${e}_fim">
                </div>
            </div>
        `;
    }
    canalHtml += `</div>`;
    canalHtml += `<button type="button" class="manual-btn" id="btn_manual_c${c}" onclick="toggleRegaManual(${c})">🚀 Testar Canal ${c} (Manual)</button></div>`;
    container.innerHTML += canalHtml;
}
function toggleDia(btn) {
    btn.classList.toggle('active');
    const inputId = btn.id.replace('btn_', '');
    document.getElementById(inputId).value = btn.classList.contains('active') ? "1" : "0";
}

function toggleTodos(canal, btnTodos) {
    btnTodos.classList.toggle('active');
    const ativo = btnTodos.classList.contains('active');
    const botoes = document.querySelectorAll(`.canal-${canal}-dia`);
    botoes.forEach(btn => { if (ativo !== btn.classList.contains('active')) { btn.click(); } });
}

function toggleRegaManual(canal) {
    const btnManual = document.getElementById(`btn_manual_c${canal}`);
    const acao = (btnManual && btnManual.innerText.includes("Parar")) ? "desligar" : "ligar";
    fetch(`/manual?canal=${canal}&acao=${acao}`, { method: 'POST' }).then(() => atualizarStatusTempoReal());
}

function salvarLimiteUmidadeImediato(valor) {
    const dados = new URLSearchParams();
    dados.append("lim_umid", valor);
    fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: dados.toString() });
}

function sincronizarHoraOculto() {
    const agora = new Date();
    const dados = new URLSearchParams();
    dados.append("ano", agora.getFullYear());
    dados.append("mes", agora.getMonth() + 1);
    dados.append("dia", agora.getDate());
    dados.append("hora", agora.getHours());
    dados.append("minuto", agora.getMinutes());

    fetch('/ajustarHora', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: dados.toString() })
    .then(() => carregarConfiguracaoGravada(true));
}

function renderizarHoraCabecalho() {
    const hStr = String(horaAtualJS).padStart(2, '0');
    const mStr = String(minutoAtualJS).padStart(2, '0');
    document.getElementById('txt_hora_sistema').textContent = `${hStr}:${mStr}`;
}

setInterval(() => {
    minutoAtualJS++;
    if (minutoAtualJS >= 60) { minutoAtualJS = 0; horaAtualJS++; }
    if (horaAtualJS >= 24) { horaAtualJS = 0; }
    renderizarHoraCabecalho();
}, 60000);

function validarFormulario() {
    for (let c = 1; c <= 8; c++) {
        for (let e = 1; e <= 3; e++) {
            const ini = document.getElementById(`c${c}_e${e}_ini`).value;
            const fim = document.getElementById(`c${c}_e${e}_fim`).value;
            
            // 1. Se ambos estiverem vazios ou ambos forem 00:00, ignora e permite salvar
            if ((ini === "" && fim === "") || (ini === "00:00" && fim === "00:00")) {
                continue; 
            }
            
            // 2. Se o usuário preencheu os dois campos com outros horários, faz a validação padrão
            if (ini !== "" && fim !== "") {
                if (fim <= ini) {
                    alert(`Erro no Canal ${c}, Estação ${e}: O horário de FIM (${fim}) deve ser maior que o horário de INÍCIO (${ini}).`);
                    return false; 
                }
            }
        }
    }
    return true;
}


function enviarFormulario(event) {
    event.preventDefault();
    if (!validarFormulario()) return false;
    const form = document.getElementById('regaForm');
    fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(new FormData(form)).toString() })
    .then(res => { 
        if (res.ok) {
            alert("💾 Cronogramas gravados com sucesso na memória Flash!");
            formularioCarregado = false;
            carregarConfiguracaoGravada(false);
        } 
    });
}

function carregarConfiguracaoGravada(forcarHora) {
    fetch('/dados').then(response => response.json()).then(data => {
        if (data.hora_atual && (forcarHora || !formularioCarregado)) {
            const partes = data.hora_atual.split(':');
            horaAtualJS = parseInt(partes[0]) || 0;
            minutoAtualJS = parseInt(partes[1]) || 0;
            renderizarHoraCabecalho();
        }

        if (!formularioCarregado) {
            if (data.limite_umidade) document.getElementById('limite_umidade').value = data.limite_umidade;
            for (let c = 1; c <= 8; c++) {
                for (let d = 0; d < 7; d++) {
                    let btn = document.getElementById(`btn_c${c}_d${d}`);
                    let input = document.getElementById(`c${c}_d${d}`);
                    if (btn && input) {
                        if (data[`c${c}_d${d}`] == 1) { btn.classList.add('active'); input.value = "1"; }
                        else { btn.classList.remove('active'); input.value = "0"; }
                    }
                }
                for (let e = 1; e <= 3; e++) {
                    document.getElementById(`c${c}_e${e}_ini`).value = data[`c${c}_e${e}_ini`] || "";
                    document.getElementById(`c${c}_e${e}_fim`).value = data[`c${c}_e${e}_fim`] || "";
                }
            }
            formularioCarregado = true;
        }
        atualizarStatusTempoReal();
    });
}

function atualizarStatusTempoReal() {
    fetch('/dados').then(response => response.json()).then(data => {
        let statusTxt = "Status do Sensor: " + (data.sensor_presente == 0 ? "❌ Desconectado" : `✔️ Conectado | Umidade: <strong>${data.umidade_atual}/10</strong>`);
        
        // CORREÇÃO: Verifica se o ESP32 sinalizou o bloqueio automático e injeta o alerta
        if (data.bloqueio_umidade == 1) {
            statusTxt += `<br><span style="color: #dc2626; font-weight: bold;">⚠️ REGA AUTOMÁTICA INTERROMPIDA PELO SENSOR DE UMIDADE</span>`;
        }
        
        document.getElementById('status_sensor').innerHTML = statusTxt;

        for (let c = 1; c <= 8; c++) {
            let btnManual = document.getElementById(`btn_manual_c${c}`);
            if (btnManual && data[`rele_c${c}`] !== undefined) {
                if (data[`rele_c${c}`] == 1) { btnManual.classList.add('active'); btnManual.innerHTML = `🛑 Parar Teste Canal ${c} (Ativo)`; }
                else { btnManual.classList.remove('active'); btnManual.innerHTML = `🚀 Testar Canal ${c} (Manual)`; }
            }
        }
    });
}


window.onload = function() {
    sincronizarHoraOculto();
};
setInterval(atualizarStatusTempoReal, 5000);
    </script>
</body>
</html>
)=====";

#endif
