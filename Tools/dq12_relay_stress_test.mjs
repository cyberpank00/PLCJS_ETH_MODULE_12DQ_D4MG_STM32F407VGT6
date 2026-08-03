#!/usr/bin/env node
// @ts-check
/*
 * dq12_relay_stress_test.mjs - End-to-end relay stress test.
 *
 * Stand: the 12 relay contacts driven by a 12DO module are wired to the inputs
 * of a 12DI module. This tool commands random 12-bit patterns on the outputs
 * and verifies the *actual physical* relay state read back from the DI module,
 * measuring real relay propagation time and catching stuck / mis-wired / non-
 * switching channels.
 *
 *   DO (.11): FC06 write holding reg 50 = 12-bit output mask (bit i -> DQ i+1)
 *   DI (.10): FC04 read input regs 0..11 = DI1..DI12 filtered state (0/1)
 *
 * Startup calibration (DQ=0x000 then 0xFFF) determines mapping/polarity and
 * flags channels that do not respond.
 *
 * No external deps (node:net). Node 18+.
 *
 * Usage:
 *   node dq12_relay_stress_test.mjs [--do-ip .11] [--di-ip .10] [--hz 5]
 *        [--hours 24] [--seconds 0] [--seed N] [--invert] [--settle 150]
 *        [--timeout 1000] [--progress 60] [--port 502] [--unit 1]
 */

import net from 'node:net';
import process from 'node:process';
import { performance } from 'node:perf_hooks';

function argVal(name, def) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const hasFlag = (n) => process.argv.includes(`--${n}`);
const CFG = {
  doIp: argVal('do-ip', '192.168.1.11'),
  diIp: argVal('di-ip', '192.168.1.10'),
  port: Number(argVal('port', '502')),
  unit: Number(argVal('unit', '1')),
  hz: Number(argVal('hz', '5')),
  hours: Number(argVal('hours', '24')),
  seconds: Number(argVal('seconds', '0')),
  seed: argVal('seed', ''),
  invert: hasFlag('invert'),          // relays wired NC (DI = ~DQ)
  verifyMs: Number(argVal('settle', '150')), // max time to wait for DI to reflect DQ
  timeoutMs: Number(argVal('timeout', '1000')),
  progressS: Number(argVal('progress', '60')),
  filterMs: Number(argVal('filter', '10')),
};
const DQ_COUNT = 12;
const MASK_12 = (1 << DQ_COUNT) - 1;
const MB_HR_DQ_GROUP = 50;
const MB_HR_DI_FILTER = 100;
const MB_IR_UPTIME_LO = 122;
const MB_IR_MODULE_ID = 125;

function makeRng(seedStr) {
  if (!seedStr) return () => Math.floor(Math.random() * (MASK_12 + 1));
  let a = (Number(seedStr) >>> 0) || 0x9e3779b9;
  return () => { a |= 0; a = (a + 0x6d2b79f5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) & MASK_12; };
}

class ModbusError extends Error {}
class ModbusTcpClient {
  constructor(ip, port, unitId, timeoutMs = 5000) {
    this.ip = ip; this.port = port; this.unitId = unitId; this.timeoutMs = timeoutMs;
    this.socket = null; this.txId = 0; this.rxBuf = Buffer.alloc(0); this.pending = [];
  }
  connect() {
    return new Promise((resolve, reject) => {
      const sock = net.createConnection({ host: this.ip, port: this.port });
      const onError = (err) => reject(err);
      sock.once('error', onError); sock.setNoDelay(true);
      sock.once('connect', () => {
        sock.removeListener('error', onError); this.socket = sock;
        sock.on('data', (c) => this._onData(c));
        sock.on('error', (e) => this._failAll(e));
        sock.on('close', () => this._failAll(new ModbusError('connection closed')));
        resolve();
      });
    });
  }
  close() { if (this.socket) { this.socket.removeAllListeners(); this.socket.destroy(); this.socket = null; } }
  _failAll(err) { for (const w of this.pending.splice(0)) w.reject(err); }
  _onData(chunk) {
    this.rxBuf = Buffer.concat([this.rxBuf, chunk]);
    while (this.rxBuf.length >= 7) {
      const len = this.rxBuf.readUInt16BE(4); const total = 6 + len;
      if (this.rxBuf.length < total) break;
      const frame = this.rxBuf.subarray(0, total); this.rxBuf = this.rxBuf.subarray(total);
      const txId = frame.readUInt16BE(0); const pdu = frame.subarray(7);
      const idx = this.pending.findIndex((w) => w.txId === txId);
      if (idx >= 0) { const [w] = this.pending.splice(idx, 1); w.resolve(pdu); }
    }
  }
  _request(pdu) {
    if (!this.socket) return Promise.reject(new ModbusError('not connected'));
    this.txId = (this.txId + 1) & 0xffff; const txId = this.txId;
    const mbap = Buffer.alloc(7);
    mbap.writeUInt16BE(txId, 0); mbap.writeUInt16BE(0, 2); mbap.writeUInt16BE(pdu.length + 1, 4); mbap.writeUInt8(this.unitId, 6);
    const frame = Buffer.concat([mbap, pdu]);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { const i = this.pending.findIndex((w) => w.txId === txId); if (i >= 0) this.pending.splice(i, 1); reject(new ModbusError(`timeout txId=${txId}`)); }, this.timeoutMs);
      this.pending.push({ txId, resolve: (p) => { clearTimeout(timer); resolve(p); }, reject: (e) => { clearTimeout(timer); reject(e); } });
      this.socket.write(frame);
    });
  }
  _check(pdu, fn) { const f = pdu.readUInt8(0); if (f === (fn | 0x80)) throw new ModbusError(`exception ${pdu.readUInt8(1)} fn ${fn}`); if (f !== fn) throw new ModbusError(`unexpected fn ${f}`); }
  async readInputRegisters(addr, qty) {
    const pdu = Buffer.alloc(5); pdu.writeUInt8(0x04, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(qty, 3);
    const r = await this._request(pdu); this._check(r, 0x04);
    const n = r.readUInt8(1); const regs = []; for (let i = 0; i < n / 2; i++) regs.push(r.readUInt16BE(2 + i * 2)); return regs;
  }
  async writeSingleRegister(addr, value) {
    const pdu = Buffer.alloc(5); pdu.writeUInt8(0x06, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(value & 0xffff, 3);
    const r = await this._request(pdu); this._check(r, 0x06);
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ts = () => new Date().toISOString().replace('T', ' ').replace('Z', '');
const avg = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN);
const chans = (mask) => { const c = []; for (let b = 0; b < DQ_COUNT; b++) if (mask & (1 << b)) c.push(b + 1); return c.join(','); };

async function main() {
  const durationMs = (CFG.hours * 3600 + CFG.seconds) * 1000;
  const periodMs = CFG.hz > 0 ? 1000 / CFG.hz : 0;
  const rng = makeRng(CFG.seed);
  let invert = CFG.invert;

  console.log('12DO->relays->12DI end-to-end relay stress test');
  console.log(`  DO (out): ${CFG.doIp}:${CFG.port} HR${MB_HR_DQ_GROUP}`);
  console.log(`  DI (in) : ${CFG.diIp}:${CFG.port} IR0..11 (actual relay state)`);
  console.log(`  rate ${CFG.hz} Hz, duration ${(durationMs / 3600000).toFixed(2)} h${CFG.seed ? `, seed=${CFG.seed}` : ''}`);

  let doCli = new ModbusTcpClient(CFG.doIp, CFG.port, CFG.unit, CFG.timeoutMs);
  let diCli = new ModbusTcpClient(CFG.diIp, CFG.port, CFG.unit, CFG.timeoutMs);
  await doCli.connect(); await diCli.connect();

  const doId = (await doCli.readInputRegisters(MB_IR_MODULE_ID, 1))[0];
  const diId = (await diCli.readInputRegisters(MB_IR_MODULE_ID, 1))[0];
  console.log(`  module IDs: DO=0x${doId.toString(16)} (exp 12d0), DI=0x${diId.toString(16)} (exp 12d1)`);
  if (doId !== 0x12d0) console.warn('  WARN: DO module id is not 0x12d0');
  if (diId !== 0x12d1) console.warn('  WARN: DI module id is not 0x12d1');

  try { await diCli.writeSingleRegister(MB_HR_DI_FILTER, CFG.filterMs); console.log(`  DI filter set to ${CFG.filterMs} ms`); }
  catch (e) { console.warn(`  could not set DI filter: ${e.message}`); }

  const readDiMask = async () => {
    const regs = await diCli.readInputRegisters(0, DQ_COUNT);
    let m = 0; for (let i = 0; i < DQ_COUNT; i++) if (regs[i] & 1) m |= (1 << i); return m;
  };
  const setDq = (v) => doCli.writeSingleRegister(MB_HR_DQ_GROUP, v & MASK_12);
  const settleRead = async (ms) => { await sleep(ms); return readDiMask(); };

  // ---- Startup calibration: mapping / polarity / dead channels ----
  await setDq(0x000); const diAt0 = await settleRead(300);
  await setDq(MASK_12); const diAtF = await settleRead(300);
  console.log(`  calibration: DQ=0x000 -> DI=0x${diAt0.toString(16).padStart(3, '0')} | DQ=0xFFF -> DI=0x${diAtF.toString(16).padStart(3, '0')}`);
  if (!CFG.invert) {
    if (diAt0 === 0x000 && diAtF === MASK_12) { invert = false; console.log('  mapping: DIRECT (DI == DQ), all 12 channels respond'); }
    else if (diAt0 === MASK_12 && diAtF === 0x000) { invert = true; console.log('  mapping: INVERTED (DI == ~DQ) — auto-enabled'); }
    else {
      const responded = (diAtF ^ diAt0) & MASK_12;
      const dead = (~responded) & MASK_12;
      console.warn(`  mapping: PARTIAL — responding ch: {${chans(responded)}}; NOT responding ch: {${chans(dead)}} (check wiring/relay)`);
      console.warn('  proceeding with DIRECT mapping; non-responding channels will count as mismatches');
    }
  }
  const expected = (cmd) => (invert ? (~cmd & MASK_12) : (cmd & MASK_12));
  await setDq(0x000); await sleep(300);

  // ---- counters ----
  let cycles = 0, ok = 0, mism = 0, commDO = 0, commDI = 0, reconn = 0, resetDO = 0, resetDI = 0;
  const chMism = new Array(DQ_COUNT).fill(0);
  const prop = { min: Infinity, max: 0, sum: 0, n: 0 };
  let logged = 0; const LOG_CAP = 300;
  let printed = false, stopping = false;
  const logEvent = (m) => { if (logged < LOG_CAP) { logged++; console.warn(`  [${ts()}] ${m}`); } };

  const readUptime = async (cli) => { const r = await cli.readInputRegisters(MB_IR_UPTIME_LO, 2); return (r[0] | (r[1] << 16)) >>> 0; };
  let lastUpDO = await readUptime(doCli), lastUpDI = await readUptime(diCli);

  const reconnect = async (which) => {
    reconn++;
    for (let a = 1; !stopping; a++) {
      try {
        if (which === 'DO') { doCli.close(); doCli = new ModbusTcpClient(CFG.doIp, CFG.port, CFG.unit, CFG.timeoutMs); await doCli.connect(); }
        else { diCli.close(); diCli = new ModbusTcpClient(CFG.diIp, CFG.port, CFG.unit, CFG.timeoutMs); await diCli.connect(); }
        logEvent(`${which} reconnected (attempt ${a})`); return;
      } catch (e) { if (a === 1 || a % 20 === 0) logEvent(`${which} reconnect failing (a${a}): ${e.message}`); await sleep(Math.min(2000, 200 * a)); }
    }
  };

  const printResults = async (reason) => {
    if (printed) return; printed = true;
    const totalS = (performance.now() - tStart) / 1000;
    try { await setDq(0x000); } catch { /* ignore */ }
    doCli.close(); diCli.close();
    const t = prop;
    console.log(`\n============= RELAY STRESS RESULTS (${reason}) =============`);
    console.log(`duration            : ${(totalS / 3600).toFixed(3)} h (${totalS.toFixed(0)} s)`);
    console.log(`mapping             : ${invert ? 'INVERTED (DI=~DQ)' : 'DIRECT (DI=DQ)'}`);
    console.log(`cycles (patterns)   : ${cycles}`);
    console.log(`  relay-verified OK : ${ok}`);
    console.log(`  mismatches        : ${mism}`);
    console.log(`comm errors DO/DI   : ${commDO} / ${commDI}   reconnects: ${reconn}`);
    console.log(`module resets DO/DI : ${resetDO} / ${resetDI}`);
    console.log(`achieved rate       : ${(cycles / totalS).toFixed(2)} Hz`);
    if (t.n) console.log(`relay propagation ms: min ${t.min.toFixed(1)} | avg ${(t.sum / t.n).toFixed(1)} | max ${t.max.toFixed(1)}`);
    console.log(`per-channel mismatches (DQ1..DQ12): [${chMism.join(', ')}]`);
    const verdict = (mism === 0 && commDO === 0 && commDI === 0 && resetDO === 0 && resetDI === 0) ? 'PASS - all relays switched correctly' : 'ATTENTION - see counters/log';
    console.log(`verdict             : ${verdict}`);
    console.log('===========================================================');
    process.exit(mism || commDO || commDI || resetDO || resetDI ? 2 : 0);
  };

  const stop = (s) => { if (!stopping) { stopping = true; console.log(`\n[${s}] stopping...`); } };
  process.on('SIGINT', () => stop('SIGINT'));
  process.on('SIGTERM', () => stop('SIGTERM'));

  const tStart = performance.now();
  let lastProgress = tStart;

  while (!stopping && (performance.now() - tStart) < durationMs) {
    const cycleStart = performance.now();
    const cmd = rng() & MASK_12;
    const exp = expected(cmd);
    cycles++;
    try {
      const t0 = performance.now();
      await setDq(cmd);
      // Poll actual relay state on the DI module until it matches (or timeout).
      let got = -1, matched = false;
      while (performance.now() - t0 <= CFG.verifyMs) {
        got = await readDiMask();
        if (got === exp) { matched = true; break; }
      }
      const dt = performance.now() - t0;
      if (matched) {
        ok++; prop.n++; prop.sum += dt; if (dt < prop.min) prop.min = dt; if (dt > prop.max) prop.max = dt;
      } else {
        mism++;
        const diff = (got ^ exp) & MASK_12;
        for (let b = 0; b < DQ_COUNT; b++) if (diff & (1 << b)) chMism[b]++;
        logEvent(`MISMATCH: DQ=0x${cmd.toString(16).padStart(3, '0')} exp DI=0x${exp.toString(16).padStart(3, '0')} got 0x${(got & MASK_12).toString(16).padStart(3, '0')} (bad ch: ${chans(diff)}) after ${dt.toFixed(0)}ms`);
      }
    } catch (e) {
      const which = /:\s*192\.168\.142\.52|DO/.test(e.message) ? 'DO' : 'DI';
      // We can't tell which socket failed from the message reliably; probe both.
      if (!doCli.socket) { commDO++; await reconnect('DO'); }
      else if (!diCli.socket) { commDI++; await reconnect('DI'); }
      else { commDI++; logEvent(`comm error: ${e.message}`); }
    }

    const now = performance.now();
    if (now - lastProgress >= CFG.progressS * 1000) {
      lastProgress = now;
      try {
        const upDO = await readUptime(doCli); const upDI = await readUptime(diCli);
        if (upDO + 2 < lastUpDO) { resetDO++; logEvent(`DO RESET: uptime ${lastUpDO}->${upDO}s`); }
        if (upDI + 2 < lastUpDI) { resetDI++; logEvent(`DI RESET: uptime ${lastUpDI}->${upDI}s`); }
        lastUpDO = upDO; lastUpDI = upDI;
        console.log(`  [${((now - tStart) / 3600000).toFixed(3)}h] cyc=${cycles} ok=${ok} mism=${mism} commDO=${commDO} commDI=${commDI} rec=${reconn} rstDO=${resetDO} rstDI=${resetDI} upDO=${upDO}s upDI=${upDI}s`);
      } catch { /* ignore progress read errors */ }
    }

    if (periodMs > 0) { const wait = periodMs - (performance.now() - cycleStart); if (wait > 0) await sleep(wait); }
  }
  await printResults(stopping ? 'stopped' : 'completed');
}

main().catch((e) => { console.error('FATAL:', e.message); process.exit(1); });
