#!/usr/bin/env node
// @ts-check
/*
 * dq_di_loopback_test.mjs - Signal-path loopback test between two PLCJS modules.
 *
 * Physically wire DQ0 (12DO output) -> DI0 (12DI input). This tool toggles the
 * output over Modbus TCP and measures how long until the input reflects the
 * change, repeated for many edges, reporting throughput and propagation stats.
 *
 *   12DO (outputs): FC06 write holding reg 51 = DQ1 value (0/1)
 *   12DI (inputs):  FC04 read  input   reg 0  = DI1 state (0/1)
 *
 * No external dependencies (built-in node:net). Requires Node 18+.
 *
 * Usage:
 *   node dq_di_loopback_test.mjs [--do-ip A] [--di-ip B] [--edges N]
 *        [--seconds S] [--do-reg 51] [--di-reg 0] [--filter 10]
 *        [--settle 0] [--timeout 2000] [--port 502] [--unit 1]
 */

import net from 'node:net';
import process from 'node:process';
import { performance } from 'node:perf_hooks';

// ---- CLI ------------------------------------------------------------------
function argVal(name, def) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const CFG = {
  doIp: argVal('do-ip', '192.168.142.52'),
  diIp: argVal('di-ip', '192.168.142.88'),
  port: Number(argVal('port', '502')),
  unit: Number(argVal('unit', '1')),
  edges: Number(argVal('edges', '100')),
  seconds: Number(argVal('seconds', '0')), // if >0, run by time instead of edge count
  hz: Number(argVal('hz', '0')),           // output square-wave freq; edge period = 500/hz ms (0 = as fast as possible)
  doReg: Number(argVal('do-reg', '51')), // HR for DQ1 value
  diReg: Number(argVal('di-reg', '0')),  // IR for DI1 state
  filterMs: Number(argVal('filter', '10')), // DI filter (HR100); <0 => leave as-is
  settleMs: Number(argVal('settle', '0')),  // gap between edges
  timeoutMs: Number(argVal('timeout', '2000')), // per-edge wait timeout
};

const MB_HR_DI_FILTER_MS = 100;
const MB_IR_MODULE_ID = 125;

// ---- Minimal Modbus TCP client (from fw_update.mjs) -----------------------
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
      sock.once('error', onError);
      sock.setNoDelay(true);
      sock.once('connect', () => {
        sock.removeListener('error', onError);
        this.socket = sock;
        sock.on('data', (chunk) => this._onData(chunk));
        sock.on('error', (err) => this._failAll(err));
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
      const len = this.rxBuf.readUInt16BE(4);
      const total = 6 + len;
      if (this.rxBuf.length < total) break;
      const frame = this.rxBuf.subarray(0, total);
      this.rxBuf = this.rxBuf.subarray(total);
      const txId = frame.readUInt16BE(0);
      const pdu = frame.subarray(7);
      const idx = this.pending.findIndex((w) => w.txId === txId);
      if (idx >= 0) { const [w] = this.pending.splice(idx, 1); w.resolve(pdu); }
    }
  }
  _request(pdu) {
    if (!this.socket) return Promise.reject(new ModbusError('not connected'));
    this.txId = (this.txId + 1) & 0xffff;
    const txId = this.txId;
    const mbap = Buffer.alloc(7);
    mbap.writeUInt16BE(txId, 0); mbap.writeUInt16BE(0, 2);
    mbap.writeUInt16BE(pdu.length + 1, 4); mbap.writeUInt8(this.unitId, 6);
    const frame = Buffer.concat([mbap, pdu]);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const i = this.pending.findIndex((w) => w.txId === txId);
        if (i >= 0) this.pending.splice(i, 1);
        reject(new ModbusError(`Modbus timeout (txId=${txId})`));
      }, this.timeoutMs);
      this.pending.push({
        txId,
        resolve: (p) => { clearTimeout(timer); resolve(p); },
        reject: (e) => { clearTimeout(timer); reject(e); },
      });
      this.socket.write(frame);
    });
  }
  _check(pdu, fn) {
    const f = pdu.readUInt8(0);
    if (f === (fn | 0x80)) throw new ModbusError(`Modbus exception ${pdu.readUInt8(1)} for fn ${fn}`);
    if (f !== fn) throw new ModbusError(`Unexpected fn ${f} (expected ${fn})`);
  }
  async readInputRegisters(addr, qty) {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x04, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(qty, 3);
    const r = await this._request(pdu); this._check(r, 0x04);
    const n = r.readUInt8(1); const regs = [];
    for (let i = 0; i < n / 2; i++) regs.push(r.readUInt16BE(2 + i * 2));
    return regs;
  }
  async readHoldingRegisters(addr, qty) {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x03, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(qty, 3);
    const r = await this._request(pdu); this._check(r, 0x03);
    const n = r.readUInt8(1); const regs = [];
    for (let i = 0; i < n / 2; i++) regs.push(r.readUInt16BE(2 + i * 2));
    return regs;
  }
  async writeSingleRegister(addr, value) {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x06, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(value & 0xffff, 3);
    const r = await this._request(pdu); this._check(r, 0x06);
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- Test ------------------------------------------------------------------
async function main() {
  console.log(`DQ->DI loopback test`);
  console.log(`  12DO (out): ${CFG.doIp}:${CFG.port} HR${CFG.doReg} (DQ1)`);
  console.log(`  12DI (in) : ${CFG.diIp}:${CFG.port} IR${CFG.diReg} (DI1)`);

  const doCli = new ModbusTcpClient(CFG.doIp, CFG.port, CFG.unit, CFG.timeoutMs);
  const diCli = new ModbusTcpClient(CFG.diIp, CFG.port, CFG.unit, CFG.timeoutMs);
  await doCli.connect();
  await diCli.connect();

  // Sanity: identify both modules.
  const doId = (await doCli.readInputRegisters(MB_IR_MODULE_ID, 1))[0];
  const diId = (await diCli.readInputRegisters(MB_IR_MODULE_ID, 1))[0];
  console.log(`  module IDs: DO=0x${doId.toString(16)} (exp 12d0), DI=0x${diId.toString(16)} (exp 12d1)`);
  if (doId !== 0x12d0) console.warn(`  WARN: DO module id is not 0x12d0`);
  if (diId !== 0x12d1) console.warn(`  WARN: DI module id is not 0x12d1`);

  // Set DI filter as low as possible to minimise added latency.
  if (CFG.filterMs >= 0) {
    try {
      await diCli.writeSingleRegister(MB_HR_DI_FILTER_MS, CFG.filterMs);
      const f = (await diCli.readHoldingRegisters(MB_HR_DI_FILTER_MS, 1))[0];
      console.log(`  DI filter set to ${f} ms`);
    } catch (e) { console.warn(`  could not set DI filter: ${e.message}`); }
  }

  const readDi = async () => (await diCli.readInputRegisters(CFG.diReg, 1))[0] & 1;

  // Establish a known baseline: DQ=0, wait until DI=0.
  await doCli.writeSingleRegister(CFG.doReg, 0);
  const tBase = performance.now();
  while ((await readDi()) !== 0) {
    if (performance.now() - tBase > CFG.timeoutMs) throw new Error('DI never settled to 0 at baseline (check wiring DQ0->DI0)');
  }

  const samples = []; // { edge, target, ms }
  let target = 1;
  let ok = 0, fail = 0;
  const byTime = CFG.seconds > 0;
  const edgePeriodMs = CFG.hz > 0 ? 500 / CFG.hz : 0; // square-wave: edge every T/2 = 500/hz ms
  const tStart = performance.now();
  let edge = 0;
  let stopping = false;
  let printed = false;

  const avg = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN);
  const printResults = (reason) => {
    if (printed) return; printed = true;
    const tEnd = performance.now();
    doCli.close(); diCli.close();
    const totalS = (tEnd - tStart) / 1000;
    const times = samples.map((s) => s.ms).sort((a, b) => a - b);
    const sum = times.reduce((a, b) => a + b, 0);
    const pct = (p) => (times.length ? times[Math.min(times.length - 1, Math.floor(p * times.length))] : NaN);
    const rise = samples.filter((s) => s.target === 1).map((s) => s.ms);
    const fall = samples.filter((s) => s.target === 0).map((s) => s.ms);
    console.log(`\n==================== RESULTS (${reason}) ====================`);
    console.log(`edges (signals) sent : ${ok + fail}`);
    console.log(`  passed             : ${ok}`);
    console.log(`  failed (timeout)   : ${fail}`);
    console.log(`test duration        : ${totalS.toFixed(2)} s (${(totalS / 60).toFixed(2)} min)`);
    if (CFG.hz > 0) console.log(`target rate          : ${CFG.hz} Hz square wave (edge every ${edgePeriodMs.toFixed(0)} ms)`);
    console.log(`achieved throughput  : ${((ok + fail) / totalS).toFixed(2)} edges/s`);
    if (times.length) {
      console.log(`propagation time (ms): min ${times[0].toFixed(1)} | avg ${(sum / times.length).toFixed(1)} | ` +
        `median ${pct(0.5).toFixed(1)} | p95 ${pct(0.95).toFixed(1)} | p99 ${pct(0.99).toFixed(1)} | max ${times[times.length - 1].toFixed(1)}`);
      console.log(`  rising  (0->1)     : avg ${avg(rise).toFixed(1)} ms  (n=${rise.length})`);
      console.log(`  falling (1->0)     : avg ${avg(fall).toFixed(1)} ms  (n=${fall.length})`);
      console.log(`note: includes Modbus write RTT, DI filter (${CFG.filterMs} ms), and one read RTT (poll resolution).`);
    }
    console.log('===========================================================');
    process.exit(fail > 0 ? 2 : 0);
  };

  const stop = (sig) => { if (!stopping) { stopping = true; console.log(`\n[${sig}] stopping after current edge...`); } };
  process.on('SIGINT', () => stop('SIGINT'));
  process.on('SIGTERM', () => stop('SIGTERM'));

  let lastProgress = tStart;
  const shouldContinue = () =>
    !stopping && (byTime ? (performance.now() - tStart) < CFG.seconds * 1000 : edge < CFG.edges);

  while (shouldContinue()) {
    const edgeStart = performance.now();
    edge++;
    const t0 = performance.now();
    await doCli.writeSingleRegister(CFG.doReg, target);
    let matched = false;
    while (performance.now() - t0 <= CFG.timeoutMs) {
      if ((await readDi()) === target) { matched = true; break; }
    }
    const dt = performance.now() - t0;
    if (matched) { ok++; samples.push({ edge, target, ms: dt }); }
    else { fail++; console.warn(`  edge ${edge}: target=${target} TIMEOUT after ${dt.toFixed(1)} ms`); }
    target ^= 1;

    // periodic progress line (every ~15 s) so a long run shows liveness.
    const now = performance.now();
    if (now - lastProgress >= 15000) {
      lastProgress = now;
      const recent = samples.slice(-400).map((s) => s.ms);
      console.log(`  [${((now - tStart) / 60000).toFixed(1)} min] edges=${edge} ok=${ok} fail=${fail} ` +
        `recentAvg=${avg(recent).toFixed(1)}ms`);
    }

    // pace to the requested frequency (measurement is << period).
    if (edgePeriodMs > 0) {
      const wait = edgePeriodMs - (performance.now() - edgeStart);
      if (wait > 0) await sleep(wait);
    } else if (CFG.settleMs > 0) {
      await sleep(CFG.settleMs);
    }
  }

  printResults(stopping ? 'stopped' : 'completed');
}

main().catch((e) => { console.error('ERROR:', e.message); process.exit(1); });
