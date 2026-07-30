#!/usr/bin/env node
// Sanity check that bridge.schema.json is itself a valid JSON Schema
// (draft 2020-12) and that a few representative sample messages validate
// against it correctly (both accept and reject cases).

import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const here = path.dirname(fileURLToPath(import.meta.url));
const schemaPath = path.join(here, "bridge.schema.json");

async function main() {
  const schema = JSON.parse(await readFile(schemaPath, "utf8"));

  const ajv = new Ajv2020({ strict: true, allErrors: true });
  addFormats(ajv);

  // Compiling the schema is itself the meta-schema-validity check: ajv
  // validates the schema document against the draft 2020-12 meta-schema
  // before it will compile it, and throws if it doesn't conform.
  const validate = ajv.compile(schema);

  const cases = [
    { name: "setParam (valid)", ok: true, data: { type: "setParam", paramId: "outputGain", value: 0.5 } },
    { name: "setParam (value out of range)", ok: false, data: { type: "setParam", paramId: "outputGain", value: 1.5 } },
    { name: "setParam (unknown paramId)", ok: false, data: { type: "setParam", paramId: "bogus", value: 0.5 } },
    { name: "meterFrame (valid)", ok: true, data: { type: "meterFrame", inPeakDb: -12.3, outPeakDb: -6 } },
    { name: "meterFrame (missing field)", ok: false, data: { type: "meterFrame", inPeakDb: -12.3 } },
    {
      name: "stateChanged (valid)",
      ok: true,
      data: { type: "stateChanged", schemaVersion: 3, params: { outputGain: 0.75 } },
    },
    {
      name: "stateChanged (wrong schemaVersion)",
      ok: false,
      data: { type: "stateChanged", schemaVersion: 1, params: { outputGain: 0.75 } },
    },
    { name: "unknown message type", ok: false, data: { type: "flarb" } },
  ];

  let failures = 0;
  for (const c of cases) {
    const valid = validate(c.data);
    const pass = valid === c.ok;
    console.log(`${pass ? "PASS" : "FAIL"} — ${c.name} (expected ${c.ok ? "valid" : "invalid"}, got ${valid ? "valid" : "invalid"})`);
    if (!pass) {
      failures++;
      if (!valid) console.log("  errors:", JSON.stringify(validate.errors));
    }
  }

  if (failures > 0) {
    console.error(`\n${failures} case(s) did not match expectations.`);
    process.exitCode = 1;
  } else {
    console.log("\nAll cases matched expectations. bridge.schema.json is valid draft 2020-12.");
  }
}

main().catch((err) => {
  console.error(err);
  process.exitCode = 1;
});
