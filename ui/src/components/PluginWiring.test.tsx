import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";
import { createFakeJuceBackend } from "../test/fakeJuceBackend";
import { Room } from "./Room";

/** Renders <Room/> wired to a fake JUCE backend, same pattern as RigWiring.test.tsx. */
function renderWithFakeBackend() {
  const fake = createFakeJuceBackend();
  window.__JUCE__ = { backend: fake.backend };
  render(<Room />);
  return fake;
}

const PLUGINS = [
  { id: "vst3-crystal-verb", name: "Crystal Verb", vendor: "Northwind Audio" },
  { id: "vst3-tape-echo", name: "Tape Echo", vendor: "Vallis DSP" },
];

/** Slot tuple shorthand: plain built-in types by slot index, every slot on. */
function slotsOf(...types: number[]) {
  return Array.from({ length: 6 }, (_, i) => ({ type: types[i] ?? 0, on: true }));
}

/** The six slots with a hosted plugin in slot 0 and the usual pair behind it. */
function slotsWithPlugin(pluginName: string | null, pluginMissing = false) {
  const slots = slotsOf(0, 2, 3);
  slots[0] = { type: 7, on: true, pluginName, pluginMissing } as (typeof slots)[number];
  return slots;
}

function triggerRigState(fake: ReturnType<typeof createFakeJuceBackend>, overrides: Record<string, unknown> = {}) {
  act(() => {
    fake.trigger("rigState", {
      type: "rigState",
      schemaVersion: 5,
      namModelName: null,
      namModelSampleRate: 0,
      irName: null,
      slots: slotsOf(1, 2, 3),
      library: { models: [], irs: [] },
      plugins: [],
      ...overrides,
    });
  });
}

function triggerLoadResult(fake: ReturnType<typeof createFakeJuceBackend>, ok: boolean, message: string) {
  act(() => {
    fake.trigger("loadResult", { type: "loadResult", kind: "plugin", ok, message });
  });
}

describe("Plugins in the add-pedal palette", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("lists the scanned plugins by name below the six built-in tiles", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));

    expect(screen.getByText("your plugins")).toBeInTheDocument();
    for (const plugin of PLUGINS) expect(screen.getByRole("button", { name: plugin.name })).toBeInTheDocument();
    // The built-in tiles are still there, and still first.
    for (const name of ["screamer", "echoes", "chamber", "squeeze", "wobble", "shiver"]) {
      expect(screen.getByRole("button", { name })).toBeInTheDocument();
    }
  });

  it("picking a plugin sends setSlotType(7) then setSlotPlugin for the first empty slot", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    fireEvent.click(screen.getByRole("button", { name: "Tape Echo" }));

    // Default floor = trio in slots 0-2, so the first empty slot is 3.
    expect(fake.emitted.at(-2)).toMatchObject({ id: "setSlotType", data: { type: "setSlotType", slot: 3, pedalType: 7 } });
    expect(fake.emitted.at(-1)).toMatchObject({
      id: "setSlotPlugin",
      data: { type: "setSlotPlugin", slot: 3, pluginId: "vst3-tape-echo" },
    });
  });

  it("stays open until the engine reports the instance built, then closes", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    fireEvent.click(screen.getByRole("button", { name: "Crystal Verb" }));
    expect(screen.getByRole("dialog", { name: "add pedal" })).toBeInTheDocument();

    triggerLoadResult(fake, true, "loaded Crystal Verb");
    expect(screen.queryByRole("dialog", { name: "add pedal" })).not.toBeInTheDocument();
  });

  it("shows a failed plugin load inline, right where the pick was made", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    fireEvent.click(screen.getByRole("button", { name: "Crystal Verb" }));
    triggerLoadResult(fake, false, "needs stereo in and out");

    expect(screen.getByRole("dialog", { name: "add pedal" })).toBeInTheDocument();
    expect(screen.getByText("needs stereo in and out")).toBeInTheDocument();
  });

  it("offers a scan instead of a list when no plugins are known yet", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: [] });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    expect(screen.getByText("no plugins yet")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "scan for plugins" }));
    expect(fake.emitted.at(-1)).toMatchObject({ id: "requestPluginScan", data: { type: "requestPluginScan" } });
    // While it runs the action reports itself rather than firing again.
    expect(screen.getByRole("button", { name: "scanning" })).toBeDisabled();
  });
});

describe("Plugin scan overlay (⋯ menu)", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("runs the whole scan: trigger, live progress, then the summary", () => {
    const fake = renderWithFakeBackend();

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Plugins" }));
    expect(screen.getByRole("dialog", { name: "plugins" })).toBeInTheDocument();
    expect(screen.getByText("no plugins yet")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "scan" }));
    expect(fake.emitted.at(-1)).toMatchObject({ id: "requestPluginScan", data: { type: "requestPluginScan" } });

    act(() => {
      fake.trigger("pluginScanProgress", { type: "pluginScanProgress", done: 2, total: 9, currentName: "Tape Echo" });
    });
    expect(screen.getByText("2/9")).toBeInTheDocument();
    expect(screen.getByText("Tape Echo")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "scanning" })).toBeDisabled();

    act(() => {
      fake.trigger("pluginScanDone", { type: "pluginScanDone", found: 7, failed: ["Crashy Chorus"] });
    });
    expect(screen.getByText("found 7")).toBeInTheDocument();
    expect(screen.getByText("Crashy Chorus")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "scan" })).toBeEnabled();
  });

  it("reports what is already known before any scan has run", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Plugins" }));

    expect(screen.getByText("2 plugins")).toBeInTheDocument();
  });
});

describe("Hosted plugin pedal", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("wears the plugin's name and a matte-black shell on the floor", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsWithPlugin("Crystal Verb"), plugins: PLUGINS });

    const trigger = screen.getByRole("button", { name: "Focus Crystal Verb" });
    expect(trigger).toBeInTheDocument();
    expect(trigger.querySelector(".pedal--utility")).not.toBeNull();
  });

  it("binds its one mix knob to slot{N}P1 and its footswitch to slot{N}On", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsWithPlugin("Crystal Verb"), plugins: PLUGINS });

    const footswitch = screen.getByRole("button", { name: "Crystal Verb bypass" });
    expect(footswitch).toHaveAttribute("aria-pressed", "true");
    fireEvent.click(footswitch);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "slot0On", data: { type: "valueChanged", value: 0 } });

    fireEvent.click(screen.getByRole("button", { name: "Focus Crystal Verb" }));
    const knobs = screen.getAllByRole("slider");
    expect(knobs).toHaveLength(1);

    const mix = screen.getByRole("slider", { name: "Mix" });
    fireEvent.pointerDown(mix, { clientY: 200, pointerId: 30, button: 0 });
    fireEvent.pointerMove(mix, { clientY: 100, pointerId: 30 });
    expect(fake.emitted.at(-1)?.id).toBe("slot0P1");
  });

  it("opens its native editor from the focus view", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsWithPlugin("Crystal Verb"), plugins: PLUGINS });

    fireEvent.click(screen.getByRole("button", { name: "Focus Crystal Verb" }));
    fireEvent.click(screen.getByRole("button", { name: "open editor" }));

    expect(fake.emitted.at(-1)).toMatchObject({ id: "openPluginEditor", data: { type: "openPluginEditor", slot: 0 } });
  });

  it("a built-in pedal has no open-editor action", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake);

    fireEvent.click(screen.getByRole("button", { name: "Focus screamer" }));
    expect(screen.queryByRole("button", { name: "open editor" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "remove" })).toBeInTheDocument();
  });
});

describe("Missing hosted plugin", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("reads as dimmed and disabled, with nothing to turn", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsWithPlugin("Crystal Verb", true), plugins: [] });

    const trigger = screen.getByRole("button", { name: "Focus Crystal Verb" });
    expect(trigger.querySelector(".pedal--missing")).not.toBeNull();
    expect(screen.getAllByText("missing").length).toBeGreaterThan(0);
    expect(screen.getByRole("button", { name: "Crystal Verb bypass" })).toBeDisabled();
  });

  it("can still be focused and removed, but offers no editor", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsWithPlugin("Crystal Verb", true), plugins: [] });

    fireEvent.click(screen.getByRole("button", { name: "Focus Crystal Verb" }));
    expect(screen.queryByRole("slider")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "open editor" })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "remove" }));
    expect(fake.emitted.at(-1)).toMatchObject({ id: "setSlotType", data: { type: "setSlotType", slot: 0, pedalType: 0 } });
  });
});
