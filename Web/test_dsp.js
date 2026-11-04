const Tuner = require("./dsp.js");
const fs = require("fs");

function stats(track, mask, refA) {
  let sum = 0, cnt = 0, within = 0;
  for (let i = 0; i < track.f0.length; i++) {
    const f = track.f0[i];
    if (f <= 0) continue;
    const m = Tuner.midiOf(f, refA);
    let best = Infinity;
    for (let n = Math.round(m) - 3; n <= Math.round(m) + 3; n++) {
      if (mask[((n % 12) + 12) % 12]) best = Math.min(best, Math.abs(n - m) * 100);
    }
    sum += best; cnt++;
    if (best < 15) within++;
  }
  return { meanErr: (sum / cnt).toFixed(1), voicedFrames: cnt, pctWithin15: ((100 * within) / cnt).toFixed(0) };
}

(async () => {
  const sr = 44100;
  const x = Tuner.demoVoice(sr);
  console.log("demo samples", x.length, (x.length / sr).toFixed(2) + "s");
  let t = Date.now();
  const tr = await Tuner.analyze(x, sr);
  console.log("analysis ms", Date.now() - t, "frames", tr.f0.length, "voiced", tr.f0.filter((v) => v > 0).length);
  const mask = Tuner.maskFor(0, "major");
  console.log("input vs C major:", JSON.stringify(stats(tr, mask, 440)));

  const base = { refA: 440, mask, retuneMs: 0, humanize: 0, vibAmt: 0, cvShape: "sine", cvRate: 5.5, cvDepth: 0, cvDelay: 0.3, transpose: 0, detune: 0, formant: 0, mix: 1 };
  for (const [label, over] of [
    ["retune 0, vib 0", {}],
    ["retune 40ms, vib natural", { retuneMs: 40, vibAmt: 1 }],
    ["retune 0, transpose +12", { transpose: 12 }],
    ["retune 0, formant +4", { formant: 4 }],
  ]) {
    const p = Object.assign({}, base, over);
    const pl = Tuner.plan(tr, p);
    t = Date.now();
    const y = Tuner.render(x, sr, tr, pl, p);
    const ms = Date.now() - t;
    const tr2 = await Tuner.analyze(y, sr);
    let m2 = mask;
    if (over.transpose) m2 = Tuner.maskFor(0, "chromatic");
    console.log(label, "| render ms", ms, "| out:", JSON.stringify(stats(tr2, m2, 440)));
    if (label === "retune 0, vib 0") fs.writeFileSync("out.wav", Buffer.from(Tuner.encodeWav(y, sr)));
  }
  const z = Tuner.zipStore("test.wav", Tuner.encodeWav(x.slice(0, 1000), sr));
  fs.writeFileSync("t.zip", Buffer.from(z));
  console.log("zip bytes", z.length);
})();
