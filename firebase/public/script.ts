// Define as interfaces de dados
interface Config {
    umidadeMin: number;
    intervaloRegaMinutos: number;
    periodoProibidoInicio: string; 
    periodoProibidoFim: string;   
}

interface LastReading {
    umidade: number;
    regando: boolean;
    timestamp: number;
}

interface NovaConfigForm {

    umidadeMin: number;
    intervaloRega: number;
}

// --- Configuração do Firebase ---
// *** ATUALIZE com as credenciais do seu projeto ***
const firebaseConfig = {
    apiKey: "AXzaXXSyBXXX2ndKXXXXOX8-5XXXXXYWBvb7XXX",
    authDomain: "rega-iot.firebaseapp.com",
    // Corrigido para usar o ID do projeto
    databaseURL: "https://rega-iot-default-rtdb.firebaseio.com", 
    projectId: "rega-iot",
    storageBucket: "rega-iot.firebasestorage.app",
    messagingSenderId: "715022772333",
    appId: "1:7A50BB772CCC:web:1186DDDD4ccCCC51BB3A8d"
};

// Inicializa o Firebase e o RTDB
declare const firebase: any;
firebase.initializeApp(firebaseConfig);
const database = firebase.database();


// --- Variáveis Globais ---
// *** SUBSTITUA PELA URL CORRETA DA SUA CLOUD FUNCTION (Ex: https://us-central1-rega-iot.cloudfunctions.net/api) ***
const API_BASE_URL: string = "https://<REGION>-rega-iot.cloudfunctions.net/api"; 

let CONFIG: Config = {
    umidadeMin: 20,
    intervaloRegaMinutos: 60,
    periodoProibidoInicio: "11:00",
    periodoProibidoFim: "14:30",
};

// --- Elementos do DOM (Tipagem para TypeScript) ---
const lcd0 = document.getElementById('lcd-line-0') as HTMLParagraphElement;
const lcd1 = document.getElementById('lcd-line-1') as HTMLParagraphElement;
const umidadeValor = document.getElementById('umidade-valor') as HTMLParagraphElement;
const umidadeBar = document.getElementById('umidade-bar') as HTMLDivElement;
const statusRele = document.getElementById('status-rele') as HTMLParagraphElement;
const logList = document.getElementById('log-list') as HTMLUListElement;
const statusConexao = document.getElementById('status-conexao') as HTMLDivElement;
const horarioDisplay = document.getElementById('horario-atual') as HTMLParagraphElement;
const periodoProibidoDisplay = document.getElementById('periodo-proibido') as HTMLParagraphElement;
const configForm = document.getElementById('config-form') as HTMLFormElement;
const umidadeMinInput = document.getElementById('umidade-min') as HTMLInputElement;
const intervaloRegaInput = document.getElementById('intervalo-rega') as HTMLInputElement;
const configMessage = document.getElementById('config-message') as HTMLParagraphElement;


// --- Funções Auxiliares (COMPLETAS) ---

function log(message: string, type: string = 'INFO') {
    const li = document.createElement('li');
    li.innerHTML = `[${new Date().toLocaleTimeString()}] **${type}**: ${message}`;
    logList.prepend(li);
    if (logList.children.length > 20) logList.removeChild(logList.lastChild as Node);
}

function updateLCD(line0: string, line1: string) {
    lcd0.textContent = line0.padEnd(16);
    lcd1.textContent = line1.padEnd(16);
}

function updateConnectionStatus(connected: boolean) {
    statusConexao.className = connected ? 'status-box status-connected' : 'status-box status-disconnected';
    statusConexao.innerHTML = connected ? '<span class="icon">✅</span> Conectado IoT' : '<span class="icon">❌</span> DESCONECTADO';
}

function updateUmidade(umidade: number) {
    umidadeValor.textContent = `${umidade}%`;
    umidadeBar.style.width = `${umidade}%`;
    let color;
    if (umidade < CONFIG.umidadeMin) color = '#dc3545';
    else if (umidade <= 60) color = '#ffc107'; // Usando 60% como limite do Arduino
    else color = '#28a745';
    umidadeBar.style.backgroundColor = color;
}

function updateReleStatus(regando: boolean) {
    statusRele.textContent = regando ? "LIGADA (REGA)" : "DESLIGADA";
    statusRele.className = regando ? 'status-on' : 'status-off';
}

function estaEmPeriodoProibido(): boolean {
    const [inicioH, inicioM] = CONFIG.periodoProibidoInicio.split(':').map(Number);
    const [fimH, fimM] = CONFIG.periodoProibidoFim.split(':').map(Number);
    const hora = new Date().getHours();
    const minuto = new Date().getMinutes();
    const agoraEmMinutos = hora * 60 + minuto;
    const inicioEmMinutos = inicioH * 60 + inicioM;
    const fimEmMinutos = fimH * 60 + fimM;
    // Período proibido é [início, fim[ (Ex: 11:00 até 14:29:59)
    return (agoraEmMinutos >= inicioEmMinutos && agoraEmMinutos < fimEmMinutos);
}


// --- Funções de Comunicação (API e RTDB) ---

// 1. Ouvinte em tempo real para as configurações
function subscribeToConfig(): void {
    database.ref("config").on('value', (snapshot: any) => {
        const newConfig = snapshot.val() as Config;
        if (newConfig) {
            CONFIG = { ...CONFIG, ...newConfig }; 
            umidadeMinInput.value = String(CONFIG.umidadeMin);
            intervaloRegaInput.value = String(CONFIG.intervaloRegaMinutos);
            log("Configurações atualizadas em tempo real.", "RTDB");
        }
    }, (error: any) => {
        log(`ERRO RTDB Config: ${error.message}`, "ERRO");
    });
}

// 2. Ouvinte em tempo real para a última leitura do sensor
function subscribeToSensorData(): void {
    database.ref("sensor/lastReading").on('value', (snapshot: any) => {
        const data = snapshot.val() as LastReading;
         if (data && data.umidade !== undefined) {
            
            updateUmidade(data.umidade);
            updateReleStatus(data.regando);
            updateConnectionStatus(true);
            
            // Simula a lógica do LCD do ESP32 no Front-end
            const umidadeTexto = `Umidade:${data.umidade}%`;
            let statusTexto: string = "Monitorando...";

            if (data.regando) {
                statusTexto = "Bomba Ligada";
            } else if (data.umidade < CONFIG.umidadeMin) {
                statusTexto = "Solo Seco! Regar!";
            } else if (data.umidade > 60) { // Limite do Arduino
                 statusTexto = "Solo Encharcado!";
            } else {
                statusTexto = "Umidade OK.     ";
            }

            updateLCD(umidadeTexto, statusTexto);
            log(`Dados recebidos: ${data.umidade}%`, "RTDB");

        } else {
             updateConnectionStatus(false); 
        }
    }, (error: any) => {
        log(`ERRO RTDB Sensor: ${error.message}`, "ERRO");
        updateConnectionStatus(false);
    });
}


// 3. Função para salvar a configuração (usa Cloud Function POST)
async function saveConfig(newConfig: NovaConfigForm): Promise<void> {
    const updatedConfig = {
        umidadeMin: newConfig.umidadeMin,
        intervaloRegaMinutos: newConfig.intervaloRega,
    };
    
    try {
        const response = await fetch(`${API_BASE_URL}/config`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(updatedConfig)
        });

        if (!response.ok) throw new Error("Falha ao salvar config");

        configMessage.textContent = 'Configurações salvas com sucesso!';
        setTimeout(() => configMessage.textContent = '', 3000);
        log(`Parâmetros enviados para a API: UMIDADE_MINIMA=${newConfig.umidadeMin}%`, "API");
        
    } catch (error) {
        log(`ERRO ao salvar: ${(error as Error).message}`, "ERRO");
        configMessage.textContent = 'ERRO: Não foi possível salvar as configurações na API.';
    }
}


// --- Event Listeners e Setup ---

configForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newConfig: NovaConfigForm = {
        umidadeMin: parseInt(umidadeMinInput.value),
        intervaloRega: parseInt(intervaloRegaInput.value)
    };
    saveConfig(newConfig);
});


// Setup
subscribeToConfig();
subscribeToSensorData();
// Loop para atualização local do horário
setInterval(() => {
    let now = new Date();
    horarioDisplay.textContent = now.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    
    const proibido = estaEmPeriodoProibido();
    periodoProibidoDisplay.className = proibido ? 'status-proibido' : 'status-on';
    periodoProibidoDisplay.textContent = proibido ? 'Proibido (11h-14:30h)' : 'Rega Liberada';
    
}, 1000);
