/* Motor de corrección de tono: detección YIN + corrección + resíntesis PSOLA. */
const Tuner = (function () {
  "use strict";

  const SCALES = {
    chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    major: [0, 2, 4, 5, 7, 9, 11],
    minor: [0, 2, 3, 5, 7, 8, 10],
    harmonicMinor: [0, 2, 3, 5, 7, 8, 11],
    majorPent: [0, 2, 4, 7, 9],
    minorPent: [0, 3, 5, 7, 10],
    blues: [0, 3, 5, 6, 7, 10],
    dorian: [0, 2, 3, 5, 7, 9, 10],
    mixolydian: [0, 2, 4, 5, 7, 9, 10],
    phrygian: [0, 1, 3, 5, 7, 8, 10],
    lydian: [0, 2, 4, 6, 7, 9, 11],
    wholeTone: [0, 2, 4, 6, 8, 10],
  };

  function maskFor(key, scaleName) {
    const iv = SCALES[scaleName] || SCALES.chromatic;
    const mask = new Array(12).fill(false);
    for (const i of iv) mask[(key + i) % 12] = true;
    return mask;
  }

  function mulberry32(a) {
    return function () {
      a |= 0; a = (a + 0x6d2b79f5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }

  const tick = () => new Promise((r) => setTimeout(r, 0));

  function downsample(x, D) {
    if (D <= 1) return x;
    const n = Math.floor(x.length / D);
    const y = new Float32Array(n);
    const norm = 1 / (D * D);
    for (let i = 0; i < n; i++) {
      const c = i * D;
      let s = 0;
      for (let j = -D + 1; j < D; j++) {
        const idx = c + j;
        if (idx >= 0 && idx < x.length) s += (D - Math.abs(j)) * x[idx];
      }
      y[i] = s * norm;
    }
    return y;
  }

  /* ---------- Detección de tono (YIN) ---------- */
  async function analyze(x, sr, onProgress) {
    const D = Math.max(1, Math.round(sr / 11000));
    const xs = downsample(x, D);
    const sr2 = sr / D;
    const tauMax = Math.floor(sr2 / 70);
    const tauMin = Math.max(2, Math.floor(sr2 / 900));
    const W = Math.round(tauMax * 1.5);
    const N = W + tauMax;
    const hop = Math.max(1, Math.round(sr2 * 0.005));
    const nFrames = Math.max(0, Math.floor((xs.length - N) / hop) + 1);
    const f0 = new Float32Array(nFrames);
    const rms = new Float32Array(nFrames);
    let maxRms = 0;
    for (let i = 0; i < nFrames; i++) {
      const st = i * hop;
      let s = 0;
      for (let j = 0; j < N; j++) s += xs[st + j] * xs[st + j];
      rms[i] = Math.sqrt(s / N);
      if (rms[i] > maxRms) maxRms = rms[i];
    }
    const thr = Math.max(0.002, 0.05 * maxRms);
    const d = new Float32Array(tauMax + 2);
    const cm = new Float32Array(tauMax + 2);
    for (let i = 0; i < nFrames; i++) {
      if (rms[i] > thr) {
        const st = i * hop;
        for (let tau = 1; tau <= tauMax; tau++) {
          let s = 0;
          for (let j = 0; j < W; j++) {
            const dl = xs[st + j] - xs[st + j + tau];
            s += dl * dl;
          }
          d[tau] = s;
        }
        cm[0] = 1;
        let run = 0;
        for (let tau = 1; tau <= tauMax; tau++) {
          run += d[tau];
          cm[tau] = (d[tau] * tau) / (run || 1e-12);
        }
        let est = -1;
        for (let t = tauMin; t < tauMax; t++) {
          if (cm[t] < 0.15) {
            while (t + 1 < tauMax && cm[t + 1] < cm[t]) t++;
            est = t;
            break;
          }
        }
        if (est < 0) {
          let best = tauMin;
          for (let t = tauMin; t < tauMax; t++) if (cm[t] < cm[best]) best = t;
          if (cm[best] < 0.3) est = best;
        }
        if (est > 1 && est < tauMax) {
          const s0 = cm[est - 1], s1 = cm[est], s2 = cm[est + 1];
          const den = s0 + s2 - 2 * s1;
          const sh = den ? (0.5 * (s0 - s2)) / den : 0;
          f0[i] = sr2 / (est + Math.max(-1, Math.min(1, sh)));
        }
      }
      if (onProgress && i % 150 === 0) {
        onProgress(i / nFrames);
        await tick();
      }
    }
    cleanTrack(f0);
    return {
      f0,
      sr,
      hopSec: (hop * D) / sr,
      t0: ((N / 2) * D) / sr,
    };
  }

  function cleanTrack(f0) {
    const n = f0.length;
    // rellenar huecos de hasta 3 cuadros entre tramos con voz
    let i = 0;
    while (i < n) {
      if (f0[i] === 0) {
        let j = i;
        while (j < n && f0[j] === 0) j++;
        if (i > 0 && j < n && j - i <= 3) {
          const a = f0[i - 1], b = f0[j];
          if (Math.abs(Math.log2(a / b)) < 0.25) {
            for (let k = i; k < j; k++) f0[k] = a + ((b - a) * (k - i + 1)) / (j - i + 1);
          }
        }
        i = j;
      } else i++;
    }
    // quitar tramos de voz muy cortos
    i = 0;
    while (i < n) {
      if (f0[i] > 0) {
        let j = i;
        while (j < n && f0[j] > 0) j++;
        if (j - i < 4) for (let k = i; k < j; k++) f0[k] = 0;
        i = j;
      } else i++;
    }
    // filtro de mediana sobre cuadros con voz
    const src = Float32Array.from(f0);
    for (let k = 0; k < n; k++) {
      if (src[k] <= 0) continue;
      const w = [];
      for (let m = k - 2; m <= k + 2; m++) if (m >= 0 && m < n && src[m] > 0) w.push(src[m]);
      if (w.length >= 3) {
        w.sort((a, b) => a - b);
        f0[k] = w[(w.length - 1) >> 1];
      }
    }
  }

  const midiOf = (f, refA) => 69 + 12 * Math.log2(f / refA);

  function nearestAllowed(m, mask) {
    const r = Math.round(m);
    let best = NaN, bd = Infinity;
    for (let n = r - 3; n <= r + 3; n++) {
      if (mask[((n % 12) + 12) % 12]) {
        const dd = Math.abs(n - m);
        if (dd < bd) { bd = dd; best = n; }
      }
    }
    return best;
  }

  function lfo(shape, ph) {
    const f = ph - Math.floor(ph);
    if (shape === "triangle") return f < 0.5 ? 4 * f - 1 : 3 - 4 * f;
    if (shape === "square") return f < 0.5 ? 1 : -1;
    return Math.sin(2 * Math.PI * ph);
  }

  /* ---------- Plan de corrección (por cuadro de análisis) ---------- */
  function plan(track, p) {
    const n = track.f0.length, hop = track.hopSec;
    const inM = new Float32Array(n).fill(NaN);
    const outM = new Float32Array(n).fill(NaN);
    const tgtM = new Float32Array(n).fill(NaN);
    const cenM = new Float32Array(n).fill(NaN);
    const shiftC = new Float32Array(n);
    const R = Math.max(1, Math.round(0.08 / hop));
    let i = 0;
    while (i < n) {
      if (track.f0[i] <= 0) { i++; continue; }
      let j = i;
      while (j < n && track.f0[j] > 0) j++;
      const len = j - i;
      const c = new Float32Array(len);
      for (let k = 0; k < len; k++) c[k] = 100 * midiOf(track.f0[i + k], p.refA);
      const pre = new Float64Array(len + 1);
      for (let k = 0; k < len; k++) pre[k + 1] = pre[k] + c[k];
      let prev = -1, age = 0, shift = 0;
      for (let k = 0; k < len; k++) {
        const lo = Math.max(0, k - R), hi = Math.min(len - 1, k + R);
        const center = (pre[hi + 1] - pre[lo]) / (hi - lo + 1);
        const m = center / 100;
        const best = nearestAllowed(m, p.mask);
        let T;
        if (Number.isNaN(best)) T = m;
        else {
          T = best;
          if (prev >= 0 && p.mask[((prev % 12) + 12) % 12] && Math.abs(m - prev) <= Math.abs(m - best) + 0.25) T = prev;
        }
        if (T !== prev) { age = 0; prev = T; } else age += hop;
        const desired = (T - m) * 100;
        const tau = (p.retuneMs / 1000) * (1 + (p.humanize / 100) * 6 * Math.min(age, 1.5));
        const a = tau < 1e-4 ? 1 : 1 - Math.exp(-hop / tau);
        shift += (desired - shift) * a;
        const vib = c[k] - center;
        let s = shift + vib * (p.vibAmt - 1);
        if (p.cvDepth > 0) {
          const env = Math.min(1, Math.max(0, (age - p.cvDelay) / 0.25));
          s += p.cvDepth * env * lfo(p.cvShape, age * p.cvRate);
        }
        s += p.transpose * 100 + p.detune;
        s = Math.max(-2400, Math.min(2400, s));
        const idx = i + k;
        shiftC[idx] = s;
        inM[idx] = c[k] / 100;
        outM[idx] = c[k] / 100 + s / 100;
        tgtM[idx] = T;
        cenM[idx] = m;
      }
      i = j;
    }
    return { inM, outM, tgtM, cenM, shiftC };
  }

  /* ---------- Resíntesis PSOLA ---------- */
  function render(x, sr, track, pl, p) {
    const n = x.length, hop = track.hopSec, t0 = track.t0;
    const f0 = track.f0, nf = f0.length;
    const shiftC = pl.shiftC;

    function frameIdx(t) {
      const fi = (t - t0) / hop;
      return Math.max(0, Math.min(nf - 1, fi));
    }
    function f0At(t) {
      if (nf === 0) return 0;
      const fi = frameIdx(t);
      const a = Math.floor(fi), b = Math.min(nf - 1, a + 1);
      if (f0[a] > 0 && f0[b] > 0) return f0[a] + (f0[b] - f0[a]) * (fi - a);
      return f0[Math.round(fi)];
    }
    function shiftAt(t) {
      const fi = frameIdx(t);
      const a = Math.floor(fi), b = Math.min(nf - 1, a + 1);
      if (f0[a] > 0 && f0[b] > 0) return shiftC[a] + (shiftC[b] - shiftC[a]) * (fi - a);
      return shiftC[Math.round(fi)];
    }

    // marcas de análisis
    const mk = [], per = [], vo = [];
    const U = Math.round(0.008 * sr);
    let pos = 0, runActive = false, pol = 1, last = 0;
    while (pos < n) {
      const f = f0At(pos / sr);
      if (!(f > 0)) {
        mk.push(pos); per.push(U); vo.push(0);
        pos += U; runActive = false;
        continue;
      }
      const P = sr / f;
      let m;
      if (!runActive) {
        const lo = pos, hi = Math.min(n - 1, Math.round(pos + P));
        let mp = -Infinity, mn = -Infinity, ip = lo, im = lo;
        for (let i = lo; i <= hi; i++) {
          if (x[i] > mp) { mp = x[i]; ip = i; }
          if (-x[i] > mn) { mn = -x[i]; im = i; }
        }
        if (mp >= mn) { pol = 1; m = ip; } else { pol = -1; m = im; }
        runActive = true;
      } else {
        const exp = last + P;
        const lo = Math.max(last + Math.round(0.5 * P), Math.round(exp - 0.3 * P));
        const hi = Math.min(n - 1, Math.round(exp + 0.3 * P));
        if (lo > hi) m = Math.min(n - 1, Math.round(exp));
        else {
          let bv = -Infinity; m = lo;
          for (let i = lo; i <= hi; i++) {
            const v = pol * x[i];
            if (v > bv) { bv = v; m = i; }
          }
        }
      }
      mk.push(m); per.push(P); vo.push(1);
      last = m;
      pos = m + Math.max(1, Math.round(P));
    }
    const nm = mk.length;
    const out = new Float32Array(n);
    const ws = new Float32Array(n);
    if (nm === 0) return Float32Array.from(x);

    const fform = Math.pow(2, p.formant / 12);
    let k = 0, ts = 0;
    while (ts < n) {
      while (k + 1 < nm && Math.abs(mk[k + 1] - ts) < Math.abs(mk[k] - ts)) k++;
      const voiced = vo[k] === 1;
      let r = 1, step;
      if (voiced) {
        r = Math.pow(2, shiftAt(ts / sr) / 1200);
        step = per[k] / r;
      } else {
        ts = mk[k];
        step = per[k];
      }
      const L = Math.max(8, Math.round(per[k]));
      const fs = voiced ? fform : 1;
      const c = mk[k];
      const o = Math.round(ts);
      for (let i = -L; i <= L; i++) {
        const j = o + i;
        if (j < 0 || j >= n) continue;
        const sp = c + i * fs;
        const s0 = Math.floor(sp);
        const fr = sp - s0;
        const a = s0 >= 0 && s0 < n ? x[s0] : 0;
        const b = s0 + 1 >= 0 && s0 + 1 < n ? x[s0 + 1] : 0;
        const w = 0.5 * (1 + Math.cos((Math.PI * i) / L));
        out[j] += (a + (b - a) * fr) * w;
        ws[j] += w;
      }
      ts += Math.max(4, step);
    }
    let peak = 0;
    const mix = p.mix;
    for (let i = 0; i < n; i++) {
      const wet = out[i] / Math.max(ws[i], 0.2);
      const v = x[i] * (1 - mix) + wet * mix;
      out[i] = v;
      const av = Math.abs(v);
      if (av > peak) peak = av;
    }
    if (peak > 0.98) for (let i = 0; i < n; i++) out[i] *= 0.98 / peak;
    return out;
  }

  /* ---------- Voz de demostración ---------- */
  function demoVoice(sr) {
    const rnd = mulberry32(7);
    const notes = [
      [64, 0.62, 38], [67, 0.55, -27], [69, 0.8, 44], [67, 0.5, -35],
      [64, 0.6, 22], [62, 0.5, -41], [60, 0.55, 30], [62, 0.5, 18], [64, 1.0, -33],
    ];
    const gap = 0.14, lead = 0.15;
    let total = lead;
    for (const nt of notes) total += nt[1] + gap;
    const n = Math.round(total * sr);
    const x = new Float32Array(n);
    let ph = 0, jit = 0, idx = Math.round(lead * sr);
    for (const [midi, dur, err] of notes) {
      const len = Math.round(dur * sr);
      for (let i = 0; i < len; i++) {
        const t = i / sr;
        jit += (rnd() - 0.5) * 0.9;
        jit *= 0.998;
        const slide = t < 0.09 ? -70 * (1 - t / 0.09) * (1 - t / 0.09) : 0;
        const vibEnv = Math.min(1, Math.max(0, (t - 0.18) / 0.2));
        const vib = 24 * vibEnv * Math.sin(2 * Math.PI * 5.3 * t);
        const cents = err + slide + vib + jit;
        const f0 = 440 * Math.pow(2, (midi - 69) / 12 + cents / 1200);
        ph += (2 * Math.PI * f0) / sr;
        const env = Math.min(1, t / 0.03) * Math.min(1, (dur - t) / 0.09);
        let s = 0;
        for (let h = 1; h * f0 < 4200 && h * f0 < sr * 0.45; h++) {
          const fh = h * f0;
          const g =
            0.12 +
            Math.exp(-Math.pow((fh - 750) / 160, 2)) +
            0.7 * Math.exp(-Math.pow((fh - 1200) / 220, 2)) +
            0.3 * Math.exp(-Math.pow((fh - 2700) / 320, 2));
          s += (Math.pow(1 / h, 1.05) * g) * Math.sin(h * ph);
        }
        x[idx + i] = (s + (rnd() - 0.5) * 0.02) * env;
      }
      idx += len + Math.round(gap * sr);
    }
    let peak = 0;
    for (let i = 0; i < n; i++) if (Math.abs(x[i]) > peak) peak = Math.abs(x[i]);
    for (let i = 0; i < n; i++) x[i] *= 0.6 / (peak || 1);
    return x;
  }

  /* ---------- WAV y ZIP ---------- */
  function encodeWav(x, sr) {
    const n = x.length;
    const buf = new ArrayBuffer(44 + n * 2);
    const v = new DataView(buf);
    const wr = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
    wr(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); wr(8, "WAVE");
    wr(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
    wr(36, "data"); v.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) {
      const s = Math.max(-1, Math.min(1, x[i]));
      v.setInt16(44 + i * 2, s < 0 ? s * 32768 : s * 32767, true);
    }
    return new Uint8Array(buf);
  }

  let crcTable = null;
  function crc32(b) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (let i = 0; i < 256; i++) {
        let c = i;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        crcTable[i] = c >>> 0;
      }
    }
    let c = 0xffffffff;
    for (let i = 0; i < b.length; i++) c = crcTable[(c ^ b[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function zipStore(name, data) {
    const nm = new TextEncoder().encode(name);
    const crc = crc32(data);
    const lh = 30 + nm.length;
    const cd = 46 + nm.length;
    const out = new Uint8Array(lh + data.length + cd + 22);
    const v = new DataView(out.buffer);
    let o = 0;
    v.setUint32(o, 0x04034b50, true); v.setUint16(o + 4, 20, true); v.setUint16(o + 6, 0, true);
    v.setUint16(o + 8, 0, true); v.setUint16(o + 10, 0, true); v.setUint16(o + 12, 0x21, true);
    v.setUint32(o + 14, crc, true); v.setUint32(o + 18, data.length, true); v.setUint32(o + 22, data.length, true);
    v.setUint16(o + 26, nm.length, true); v.setUint16(o + 28, 0, true);
    out.set(nm, o + 30); o += lh;
    out.set(data, o); o += data.length;
    const cdStart = o;
    v.setUint32(o, 0x02014b50, true); v.setUint16(o + 4, 20, true); v.setUint16(o + 6, 20, true);
    v.setUint16(o + 8, 0, true); v.setUint16(o + 10, 0, true); v.setUint16(o + 12, 0, true); v.setUint16(o + 14, 0x21, true);
    v.setUint32(o + 16, crc, true); v.setUint32(o + 20, data.length, true); v.setUint32(o + 24, data.length, true);
    v.setUint16(o + 28, nm.length, true); v.setUint16(o + 30, 0, true); v.setUint16(o + 32, 0, true);
    v.setUint16(o + 34, 0, true); v.setUint16(o + 36, 0, true); v.setUint32(o + 38, 0, true); v.setUint32(o + 42, 0, true);
    out.set(nm, o + 46); o += cd;
    v.setUint32(o, 0x06054b50, true); v.setUint16(o + 4, 0, true); v.setUint16(o + 6, 0, true);
    v.setUint16(o + 8, 1, true); v.setUint16(o + 10, 1, true); v.setUint32(o + 12, cd, true);
    v.setUint32(o + 16, cdStart, true); v.setUint16(o + 20, 0, true);
    return out;
  }

  return { SCALES, maskFor, analyze, plan, render, demoVoice, encodeWav, zipStore, midiOf };
})();

if (typeof module !== "undefined") module.exports = Tuner;
