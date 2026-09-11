#!/usr/bin/env node

import { createReadStream, createWriteStream, existsSync } from "node:fs";
import { createInterface } from "node:readline";
import { once } from "node:events";

if (process.argv.length !== 4) {
  console.error(`usage: ${process.argv[1]} INPUT_BLOCKS OUTPUT_BLOCKS`);
  process.exit(2);
}

const inputPath = process.argv[2];
const outputPath = process.argv[3];
if (existsSync(outputPath)) {
  throw new Error(`refusing to overwrite output: ${outputPath}`);
}

const mapping = new Map([
  ["Bw", "Bw"],
  ["DsS", "Dpc"],
  ["C{", "C{"],
  ["Dv?", "Dy_"],
  ["Cw", "Cs"],
  ["C]", "C]"],
  ["DfO", "Dj_"],
  ["Dm?", "Dk_"],
  ["DMo", "DLo"]
]);

const input = createInterface({
  input: createReadStream(inputPath),
  crlfDelay: Infinity
});
const output = createWriteStream(outputPath, { flags: "wx" });
let references = 0;
let changed = 0;

for await (const line of input) {
  const converted = line.replace(/graph6=(\S+)/g, (whole, encoded) => {
    const standard = mapping.get(encoded);
    if (standard === undefined) {
      throw new Error(`unknown development graph6 string: ${encoded}`);
    }
    references += 1;
    if (standard !== encoded) changed += 1;
    return `graph6=${standard}`;
  });
  if (!output.write(`${converted}\n`)) await once(output, "drain");
}
output.end();
await once(output, "finish");

console.log(
  `policy-graph6=NORMALIZED references=${references} changed=${changed} ` +
  `input=${inputPath} output=${outputPath}`
);
