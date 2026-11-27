const CARGOS = {
  M: [
    { cod: 'V', nome: 'Vereador', dig: 5 },
    { cod: 'P', nome: 'Prefeito', dig: 2 }
  ],
  F: [
    { cod: 'F', nome: 'Dep. Federal', dig: 4 },
    { cod: 'E', nome: 'Dep. Estadual', dig: 5 },
    { cod: 'S', nome: 'Senador', dig: 3 },
    { cod: 'G', nome: 'Governador', dig: 2 },
    { cod: 'R', nome: 'Presidente', dig: 2 }
  ]
};

const NOMES_CARGOS = {
  V: 'Vereador', P: 'Prefeito',
  F: 'Deputado Federal', E: 'Deputado Estadual',
  S: 'Senador', G: 'Governador', R: 'Presidente'
};

let modoAtual = '';

function formatarLCD(l1, l2) {
  const pad = s => (String(s || '') + '                ').slice(0, 16);
  return pad(l1) + '\n' + pad(l2);
}

function atualizarEspelhoLCD(st) {
  const lcd = document.getElementById('lcd');
  switch (st.estado) {
    case 'AGUARDANDO_CONFIG':
      lcd.textContent = formatarLCD('Urna Sem Modo', 'Aguardando..');
      break;
    case 'PRONTA':
      lcd.textContent = formatarLCD(
        'MODO: ' + (st.modo === 'M' ? 'Municipal' : 'Federal'),
        'Cand: ' + st.candidatos
      );
      break;
    case 'BLOQUEADA':
      lcd.textContent = formatarLCD('Aguardando', 'liberacao');
      break;
    case 'LIBERADA':
      lcd.textContent = formatarLCD('Votacao em curso', 'Digite na urna..');
      break;
    case 'ENCERRADA':
      lcd.textContent = formatarLCD('Votacao fechada', 'Eleitores: ' + st.eleitores);
      break;
    default:
      lcd.textContent = formatarLCD('Urna desconectada', 'Aguardando..');
  }
}

async function carregarPortas() {
  try {
    const res = await fetch('/api/portas');
    const portas = await res.json();
    const sel = document.getElementById('selectPorta');
    sel.innerHTML = portas.length
      ? portas.map(p => `<option value="${p.device}" ${p.device === 'COM8' ? 'selected' : ''}>${p.device} — ${p.desc}</option>`).join('')
      : '<option value="">Nenhuma porta encontrada</option>';
  } catch (e) {
    console.error(e);
  }
}

async function conectar() {
  const porta = document.getElementById('selectPorta').value;
  if (!porta) return alert('Selecione uma porta serial.');
  const res = await fetch('/api/conectar', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ porta })
  });
  const data = await res.json();
  if (!data.ok) return alert('Erro ao conectar: ' + data.erro);
  atualizarUI(data.status);
}

async function enviarCmd(cmd) {
  const res = await fetch(`/api/comando/${cmd}`, { method: 'POST' });
  const data = await res.json();
  if (data.status) atualizarUI(data.status);
}

function atualizarCargosSelect(modo) {
  if (modo === modoAtual) return;
  modoAtual = modo;
  const sel = document.getElementById('selCargo');
  const lista = CARGOS[modo] || [];
  sel.innerHTML = lista.map(c => `<option value="${c.cod}" data-dig="${c.dig}">${c.nome} (${c.dig} dígitos)</option>`).join('');
  ajustarDigitos();
}

function ajustarDigitos() {
  const sel = document.getElementById('selCargo');
  const opt = sel.options[sel.selectedIndex];
  const dig = opt ? opt.dataset.dig : 5;
  const inp = document.getElementById('inpNumero');
  inp.maxLength = dig;
  inp.placeholder = `${dig} dígitos`;
}

async function cadastrar(e) {
  e.preventDefault();
  const payload = {
    cargo: document.getElementById('selCargo').value,
    numero: document.getElementById('inpNumero').value,
    nome: document.getElementById('inpNome').value
  };
  const res = await fetch('/api/candidato', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  const data = await res.json();
  if (!data.ok) return alert(data.erro || 'Erro ao cadastrar');
  document.getElementById('inpNumero').value = '';
  document.getElementById('inpNome').value = '';
  atualizarUI(data.status);
}

async function apurar() {
  const res = await fetch('/api/resultado');
  const data = await res.json();
  if (!data.ok) return alert(data.erro);

  const b = data.boletim;
  document.getElementById('resumoApuracao').innerHTML =
    `<strong>Modo:</strong> ${b.modo === 'M' ? 'Municipal' : 'Federal'} · <strong>Total de eleitores:</strong> ${b.eleitores}`;

  let html = '';
  for (const c of b.cargos) {
    const titulo = NOMES_CARGOS[c.cargo] || c.cargo;
    const todosVotos = [...c.candidatos.map(x => x.votos), c.brancos, c.nulos];
    const maxVotos = Math.max(1, ...todosVotos);

    html += `<h3>${titulo}</h3>`;
    for (const cand of c.candidatos) {
      const pct = (cand.votos / maxVotos) * 100;
      html += `
        <div class="cand">
          <div class="cand-info"><code>${cand.numero}</code>${cand.nome}</div>
          <div class="track"><div class="fill" style="width:${pct}%"></div></div>
          <span class="num">${cand.votos}</span>
        </div>`;
    }

    const pctBranco = (c.brancos / maxVotos) * 100;
    const pctNulo = (c.nulos / maxVotos) * 100;
    html += `
      <div class="cand">
        <div class="cand-info" style="color:var(--mute)">Votos em Branco</div>
        <div class="track"><div class="fill neutral" style="width:${pctBranco}%"></div></div>
        <span class="num">${c.brancos}</span>
      </div>
      <div class="cand">
        <div class="cand-info" style="color:var(--mute)">Votos Nulos</div>
        <div class="track"><div class="fill neutral" style="width:${pctNulo}%"></div></div>
        <span class="num">${c.nulos}</span>
      </div>`;
  }
  document.getElementById('areaBoletim').innerHTML = html;
}

async function zerarUrna() {
  if (!confirm('Tem certeza que deseja apagar TODOS os candidatos e votos da EEPROM?')) return;
  await enviarCmd('Z');
  document.getElementById('areaBoletim').innerHTML = '';
  document.getElementById('resumoApuracao').innerHTML = 'Encerre a votação para liberar a leitura do comando <code>R</code>.';
}

function atualizarUI(st) {
  const badge = document.getElementById('badgeEstado');
  badge.textContent = st.estado;
  badge.className = 'badge' + (st.estado === 'LIBERADA' ? ' on' : (st.estado === 'BLOQUEADA' ? ' warn' : ''));

  document.getElementById('stModo').textContent = st.modo === 'M' ? 'Municipal' : (st.modo === 'F' ? 'Federal' : 'Não definido');
  document.getElementById('stCand').textContent = `${st.candidatos} / 38`;
  document.getElementById('stEleit').textContent = st.eleitores;

  atualizarEspelhoLCD(st);

  if (st.modo === 'M' || st.modo === 'F') atualizarCargosSelect(st.modo);

  const e = st.estado;
  document.getElementById('btnModoM').disabled = !(e === 'AGUARDANDO_CONFIG' || e === 'PRONTA');
  document.getElementById('btnModoF').disabled = !(e === 'AGUARDANDO_CONFIG' || e === 'PRONTA');
  document.getElementById('btnCadastrar').disabled = !(e === 'PRONTA' && st.candidatos < 38);
  document.getElementById('btnAbrir').disabled = (e !== 'PRONTA');
  document.getElementById('btnLiberar').disabled = (e !== 'BLOQUEADA');
  document.getElementById('btnCancelar').disabled = (e !== 'LIBERADA');
  document.getElementById('btnEncerrar').disabled = (e !== 'BLOQUEADA');
  document.getElementById('btnApurar').disabled = (e !== 'ENCERRADA');
  document.getElementById('btnZerar').disabled = (e !== 'ENCERRADA');

  const box = document.getElementById('log');
  const estavaNoFim = box.scrollTop + box.clientHeight >= box.scrollHeight - 10;
  box.innerHTML = st.logs.map(linha => {
    let cls = 'rx';
    if (linha.startsWith('->')) cls = 'tx';
    else if (linha.includes('ERRO') || linha.includes('Erro')) cls = 'er';
    const texto = linha.replace(/^-> /, '→ ').replace(/^<- /, '← ');
    return `<div class="${cls}">${texto}</div>`;
  }).join('');
  if (estavaNoFim) box.scrollTop = box.scrollHeight;
}

setInterval(async () => {
  try {
    const res = await fetch('/api/status');
    const st = await res.json();
    atualizarUI(st);
  } catch (e) {}
}, 1500);

carregarPortas();