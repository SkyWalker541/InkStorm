/**
 * InkStorm Installer — shared flashing module.
 *
 * Flashes the InkStorm dual-boot firmware (CrossInk on app0, InkStorm weather
 * on app1) onto an Xteink X3 / X4 e-ink reader using esptool-js over the Web
 * Serial API. Ported from the CrossPoint flasher (Apache-2.0, crosspoint-tools)
 * and adapted for a fixed two-partition install with a deterministic boot to
 * app0.
 */

import { ESPLoader, Transport } from './esptool.bundle.js';

// --- CRC32 ---

const CRC32_TABLE = new Uint32Array(256);
(function buildTable() {
  for (let i = 0; i < 256; i++) {
    let crc = i;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 1) ? (0xEDB88320 ^ (crc >>> 1)) : (crc >>> 1);
    }
    CRC32_TABLE[i] = crc >>> 0;
  }
})();

function crc32(data, previous = 0) {
  let crc = previous === 0 ? 0 : (previous ^ 0xFFFFFFFF) >>> 0;
  for (let i = 0; i < data.length; i++) {
    crc = CRC32_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >>> 8);
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

// --- Byte Utilities ---

function u32ToLeBytes(val) {
  return new Uint8Array([val & 0xFF, (val >>> 8) & 0xFF, (val >>> 16) & 0xFF, (val >>> 24) & 0xFF]);
}

function leBytesToU32(bytes) {
  return ((bytes[0] || 0) + (((bytes[1] || 0) << 8) >>> 0) +
    (((bytes[2] || 0) << 16) >>> 0) + (((bytes[3] || 0) << 24) >>> 0)) >>> 0;
}

function isEqualBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}

function generateCrc32Le(sequence) {
  // ESP-IDF stores crc32_le(UINT32_MAX, ota_seq, 4) in otadata entries.
  return u32ToLeBytes(crc32(u32ToLeBytes(sequence), 0xFFFFFFFF));
}

// --- MD5 ---
// The partition table's checksum row stores an MD5 digest; WebCrypto doesn't
// offer MD5, so this is a minimal RFC 1321 implementation.

const MD5_K = new Uint32Array(64);
for (let i = 0; i < 64; i++) MD5_K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32);
const MD5_S = [
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];

export function md5(input) {
  const bitLen = input.length * 8;
  const paddedLen = (Math.floor((input.length + 8) / 64) + 1) * 64;
  const msg = new Uint8Array(paddedLen);
  msg.set(input);
  msg[input.length] = 0x80;
  msg.set(u32ToLeBytes(bitLen >>> 0), paddedLen - 8);
  msg.set(u32ToLeBytes(Math.floor(bitLen / 2 ** 32)), paddedLen - 4);

  let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  const M = new Uint32Array(16);
  for (let chunk = 0; chunk < paddedLen; chunk += 64) {
    for (let i = 0; i < 16; i++) M[i] = leBytesToU32(msg.subarray(chunk + i * 4, chunk + i * 4 + 4));
    let A = a0, B = b0, C = c0, D = d0;
    for (let i = 0; i < 64; i++) {
      let F, g;
      if (i < 16) { F = (B & C) | (~B & D); g = i; }
      else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
      else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
      else { F = C ^ (B | ~D); g = (7 * i) % 16; }
      F = (F + A + MD5_K[i] + M[g]) >>> 0;
      A = D; D = C; C = B;
      B = (B + ((F << MD5_S[i]) | (F >>> (32 - MD5_S[i])))) >>> 0;
    }
    a0 = (a0 + A) >>> 0; b0 = (b0 + B) >>> 0; c0 = (c0 + C) >>> 0; d0 = (d0 + D) >>> 0;
  }
  const out = new Uint8Array(16);
  out.set(u32ToLeBytes(a0), 0);
  out.set(u32ToLeBytes(b0), 4);
  out.set(u32ToLeBytes(c0), 8);
  out.set(u32ToLeBytes(d0), 12);
  return out;
}

// --- Firmware Image Validation ---

const ESP_IMAGE_MAGIC = 0xE9;
const IMG_HEADER_SIZE = 24;
const IMG_SEG_HEADER_SIZE = 8;
const IMG_SHA_TRAILER = 32;
const IMG_CHECKSUM_SEED = 0xEF;
const IMG_HASH_APPENDED_OFFSET = 23;

// Walk the ESP image structure and reject HTML error pages, truncated
// downloads, and wrong-shape binaries before anything touches flash.
export async function validateFirmwareImage(data) {
  const totalSize = data.length;
  if (totalSize < IMG_HEADER_SIZE) {
    throw new Error('Firmware too small: header is truncated.');
  }
  if (data[0] !== ESP_IMAGE_MAGIC) {
    throw new Error('Invalid firmware: ESP image magic byte (0xE9) missing. Are you sure this is a firmware .bin?');
  }
  const segCount = data[1];
  const hashAppended = (data[IMG_HASH_APPENDED_OFFSET] & 0x01) !== 0;

  let xorAccum = IMG_CHECKSUM_SEED;
  let pos = IMG_HEADER_SIZE;
  for (let i = 0; i < segCount; i++) {
    if (totalSize - pos < IMG_SEG_HEADER_SIZE) {
      throw new Error('Invalid firmware: segment header runs past end of file.');
    }
    const dataLen = leBytesToU32(data.subarray(pos + 4, pos + 8));
    pos += IMG_SEG_HEADER_SIZE;
    if (dataLen > totalSize - pos) {
      throw new Error('Invalid firmware: segment data runs past end of file.');
    }
    const end = pos + dataLen;
    for (let j = pos; j < end; j++) xorAccum ^= data[j];
    pos = end;
  }

  const padEnd = (pos + 16) & ~15;
  const expectedTotal = padEnd + (hashAppended ? IMG_SHA_TRAILER : 0);
  if (expectedTotal > totalSize) {
    throw new Error(`Invalid firmware: declared size ${expectedTotal} exceeds file size ${totalSize}. File is truncated or corrupt.`);
  }
  // Files may legitimately be larger than the bare image: the release assets
  // are full partition dumps (image + trailing partition content + 0xFF
  // padding). Only an image that is too SMALL for its own structure is bad.
  const storedChecksum = data[padEnd - 1];
  if ((xorAccum & 0xFF) !== storedChecksum) {
    throw new Error(`Invalid firmware: segment checksum mismatch (computed 0x${(xorAccum & 0xFF).toString(16)}, stored 0x${storedChecksum.toString(16)}).`);
  }
  if (hashAppended) {
    const body = data.subarray(0, padEnd);
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', body));
    const stored = data.subarray(padEnd, padEnd + IMG_SHA_TRAILER);
    if (!isEqualBytes(digest, stored)) {
      throw new Error('Invalid firmware: SHA-256 trailer mismatch. File is corrupt or truncated.');
    }
  }
}

// --- Partition Table ---

// InkStorm's dual-app layout (matches the repo's partitions.csv): two
// 6.5 MB OTA slots, CrossInk on ota_0 and InkStorm on ota_1.
export const INKSTORM_PARTITION_TABLE = [
  { type: 'data-nvs', offset: 0x9000, size: 0x5000 },
  { type: 'data-ota', offset: 0xe000, size: 0x2000 },
  { type: 'app-ota_0', offset: 0x10000, size: 0x640000 },
  { type: 'app-ota_1', offset: 0x650000, size: 0x640000 },
  { type: 'data-spiffs', offset: 0xc90000, size: 0x360000 },
  { type: 'data-coredump', offset: 0xff0000, size: 0x10000 },
];

const PARTITION_TYPES = {
  0x00: { 0x10: 'app-ota_0', 0x11: 'app-ota_1' },
  0x01: { 0x00: 'data-ota', 0x01: 'data-phy', 0x02: 'data-nvs', 0x03: 'data-coredump', 0x82: 'data-spiffs' },
};

const PARTITION_ENTRY_INFO = {
  'app-ota_0': { type: 0x00, subtype: 0x10, label: 'app0' },
  'app-ota_1': { type: 0x00, subtype: 0x11, label: 'app1' },
  'data-ota': { type: 0x01, subtype: 0x00, label: 'otadata' },
  'data-nvs': { type: 0x01, subtype: 0x02, label: 'nvs' },
  'data-spiffs': { type: 0x01, subtype: 0x82, label: 'spiffs' },
  'data-coredump': { type: 0x01, subtype: 0x03, label: 'coredump' },
};

export function parsePartitionTable(data) {
  const partitions = [];
  for (let offset = 0; offset < data.length; offset += 32) {
    const chunk = data.slice(offset, offset + 32);
    if (chunk.length !== 32) break;
    let allFF = true;
    for (let i = 0; i < 32; i++) { if (chunk[i] !== 0xFF) { allFF = false; break; } }
    if (allFF) break;
    if (chunk[0] === 0xEB && chunk[1] === 0xEB) continue;

    const type = PARTITION_TYPES[chunk[2]]?.[chunk[3]] || 'unknown';
    const off = leBytesToU32(chunk.slice(4, 8));
    const size = leBytesToU32(chunk.slice(8, 12));
    partitions.push({ type, offset: off, size });
  }
  return partitions;
}

export function buildPartitionTableBinary(table) {
  const out = new Uint8Array(0x1000).fill(0xFF);
  table.forEach((p, i) => {
    const info = PARTITION_ENTRY_INFO[p.type];
    if (!info) throw new Error(`No entry info for partition type ${p.type}.`);
    const entry = out.subarray(i * 32, (i + 1) * 32);
    entry.fill(0);
    entry[0] = 0xAA;
    entry[1] = 0x50;
    entry[2] = info.type;
    entry[3] = info.subtype;
    entry.set(u32ToLeBytes(p.offset), 4);
    entry.set(u32ToLeBytes(p.size), 8);
    for (let j = 0; j < info.label.length; j++) entry[12 + j] = info.label.charCodeAt(j);
  });
  const md5Row = out.subarray(table.length * 32, (table.length + 1) * 32);
  md5Row[0] = 0xEB;
  md5Row[1] = 0xEB;
  md5Row.set(md5(out.subarray(0, table.length * 32)), 16);
  return out;
}

export function matchesPartitionTable(actual, expected) {
  return actual.length === expected.length &&
    expected.every((exp, i) =>
      actual[i].type === exp.type &&
      actual[i].offset === exp.offset &&
      actual[i].size === exp.size
    );
}

// --- OTA Partition ---

// IDF otadata format: exactly two 4 KB flash sectors, each holding one
// esp_ota_select_entry_t at byte 0.
const OTA_SECTOR_BYTES = 0x1000;
const OTADATA_BYTES = 2 * OTA_SECTOR_BYTES;

export const OTA_STATE = { NEW: 0, PENDING_VERIFY: 1, VALID: 2, INVALID: 3, ABORTED: 4, UNDEFINED: 0xFFFFFFFF };

function parseOtaPartitionSlot(data, offset) {
  const sequence = leBytesToU32(data.slice(offset, offset + 4));
  const state = leBytesToU32(data.slice(offset + 0x18, offset + 0x1C));
  const crcBytes = data.slice(offset + 0x1C, offset + 0x20);
  const expectedCrc = generateCrc32Le(sequence);
  return { sequence, state, crcValid: isEqualBytes(crcBytes, expectedCrc) };
}

export function parseOtadata(data) {
  const slot0 = parseOtaPartitionSlot(data, 0);
  const slot1 = parseOtaPartitionSlot(data, 0x1000);
  return { slot0, slot1 };
}

// Fresh otadata that boots app0 (CrossInk) deterministically: sector 0 holds a
// VALID entry with sequence 1 (active app = (1-1) % 2 = 0), sector 1 is left
// erased (0xFF). VALID rather than NEW because a dual-boot install writes both
// slots — there is no previous working image to roll back to, so a pending
// verify would only risk a needless second boot. On the X3/X4 bootloaders the
// state field is ignored entirely.
export function buildFreshOtadataBootingApp0() {
  const otadata = new Uint8Array(OTADATA_BYTES).fill(0xFF);
  const entry = otadata.subarray(0, 0x20);
  entry.fill(0);
  entry.set(u32ToLeBytes(1), 0);
  entry.set(u32ToLeBytes(OTA_STATE.VALID), 0x18);
  entry.set(generateCrc32Le(1), 0x1C);
  return otadata;
}

// --- Firmware Fetch ---

// Firmware images are served same-origin from the site's /flash/ directory
// (site/public/flash/), so no CORS headers are needed.
export async function fetchFirmware(name) {
  // import.meta.env.BASE_URL is inlined without a trailing slash (e.g.
  // "/InkStorm"), so normalize it before appending the flash/ path.
  const base = (import.meta.env.BASE_URL || '/').replace(/\/?$/, '/');
  const url = `${base}flash/${name}`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Failed to download ${name} (HTTP ${res.status}).`);
  return new Uint8Array(await res.arrayBuffer());
}

// --- Main Flasher ---

export class InkStormFlasher {
  static SUPPORTED_BROWSERS = 'Chrome or Microsoft Edge on a desktop computer';

  // Must be called synchronously inside a user gesture (click handler) before
  // any awaits. Filters to the Espressif USB-Serial-JTAG VID/PID the Xteink
  // devices enumerate with.
  static async requestPort() {
    if (!('serial' in navigator)) {
      throw new Error(
        `This browser does not support the Web Serial API. ${InkStormFlasher.SUPPORTED_BROWSERS} is required.`
      );
    }
    return await navigator.serial.requestPort({
      filters: [{ usbVendorId: 12346, usbProductId: 4097 }],
    });
  }

  constructor(port) {
    this.port = port;
    this.espLoader = null;
    // Fixed install layout: otadata at 0xE000, app0 at 0x10000, app1 at 0x650000.
    this.otadataOffset = 0xe000;
    this.appSlots = [
      { offset: 0x10000, size: 0x640000 },
      { offset: 0x650000, size: 0x640000 },
    ];
  }

  async connect() {
    const transport = new Transport(this.port, false);
    this.espLoader = new ESPLoader({
      transport, baudrate: 115200, romBaudrate: 115200, enableTracing: false,
    });
    await this.espLoader.main();

    const chipName = this.espLoader.chip?.CHIP_NAME;
    if (chipName && chipName !== 'ESP32-C3') {
      await this.disconnect(true);
      throw new Error(
        `The connected device reports as ${chipName}, but InkStorm targets the ESP32-C3 ` +
        '(Xteink X3 / X4). Nothing was written.'
      );
    }
    return chipName || 'ESP32';
  }

  async disconnect(skipReset = false) {
    if (!this.espLoader) return;
    try { await this.espLoader.transport.setDTR(false); } catch {}
    if (skipReset) {
      await this.espLoader.after('no_reset_stub');
    } else {
      await this.espLoader.transport.setRTS(true);
      await new Promise((resolve) => setTimeout(resolve, 100));
      await this.espLoader.after('hard_reset');
    }
    try {
      await this.espLoader.transport.setDTR(false);
      await this.espLoader.transport.setRTS(false);
      await new Promise((resolve) => setTimeout(resolve, 100));
    } catch {}
    await this.espLoader.transport.disconnect();
    this.espLoader = null;
  }

  async writeBlock(data, address, label, onProgress) {
    if (data.length > this.appSlots[0].size && address >= 0x10000) {
      throw new Error(`${label} is too large to fit in a 6.5 MB app partition.`);
    }
    await this.espLoader.writeFlash({
      fileArray: [{ data: this.espLoader.ui8ToBstr(data), address }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep',
      eraseAll: false, compress: true,
      reportProgress: (_, written, total) => { if (onProgress) onProgress(label, written, total); },
    });
  }

  async install({ app0, app1, onStepChange, onProgress, onLog } = {}) {
    const steps = [
      'Connect to device',
      'Check partition table',
      'Install CrossInk (app0)',
      'Install InkStorm (app1)',
      'Set default boot to CrossInk',
      'Reset device',
    ];
    const step = (idx, status) => { if (onStepChange) onStepChange(idx, steps[idx], status); };

    // Image-shape gates before connecting: a bad .bin fails without touching flash.
    await validateFirmwareImage(app0);
    await validateFirmwareImage(app1);

    step(0, 'running');
    const chipName = await this.connect();
    onLog?.(`Connected: ${chipName}`);
    step(0, 'done');

    step(1, 'running');
    const ptRaw = await this.espLoader.readFlash(0x8000, 0x1000);
    const partitions = parsePartitionTable(ptRaw);
    if (matchesPartitionTable(partitions, INKSTORM_PARTITION_TABLE)) {
      onLog?.('Partition table already matches the InkStorm dual-app layout.');
    } else {
      onLog?.('The device partition table differs from the InkStorm dual-app layout — rewriting it.');
      await this.writeBlock(buildPartitionTableBinary(INKSTORM_PARTITION_TABLE), 0x8000, 'Write partition table', onProgress);
      const verify = parsePartitionTable(await this.espLoader.readFlash(0x8000, 0x1000));
      if (!matchesPartitionTable(verify, INKSTORM_PARTITION_TABLE)) {
        throw new Error('Partition table did not verify after write.');
      }
      onLog?.('Partition table written and verified.');
    }
    step(1, 'done');

    step(2, 'running');
    if (app0.length > this.appSlots[0].size) {
      throw new Error(`CrossInk firmware is too large for app0: ${app0.length} bytes (max ${this.appSlots[0].size}).`);
    }
    await this.writeBlock(app0, this.appSlots[0].offset, 'Install CrossInk (app0)', onProgress);
    step(2, 'done');

    step(3, 'running');
    if (app1.length > this.appSlots[1].size) {
      throw new Error(`InkStorm firmware is too large for app1: ${app1.length} bytes (max ${this.appSlots[1].size}).`);
    }
    await this.writeBlock(app1, this.appSlots[1].offset, 'Install InkStorm (app1)', onProgress);
    step(3, 'done');

    step(4, 'running');
    const otadata = buildFreshOtadataBootingApp0();
    await this.writeBlock(otadata, this.otadataOffset, 'Set default boot', onProgress);
    const ota = parseOtadata(await this.espLoader.readFlash(this.otadataOffset, OTADATA_BYTES));
    const bootedApp0 = ota.slot0.sequence === 1 && ota.slot0.crcValid &&
      ota.slot0.state === OTA_STATE.VALID && ota.slot1.sequence === 0xFFFFFFFF;
    if (!bootedApp0) {
      throw new Error('Boot selector did not verify after write. Reconnect and try again.');
    }
    onLog?.('Boot selector verified: the device will boot into CrossInk.');
    step(4, 'done');

    step(5, 'running');
    await this.disconnect(false);
    step(5, 'done');

    return { success: true };
  }
}
