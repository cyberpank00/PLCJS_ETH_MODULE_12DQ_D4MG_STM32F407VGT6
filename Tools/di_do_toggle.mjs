#!/usr/bin/env node
/**
 * di_do_toggle.mjs — toggle flip-flop: every press of the button on 12DI DI0
 * (rising edge) flips 12DQ DO0. Divide-by-two / push-on-push-off switch.
 *
 * Reads DI0 with FC02 (discrete input 0, already debounced by the module's
 * input filter, HR100), writes DO0 with FC05 (coil 0). Two independent TCP
 * connections, ~20 ms poll.
 *
 *   node di_do_toggle.mjs [--di-ip 192.168.1.10] [--do-ip 192.168.1.11]
 *                         [--di 0] [--do 0] [--poll-ms 20]
 */
import net from 'node:net';

const args = Object.fromEntries(process.argv.slice(2).map((a, i, v) => a.startsWith('--') ? [a.slice(2), v[i + 1]] : []).filter((p) => p.length));
const DI_IP = args['di-ip'] || '192.168.1.10';
const DO_IP = args['do-ip'] || '192.168.1.11';
const DI = parseInt(args.di ?? '0', 10);
const DO = parseInt(args.do ?? '0', 10);
const POLL_MS = parseInt(args['poll-ms'] ?? '20', 10);

class Modbus {
  constructor(ip) { this.ip = ip; this.tx = 0; this.sock = null; }
  connect() {
    return new Promise((res, rej) => {
      const s = net.connect({ host: this.ip, port: 502 }, () => { this.sock = s; res(); });
      s.setNoDelay(true);
      s.on('error', (e) => { if (!this.sock) rej(e); });
      s.on('close', () => { this.sock = null; });
    });
  }
  txn(pdu, timeout = 1000) {
    return new Promise((res, rej) => {
      if (!this.sock) return rej(new Error('not connected'));
      const s = this.sock, tx = (this.tx = (this.tx + 1) & 0xffff);
      const h = Buffer.alloc(7); h.writeUInt16BE(tx, 0); h.writeUInt16BE(pdu.length + 1, 4); h[6] = 1;
      let buf = Buffer.alloc(0);
      const done = (fn) => { clearTimeout(to); s.off('data', onData); s.off('close', onClose); fn(); };
      const to = setTimeout(() => done(() => rej(new Error('timeout'))), timeout);
      const onData = (d) => { buf = Buffer.concat([buf, d]); if (buf.length >= 6 && buf.length >= 6 + buf.readUInt16BE(4)) done(() => (buf[7] & 0x80) ? rej(new Error('exception ' + buf[8])) : res(buf.subarray(8))); };
      const onClose = () => done(() => rej(new Error('closed')));
      s.on('data', onData); s.once('close', onClose);
      s.write(Buffer.concat([h, pdu]));
    });
  }
  async readDiscrete(addr) { const p = Buffer.from([0x02, addr >> 8, addr & 0xff, 0, 1]); const r = await this.txn(p); return (r[1] & 1) !== 0; }
  async writeCoil(addr, on) { const p = Buffer.from([0x05, addr >> 8, addr & 0xff, on ? 0xff : 0x00, 0x00]); await this.txn(p); }
  async readCoil(addr) { const p = Buffer.from([0x01, addr >> 8, addr & 0xff, 0, 1]); const r = await this.txn(p); return (r[1] & 1) !== 0; }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function withReconnect(mb, fn) {
  for (;;) {
    try { if (!mb.sock) { await mb.connect(); console.log(`[${mb.ip}] connected`); } return await fn(); }
    catch (e) { console.log(`[${mb.ip}] ${e.message}, reconnecting...`); mb.sock?.destroy(); mb.sock = null; await sleep(500); }
  }
}

(async () => {
  const di = new Modbus(DI_IP), dq = new Modbus(DO_IP);
  let out = await withReconnect(dq, () => dq.readCoil(DO));
  let prev = await withReconnect(di, () => di.readDiscrete(DI));
  console.log(`DI${DI}@${DI_IP} -> DO${DO}@${DO_IP}  poll ${POLL_MS} ms  (DO now ${out ? 'ON' : 'OFF'}, DI now ${prev ? 1 : 0}). Ctrl+C to stop.`);
  let presses = 0;
  for (;;) {
    const cur = await withReconnect(di, () => di.readDiscrete(DI));
    if (cur && !prev) {                         // rising edge = button press
      out = !out; presses++;
      await withReconnect(dq, () => dq.writeCoil(DO, out));
      console.log(`${new Date().toISOString().slice(11, 23)}  press #${presses}  ->  DO${DO} ${out ? 'ON ' : 'OFF'}`);
    }
    prev = cur;
    await sleep(POLL_MS);
  }
})();
