#!/usr/bin/env node
// @ts-check
/*
 * dq_random_stress_test.mjs - Random-pattern robustness/EMC stress test for the
 * PLCJS 12DO (12DQ) module driving inductive loads (e.g. 12x 24V relays).
 *
 * Each cycle:
 *   1. pick a random 12-bit value (0..4095, i.e. 2^12 combinations)
 *   2. write it to the group output holding register (HR50): bit i -> DQ(i+1)
 *   3. read back input registers 120..125 (fw, uptime, output mask, module id)
 *   4. verify the echoed output mask (IR124) matches what we wrote
 *   5. watch uptime for unexpected drops => the module reset (EMC event!)
 *
 * Detects: stuck/flipped channels (back-EMF latch-up), Modbus timeouts /
 * dropped links, and MCU resets/brown-outs from EMC. Per-channel mismatch
 * counters pinpoint a weak output.
 *
 * No external dependencies (built-in node:net). Requires Node 18+.
 *
 * Usage:
 *   node dq_random_stress_test.mjs [--ip 192.168.142.52] [--hz 5]
 *        [--hours 24] [--seconds 0] [--seed N] [--timeout 1000]
 *        [--progress 30] [--port 502] [--unit 1]
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
  ip: argVal('ip', '192.168.142.52'),
  port: Number(argVal('port', '502')),
  unit: Number(argVal('unit', '1')),
  hz: Number(argVal('hz', '5')),
  hours: Number(argVal('hours', '24')),
  seconds: Number(argVal('seconds', '0')),
  seed: argVal('seed', ''),               // optional: reproducible sequence
  timeoutMs: Number(argVal('timeout', '1000')),
  progressS: Number(argVal('progress', '30')),
  groupReg: Number(argVal('group-reg', '50')), // HR: group output mask
  infoBase: Number(argVal('info-base', '120')), // IR base: fwMaj,fwMin,upLo,upHi,mask,id
};
const DQ_COUNT = 12;
const MASK_12 = (1 << DQ_COUNT) - 1; // 0xFFF

// ---- PRNG (mulberry32) for optional reproducible sequences ----------------
function makeRng(seedStr) {
  if (!seedStr) return () => Math.floor(Math.random() * (MASK_12 + 1));
  let a = (Number(seedStr) >>> 0) || 0x9e3779b9;
  return () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) & MASK_12;
  };
}

// ---- Minimal Modbus TCP client --------------------------------------------
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
  async writeSingleRegister(addr, value) {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x06, 0); pdu.writeUInt16BE(addr, 1); pdu.writeUInt16BE(value & 0xffff, 3);
    const r = await this._request(pdu); this._check(r, 0x06);
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ts = () => new Date().toISOString().replace('T', ' ').replace('Z', '');

// ---- Main -----------------------------------------------------------------
async function main() {
  const durationMs = (CFG.hours * 3600 + CFG.seconds) * 1000;
  const periodMs = CFG.hz > 0 ? 1000 / CFG.hz : 0;
  const rng = makeRng(CFG.seed);

  console.log(`12DO random-pattern EMC/back-EMF stress test`);
  console.log(`  target   : ${CFG.ip}:${CFG.port} unit ${CFG.unit}`);
  console.log(`  pattern  : random 12-bit -> HR${CFG.groupReg}; verify echo IR${CFG.infoBase + 4}`);
  console.log(`  rate     : ${CFG.hz} Hz (period ${periodMs.toFixed(0)} ms)`);
  console.log(`  duration : ${(durationMs / 3600000).toFixed(2)} h${CFG.seed ? `  seed=${CFG.seed}` : ''}`);

  let cli = new ModbusTcpClient(CFG.ip, CFG.port, CFG.unit, CFG.timeoutMs);
  await cli.connect();

  const info0 = await cli.readInputRegisters(CFG.infoBase, 6);
  const modId = info0[5];
  console.log(`  module   : id=0x${modId.toString(16)} fw=${info0[0]}.${info0[1]} uptime=${(info0[2] | (info0[3] << 16)) >>> 0}s`);
  if (modId !== 0x12d0) console.warn(`  WARN: module id is not 0x12d0 (got 0x${modId.toString(16)})`);

  // ---- counters ----
  let cycles = 0, ok = 0, mismatches = 0, commErrors = 0, reconnects = 0, resets = 0;
  const chMismatch = new Array(DQ_COUNT).fill(0); // per-channel differing-bit counts
  const rtt = { min: Infinity, max: 0, sum: 0, n: 0 };
  let lastUptime = (info0[2] | (info0[3] << 16)) >>> 0;
  let logged = 0; // rate-limit anomaly logging
  const LOG_CAP = 200;
  let printed = false, stopping = false;

  const logEvent = (msg) => { if (logged < LOG_CAP) { logged++; console.warn(`  [${ts()}] ${msg}`); } };

  const reconnect = async () => {
    reconnects++;
    try { cli.close(); } catch { /* ignore */ }
    for (let attempt = 1; !stopping; attempt++) {
      try {
        cli = new ModbusTcpClient(CFG.ip, CFG.port, CFG.unit, CFG.timeoutMs);
        await cli.connect();
        logEvent(`reconnected (attempt ${attempt})`);
        return true;
      } catch (e) {
        if (attempt === 1 || attempt % 20 === 0) logEvent(`reconnect failing (attempt ${attempt}): ${e.message}`);
        await sleep(Math.min(2000, 200 * attempt));
      }
    }
    return false;
  };

  const printResults = async (reason) => {
    if (printed) return; printed = true;
    const tEnd = performance.now();
    const totalS = (tEnd - tStart) / 1000;
    // Try to leave the outputs de-energized.
    try { await cli.writeSingleRegister(CFG.groupReg, 0); } catch { /* ignore */ }
    try { cli.close(); } catch { /* ignore */ }
    console.log(`\n==================== STRESS RESULTS (${reason}) ====================`);
    console.log(`duration            : ${(totalS / 3600).toFixed(3)} h (${totalS.toFixed(0)} s)`);
    console.log(`cycles (writes)     : ${cycles}`);
    console.log(`  verified OK       : ${ok}`);
    console.log(`  echo mismatches   : ${mismatches}`);
    console.log(`comm errors/timeouts: ${commErrors}`);
    console.log(`reconnects          : ${reconnects}`);
    console.log(`module resets seen  : ${resets}`);
    console.log(`achieved rate       : ${(cycles / totalS).toFixed(2)} Hz`);
    if (rtt.n) console.log(`txn time (ms)       : min ${rtt.min.toFixed(1)} | avg ${(rtt.sum / rtt.n).toFixed(1)} | max ${rtt.max.toFixed(1)}`);
    console.log(`per-channel mismatches (DQ1..DQ12): [${chMismatch.join(', ')}]`);
    const verdict = (mismatches === 0 && commErrors === 0 && resets === 0)
      ? 'PASS - no faults detected'
      : 'ATTENTION - faults detected (see counters/log above)';
    console.log(`verdict             : ${verdict}`);
    console.log('===================================================================');
    process.exit((mismatches || commErrors || resets) ? 2 : 0);
  };

  const stop = (sig) => { if (!stopping) { stopping = true; console.log(`\n[${sig}] stopping...`); } };
  process.on('SIGINT', () => stop('SIGINT'));
  process.on('SIGTERM', () => stop('SIGTERM'));

  const tStart = performance.now();
  let lastProgress = tStart;

  while (!stopping && (performance.now() - tStart) < durationMs) {
    const cycleStart = performance.now();
    const value = rng() & MASK_12;
    cycles++;
    try {
      const t0 = performance.now();
      await cli.writeSingleRegister(CFG.groupReg, value);
      const info = await cli.readInputRegisters(CFG.infoBase, 6);
      const dt = performance.now() - t0;
      rtt.n++; rtt.sum += dt; if (dt < rtt.min) rtt.min = dt; if (dt > rtt.max) rtt.max = dt;

      const echo = info[4] & MASK_12;
      if (echo === value) {
        ok++;
      } else {
        mismatches++;
        let diff = echo ^ value;
        for (let b = 0; b < DQ_COUNT; b++) if (diff & (1 << b)) chMismatch[b]++;
        logEvent(`ECHO MISMATCH: wrote 0x${value.toString(16).padStart(3, '0')} read 0x${echo.toString(16).padStart(3, '0')} (diff bits DQ: ${bitsToChannels(diff)})`);
      }

      const up = (info[2] | (info[3] << 16)) >>> 0;
      if (up + 2 < lastUptime) { // uptime went backwards => reset
        resets++;
        logEvent(`MODULE RESET detected: uptime ${lastUptime}s -> ${up}s`);
      }
      lastUptime = up;
    } catch (e) {
      commErrors++;
      logEvent(`comm error: ${e.message}`);
      await reconnect();
    }

    const now = performance.now();
    if (now - lastProgress >= CFG.progressS * 1000) {
      lastProgress = now;
      console.log(`  [${((now - tStart) / 3600000).toFixed(3)}h] cyc=${cycles} ok=${ok} mism=${mismatches} ` +
        `commErr=${commErrors} rec=${reconnects} resets=${resets} uptime=${lastUptime}s`);
    }

    if (periodMs > 0) {
      const wait = periodMs - (performance.now() - cycleStart);
      if (wait > 0) await sleep(wait);
    }
  }

  await printResults(stopping ? 'stopped' : 'completed');
}

function bitsToChannels(mask) {
  const ch = [];
  for (let b = 0; b < DQ_COUNT; b++) if (mask & (1 << b)) ch.push(`DQ${b + 1}`);
  return ch.join(',');
}

main().catch((e) => { console.error('FATAL:', e.message); process.exit(1); });
