import * as functions from "firebase-functions";
import * as admin from "firebase-admin";
import * as express from "express";
import * as cors from "cors";

// Inicializa o Firebase Admin SDK e o RTDB
admin.initializeApp();
const db = admin.database();

const app = express();
app.use(cors({ origin: true }));
app.use(express.json());

interface Config {
    umidadeMin: number;
    intervaloRegaMinutos: number;
    periodoProibidoInicio: string; 
    periodoProibidoFim: string; 
}

// --- Rota de Configuração (Front-end: GET/POST) ---
app.get("/config", async (req, res) => {
    try {
        const snapshot = await db.ref("config").once("value");
        res.json(snapshot.val() || { umidadeMin: 25, intervaloRegaMinutos: 60, periodoProibidoInicio: "11:00", periodoProibidoFim: "14:30" });
    } catch (error) {
        functions.logger.error("Erro RTDB GET config:", error);
        res.status(500).send("Erro interno ao buscar configurações.");
    }
});

app.post("/config", async (req, res) => {
    const { umidadeMin, intervaloRegaMinutos } = req.body as Config;
    
    if (umidadeMin === undefined || intervaloRegaMinutos === undefined) {
        return res.status(400).send("Campos obrigatórios ausentes.");
    }

    try {
        await db.ref("config").update({
            umidadeMin,
            intervaloRegaMinutos,
            dataUltimaAtualizacao: admin.database.ServerValue.TIMESTAMP,
            periodoProibidoInicio: "11:00",
            periodoProibidoFim: "14:30"
        });

        res.status(200).send("Configurações salvas no RTDB.");
    } catch (error) {
        functions.logger.error("Erro RTDB POST config:", error);
        res.status(500).send("Erro interno ao salvar configurações.");
    }
});

// --- Rota de Dados (ESP32: POST) ---
app.post("/sensor", async (req, res) => {
    // Note que 'timestamp' é o valor time(NULL) enviado pelo ESP32 (timestamp Unix)
    const { umidade, regando, timestamp } = req.body as { umidade: number, regando: boolean, timestamp: number };
    
    if (umidade === undefined) {
        return res.status(400).send("Umidade é obrigatória.");
    }

    try {
        const leituraData = {
            umidade,
            regando,
            timestamp 
        };
        
        // 1. Salva a leitura no histórico
        await db.ref("sensor/leituras").push(leituraData);

        // 2. Atualiza a última leitura (para o Front-end buscar em tempo real)
        await db.ref("sensor/lastReading").set(leituraData);

        res.status(200).send("Dados do sensor recebidos e processados.");
    } catch (error) {
        functions.logger.error("Erro RTDB POST sensor:", error);
        res.status(500).send("Erro interno ao processar dados do sensor.");
    }
});

// Exporta o aplicativo Express como a função 'api'
exports.api = functions.https.onRequest(app);
