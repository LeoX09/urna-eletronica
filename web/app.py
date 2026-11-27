import time
import threading
import unicodedata
import serial
import serial.tools.list_ports
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# Estado compartilhado entre o Arduino e a interface web
porta_serial = None
serial_lock = threading.Lock()
urna_info = {
    "conectada": False,
    "porta": "",
    "estado": "DESCONECTADA",
    "modo": " ",
    "candidatos": 0,
    "eleitores": 0,
    "ultimo_evento": "",
    "logs": []
}

def registrar_log(msg):
    urna_info["logs"].append(msg)
    if len(urna_info["logs"]) > 50:
        urna_info["logs"].pop(0)

def limpar_texto_lcd(texto):
    # Remove acentos, virgulas e limita a 16 caracteres para o LCD 16x2
    nfkd = unicodedata.normalize("NFKD", texto)
    sem_acento = "".join([c for c in nfkd if not unicodedata.combining(c)])
    return sem_acento.replace(",", " ").strip()[:16]

def processar_linha_arduino(linha):
    if not linha:
        return
    registrar_log(f"<- {linha}")

    if linha.startswith("ESTADO:"):
        urna_info["estado"] = linha.split(":", 1)[1].strip()
    elif linha.startswith("OK MODO:"):
        urna_info["modo"] = linha.split(":", 1)[1].strip()
        urna_info["candidatos"] = 0
        urna_info["eleitores"] = 0
    elif linha.startswith("OK CANDIDATO:"):
        urna_info["candidatos"] += 1
    elif linha == "VOTO OK":
        urna_info["eleitores"] += 1
        urna_info["ultimo_evento"] = "Voto registrado com sucesso!"
    elif linha.startswith("STATUS "):
        # STATUS ESTADO:X MODO:Y CANDIDATOS:N ELEITORES:M
        for parte in linha.replace("STATUS ", "").split(" "):
            if ":" in parte:
                k, v = parte.split(":", 1)
                if k == "ESTADO":
                    urna_info["estado"] = v
                elif k == "MODO":
                    urna_info["modo"] = v
                elif k == "CANDIDATOS":
                    urna_info["candidatos"] = int(v)
                elif k == "ELEITORES":
                    urna_info["eleitores"] = int(v)

def leitor_background():
    global porta_serial
    while True:
        if porta_serial and porta_serial.is_open:
            try:
                if porta_serial.in_waiting > 0:
                    with serial_lock:
                        linha = porta_serial.readline().decode("utf-8", errors="ignore").strip()
                    processar_linha_arduino(linha)
            except Exception as e:
                registrar_log(f"Erro de leitura: {e}")
                urna_info["conectada"] = False
                urna_info["estado"] = "DESCONECTADA"
        time.sleep(0.05)

threading.Thread(target=leitor_background, daemon=True).start()

def enviar_comando_e_aguardar(cmd, timeout=1.5):
    if not porta_serial or not porta_serial.is_open:
        return False, ["Urna nao conectada"]

    respostas = []
    with serial_lock:
        porta_serial.reset_input_buffer()
        registrar_log(f"-> {cmd}")
        porta_serial.write((cmd + "\n").encode("utf-8"))

        inicio = time.time()
        while time.time() - inicio < timeout:
            if porta_serial.in_waiting > 0:
                linha = porta_serial.readline().decode("utf-8", errors="ignore").strip()
                if linha:
                    respostas.append(linha)
                    processar_linha_arduino(linha)
                    # Para assim que receber confirmacao ou fim de relatorio
                    if linha == "FIM" or linha.startswith(("ESTADO:", "STATUS ", "ERRO:")):
                        break
            time.sleep(0.02)
    return True, respostas

# ---------------- ROTAS DA API ----------------

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/portas", methods=["GET"])
def listar_portas():
    portas = [{"device": p.device, "desc": p.description} for p in serial.tools.list_ports.comports()]
    return jsonify(portas)

@app.route("/api/conectar", methods=["POST"])
def conectar():
    global porta_serial
    dados = request.json or {}
    porta = dados.get("porta")
    if not porta:
        return jsonify({"ok": False, "erro": "Informe a porta COM"}), 400

    try:
        if porta_serial and porta_serial.is_open:
            porta_serial.close()

        porta_serial = serial.Serial(porta, 9600, timeout=0.5)
        time.sleep(2.0)  # Aguarda o reset automatico do Arduino ao abrir Serial
        urna_info["conectada"] = True
        urna_info["porta"] = porta
        enviar_comando_e_aguardar("S")
        return jsonify({"ok": True, "status": urna_info})
    except Exception as e:
        return jsonify({"ok": False, "erro": str(e)}), 500

@app.route("/api/status", methods=["GET"])
def status():
    return jsonify(urna_info)

@app.route("/api/comando/<cmd>", methods=["POST"])
def comando(cmd):
    cmd = cmd.upper()
    if cmd not in ["M", "F", "A", "L", "C", "E", "Z", "S"]:
        return jsonify({"ok": False, "erro": "Comando nao permitido"}), 400
    ok, resp = enviar_comando_e_aguardar(cmd)
    return jsonify({"ok": ok, "respostas": resp, "status": urna_info})

@app.route("/api/candidato", methods=["POST"])
def cadastrar_candidato():
    dados = request.json or {}
    cargo = str(dados.get("cargo", "")).upper().strip()
    numero = str(dados.get("numero", "")).strip()
    nome = limpar_texto_lcd(str(dados.get("nome", "")))

    regras_digitos = {"V": 5, "P": 2, "F": 4, "E": 5, "S": 3, "G": 2, "R": 2}
    if cargo not in regras_digitos:
        return jsonify({"ok": False, "erro": "Cargo invalido"}), 400
    if not numero.isdigit() or len(numero) != regras_digitos[cargo]:
        return jsonify({"ok": False, "erro": f"O cargo {cargo} exige {regras_digitos[cargo]} digitos"}), 400
    if not nome:
        return jsonify({"ok": False, "erro": "Nome obrigatorio"}), 400

    cmd = f"P{cargo},{numero},{nome}"
    ok, resp = enviar_comando_e_aguardar(cmd)
    erro = next((r for r in resp if r.startswith("ERRO:")), None)
    return jsonify({"ok": ok and not erro, "erro": erro, "respostas": resp, "status": urna_info})

@app.route("/api/resultado", methods=["GET"])
def apurar_resultado():
    ok, linhas = enviar_comando_e_aguardar("R", timeout=3.0)
    if not ok:
        return jsonify({"ok": False, "erro": "Urna desconectada"}), 400

    boletim = {"modo": "", "eleitores": 0, "cargos": []}
    cargo_atual = None

    for l in linhas:
        if l.startswith("MODO:"):
            boletim["modo"] = l.split(":", 1)[1]
        elif l.startswith("ELEITORES:"):
            boletim["eleitores"] = int(l.split(":", 1)[1])
        elif l.startswith("CARGO:"):
            cargo_atual = {"cargo": l.split(":", 1)[1], "candidatos": [], "brancos": 0, "nulos": 0}
            boletim["cargos"].append(cargo_atual)
        elif l.startswith("BRANCOS:") and cargo_atual:
            cargo_atual["brancos"] = int(l.split(":", 1)[1])
        elif l.startswith("NULOS:") and cargo_atual:
            cargo_atual["nulos"] = int(l.split(":", 1)[1])
        elif " - " in l and ": " in l and cargo_atual:
            num_nome, votos = l.rsplit(": ", 1)
            num, nome = num_nome.split(" - ", 1)
            cargo_atual["candidatos"].append({"numero": num, "nome": nome, "votos": int(votos)})

    for c in boletim["cargos"]:
        c["candidatos"].sort(key=lambda x: x["votos"], reverse=True)

    return jsonify({"ok": True, "boletim": boletim})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)