# Bridge schema

Single source of truth for the C++ (engine/app) <-> React (ui) message protocol.

## Contract-first rule

**Any new UI<->engine capability starts with a schema PR.** That means:

1. Edit `bridge.schema.json` (add/extend a `$defs` entry, wire it into `UiToEngineMessage` or `EngineToUiMessage`).
2. Run `npm run gen` (see below) to regenerate `gen/ts/bridge.ts`.
3. Commit the regenerated output alongside the schema change — it's checked in, not built on the fly in CI, so `ui/` always has types to import even before a full toolchain run.
4. Only then do engine (`app/` APVTS params, message handling) and UI (`ui/src` components/views) work against the new shape, in parallel if desired.

**TypeScript types are generated, never hand-written.** `gen/ts/bridge.ts` is produced by `gen-types.mjs` from `bridge.schema.json` via [`json-schema-to-typescript`](https://www.npmjs.com/package/json-schema-to-typescript). If a type looks wrong, fix the schema and regenerate — do not hand-edit the generated file. `ui/` imports its bridge message types from `schema/gen/ts/bridge.ts` (e.g. `import type { SetParam, MeterFrame, StateChanged } from "../../schema/gen/ts/bridge"` or via a path alias configured in `ui/`'s Vite/TS config).

## Protocol versioning

`SchemaVersion` (currently `0`, i.e. bridge protocol v0) is a `const` in the schema and is carried on every `stateChanged` message. Bump it whenever a message shape changes in a backwards-incompatible way; the UI can use it to detect a stale webview bundle talking to a newer/older engine build instead of silently desyncing.

## Files

- `bridge.schema.json` — the schema (JSON Schema draft 2020-12).
- `gen-types.mjs` — generator script (Node). Reads `bridge.schema.json`, writes `gen/ts/bridge.ts`.
- `gen/ts/bridge.ts` — generated TypeScript types. Checked in for convenience; regenerate after every schema edit rather than trusting a stale copy.
- `package.json` — pins the `json-schema-to-typescript` devDependency used by the generator.
- `tsconfig.json` — minimal strict config used only to sanity-check that the generated output compiles (`npx tsc --noEmit --strict -p schema/tsconfig.json`). Not used to build anything.

## Commands

Run from `schema/`:

```sh
npm install       # once, or after editing package.json
npm run gen       # regenerate gen/ts/bridge.ts from bridge.schema.json
npm run typecheck # npx tsc --noEmit --strict over the generated output
```
