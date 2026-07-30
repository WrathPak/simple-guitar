/// <reference types="vitest/config" />
import path from "node:path";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    fs: {
      // ui/ imports bridge message types from ../schema/gen/ts/bridge.ts (see
      // schema/README.md) -- that's outside this project's own root, so the
      // dev server needs to be told it's allowed to serve it.
      allow: [path.resolve(__dirname, "..")],
    },
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./src/test/setup.ts"],
    // Process real CSS so computed-style assertions (e.g. no page scroll) mean something.
    css: true,
  },
});
