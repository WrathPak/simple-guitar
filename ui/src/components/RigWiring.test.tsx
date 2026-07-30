import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";
import { createFakeJuceBackend } from "../test/fakeJuceBackend";
import { Room } from "./Room";

/** Renders <Room/> wired to a fake JUCE backend, same pattern as Room.test.tsx's OUTPUT round trip. */
function renderWithFakeBackend() {
  const fake = createFakeJuceBackend();
  window.__JUCE__ = { backend: fake.backend };
  render(<Room />);
  return fake;
}

/** Slot tuple shorthand: types by slot index, every slot on (the flat default). */
function slotsOf(...types: number[]) {
  return Array.from({ length: 6 }, (_, i) => ({ type: types[i] ?? 0, on: true }));
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

describe("Amp EQ knob <-> bridge round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("Bass knob emits on the ampBassDb channel and reflects backend-originated updates", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    const bassKnob = screen.getByRole("slider", { name: "Bass" });

    fireEvent.pointerDown(bassKnob, { clientY: 200, pointerId: 1, button: 0 });
    fireEvent.pointerMove(bassKnob, { clientY: 100, pointerId: 1 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("ampBassDb");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged" });
    expect((lastEmit!.data as { value: number }).value).toBeGreaterThan(0.5);
  });

  it("Treble knob is wired to its own ampTrebleDb channel, independent of Bass", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    const trebleKnob = screen.getByRole("slider", { name: "Treble" });

    fireEvent.pointerDown(trebleKnob, { clientY: 200, pointerId: 2, button: 0 });
    fireEvent.pointerMove(trebleKnob, { clientY: 130, pointerId: 2 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("ampTrebleDb");
  });
});

describe("Bool toggle <-> bridge round trip (namNormalize)", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("clicking Normalize emits 1 then 0 as normalized bool values", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));

    const normalizeButton = screen.getByRole("button", { name: "Normalize" });
    expect(normalizeButton).toHaveAttribute("aria-pressed", "false");

    fireEvent.click(normalizeButton);
    let lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("namNormalize");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged", value: 1 });
    expect(normalizeButton).toHaveAttribute("aria-pressed", "true");

    fireEvent.click(normalizeButton);
    lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged", value: 0 });
    expect(normalizeButton).toHaveAttribute("aria-pressed", "false");
  });
});

describe("Model picker overlay", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("opening it sends requestRigState", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    fireEvent.click(screen.getByText("change model ▾"));

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("requestRigState");
    expect(lastEmit?.data).toMatchObject({ type: "requestRigState" });
  });

  it("selecting a model sends loadNamModel with the exact library path", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, {
      library: {
        models: [{ name: "Mark IIC+", path: "C:\\test-home\\Documents\\Simple Guitar\\Models\\Mark IIC+.nam" }],
        irs: [],
      },
    });

    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    fireEvent.click(screen.getByText("change model ▾"));
    fireEvent.click(screen.getByText("Mark IIC+"));

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("loadNamModel");
    expect(lastEmit?.data).toMatchObject({
      type: "loadNamModel",
      path: "C:\\test-home\\Documents\\Simple Guitar\\Models\\Mark IIC+.nam",
    });
  });

  it("shows the plain-voice empty-library copy when there are no models", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake);

    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    fireEvent.click(screen.getByText("change model ▾"));

    expect(screen.getByText(/no models found/)).toBeInTheDocument();
    expect(screen.getByText(/Documents\\Simple Guitar\\Models/)).toBeInTheDocument();
  });
});

describe("Cab focus and wiring", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("is focusable from the wide shot and shows IR name, cut knobs, and a power toggle bound to cabOn", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { irName: "4x12 V30" });

    fireEvent.click(screen.getByRole("button", { name: "Focus cabinet" }));
    expect(screen.getByText("4x12 V30")).toBeInTheDocument();
    expect(screen.getByText("change ir ▾")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Low Cut" })).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "High Cut" })).toBeInTheDocument();

    const power = screen.getByRole("button", { name: "Cab power" });
    expect(power).toHaveAttribute("aria-pressed", "true");

    fireEvent.click(power);
    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("cabOn");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged", value: 0 });
    expect(power).toHaveAttribute("aria-pressed", "false");

    // Off = the cab reads as visually dimmed in the wide shot too (same
    // desaturate+dim convention as pedal bypass), not just in focus.
    const wideCab = screen.getByRole("button", { name: "Focus cabinet" });
    expect(wideCab).toHaveClass("cab--off");
  });

  it("opening the IR overlay sends requestRigState and selecting an entry sends loadIr with its exact path", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { library: { models: [], irs: [{ name: "2x12 Blue", path: "C:\\IRs\\2x12 Blue.wav" }] } });

    fireEvent.click(screen.getByRole("button", { name: "Focus cabinet" }));
    fireEvent.click(screen.getByText("change ir ▾"));
    expect(fake.emitted.at(-1)?.id).toBe("requestRigState");

    fireEvent.click(screen.getByText("2x12 Blue"));
    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("loadIr");
    expect(lastEmit?.data).toMatchObject({ type: "loadIr", path: "C:\\IRs\\2x12 Blue.wav" });
  });
});

describe("Gate overlay (⋯ menu)", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("opens from the ⋯ menu and wires the on/off toggle plus the threshold knob", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Gate" }));

    const gateToggle = screen.getByRole("button", { name: "Gate" });
    expect(gateToggle).toHaveAttribute("aria-pressed", "false");
    fireEvent.click(gateToggle);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "gateOn", data: { type: "valueChanged", value: 1 } });

    const thresholdKnob = screen.getByRole("slider", { name: "Threshold" });
    fireEvent.pointerDown(thresholdKnob, { clientY: 200, pointerId: 3, button: 0 });
    fireEvent.pointerMove(thresholdKnob, { clientY: 150, pointerId: 3 });
    expect(fake.emitted.at(-1)?.id).toBe("gateThresholdDb");
  });

  it("Esc closes the gate overlay without also dropping amp focus", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Gate" }));
    expect(screen.getByRole("dialog", { name: "Gate" })).toBeInTheDocument();

    fireEvent.keyDown(window, { key: "Escape" });
    expect(screen.queryByRole("dialog", { name: "Gate" })).not.toBeInTheDocument();
    // Amp is still focused -- one Esc only closed the overlay.
    expect(screen.getByRole("slider", { name: "Output" })).toBeInTheDocument();
  });
});

describe("Slot pedal knob <-> bridge round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("the screamer in slot 0 binds Drive to the slot0P1 channel", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus screamer" }));
    const driveKnob = screen.getByRole("slider", { name: "Drive" });

    fireEvent.pointerDown(driveKnob, { clientY: 200, pointerId: 10, button: 0 });
    fireEvent.pointerMove(driveKnob, { clientY: 100, pointerId: 10 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("slot0P1");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged" });
    expect((lastEmit!.data as { value: number }).value).toBeGreaterThan(0.5);
  });

  it("knob params follow the pedal's slot, not its type: echoes in slot 2 emits on slot2P1", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(3, 1, 2) });

    fireEvent.click(screen.getByRole("button", { name: "Focus echoes" }));
    const timeKnob = screen.getByRole("slider", { name: "Time" });

    fireEvent.pointerDown(timeKnob, { clientY: 200, pointerId: 11, button: 0 });
    fireEvent.pointerMove(timeKnob, { clientY: 100, pointerId: 11 });

    expect(fake.emitted.at(-1)?.id).toBe("slot2P1");
  });

  it("a 4-knob type (squeeze) binds its fourth knob to P4", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(4) });

    fireEvent.click(screen.getByRole("button", { name: "Focus squeeze" }));
    for (const label of ["Amount", "Attack", "Release", "Level"]) {
      expect(screen.getByRole("slider", { name: label })).toBeInTheDocument();
    }

    const levelKnob = screen.getByRole("slider", { name: "Level" });
    fireEvent.pointerDown(levelKnob, { clientY: 200, pointerId: 12, button: 0 });
    fireEvent.pointerMove(levelKnob, { clientY: 100, pointerId: 12 });

    expect(fake.emitted.at(-1)?.id).toBe("slot0P4");
  });
});

describe("Pedal footswitch <-> slot{N}On round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("wide footswitch click emits slot0On, and the focused instance reflects the same shared state", () => {
    const fake = renderWithFakeBackend();

    // Every slot's On defaults true -- clicking the wide footswitch bypasses.
    const wideFootswitch = screen.getByRole("button", { name: "screamer bypass" });
    expect(wideFootswitch).toHaveAttribute("aria-pressed", "true");

    fireEvent.click(wideFootswitch);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "slot0On", data: { type: "valueChanged", value: 0 } });
    expect(wideFootswitch).toHaveAttribute("aria-pressed", "false");

    // Focusing the pedal renders a second footswitch (wide row + focused
    // card); both must read the same on state -- there's no separate
    // UI-local bypass state left to fall out of sync (that's the whole
    // point of binding the footswitch straight to the shared slot{N}On).
    fireEvent.click(screen.getByRole("button", { name: "Focus screamer" }));
    const footswitches = screen.getAllByRole("button", { name: "screamer bypass" });
    expect(footswitches.length).toBeGreaterThanOrEqual(2);
    for (const footswitch of footswitches) expect(footswitch).toHaveAttribute("aria-pressed", "false");
  });

  it("backend-originated slot{N}On changes update the footswitch too (host automation / preset restore)", () => {
    const fake = renderWithFakeBackend();
    const footswitch = screen.getByRole("button", { name: "chamber bypass" });
    expect(footswitch).toHaveAttribute("aria-pressed", "true");

    act(() => {
      fake.trigger("slot2On", { type: "valueChanged", value: 0 });
    });
    expect(footswitch).toHaveAttribute("aria-pressed", "false");
  });
});

describe("Reorder <-> movePedal round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("committing a drag-reorder sends movePedal with from/to slot indices", () => {
    const fake = renderWithFakeBackend();

    const screamerSlot = screen.getByRole("button", { name: "Focus screamer" }).closest(".pedal-slot");
    expect(screamerSlot).not.toBeNull();

    fireEvent.pointerDown(screamerSlot!, { clientX: 100, clientY: 0, pointerId: 20, button: 0 });
    fireEvent.pointerMove(screamerSlot!, { clientX: 400, clientY: 0, pointerId: 20 });
    fireEvent.pointerUp(screamerSlot!, { pointerId: 20 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("movePedal");
    expect(lastEmit?.data).toMatchObject({ type: "movePedal", from: 0, to: 2 });
  });

  it("maps visual positions to sparse slot indices (pedals in slots 0 and 2)", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(1, 0, 3) });

    const screamerSlot = screen.getByRole("button", { name: "Focus screamer" }).closest(".pedal-slot");
    fireEvent.pointerDown(screamerSlot!, { clientX: 100, clientY: 0, pointerId: 21, button: 0 });
    fireEvent.pointerMove(screamerSlot!, { clientX: 400, clientY: 0, pointerId: 21 });
    fireEvent.pointerUp(screamerSlot!, { pointerId: 21 });

    // Visually the screamer moved from position 0 to 1; the chamber it
    // passed lives in slot 2, so the message names slots 0 -> 2.
    expect(fake.emitted.at(-1)).toMatchObject({ id: "movePedal", data: { type: "movePedal", from: 0, to: 2 } });
  });

  it("Shift+ArrowRight swaps with the right neighbor via movePedal", () => {
    const fake = renderWithFakeBackend();

    const screamerTrigger = screen.getByRole("button", { name: "Focus screamer" });
    fireEvent.keyDown(screamerTrigger, { key: "ArrowRight", shiftKey: true });

    expect(fake.emitted.at(-1)).toMatchObject({ id: "movePedal", data: { type: "movePedal", from: 0, to: 1 } });
  });

  it("adopts slots from rigState, rearranging the floor (e.g. after a DAW session restore or preset load)", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(3, 1, 2) });

    const focusButtons = screen.getAllByRole("button", { name: /^Focus (screamer|echoes|chamber|squeeze|wobble|shiver)$/ });
    const order = focusButtons.map((button) => button.getAttribute("aria-label")!.replace("Focus ", ""));
    expect(order).toEqual(["chamber", "screamer", "echoes"]);
  });
});

describe("Add-pedal palette", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("＋ opens the palette overlay listing all six pedal types", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));

    expect(screen.getByRole("dialog", { name: "add pedal" })).toBeInTheDocument();
    for (const name of ["screamer", "echoes", "chamber", "squeeze", "wobble", "shiver"]) {
      expect(screen.getByRole("button", { name })).toBeInTheDocument();
    }
  });

  it("picking a type sends setSlotType for the first empty slot and closes the palette", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    fireEvent.click(screen.getByRole("button", { name: "squeeze" }));

    // Default floor = trio in slots 0-2, so the first empty slot is 3.
    expect(fake.emitted.at(-1)).toMatchObject({ id: "setSlotType", data: { type: "setSlotType", slot: 3, pedalType: 4 } });
    expect(screen.queryByRole("dialog", { name: "add pedal" })).not.toBeInTheDocument();

    // The floor updates once the engine echoes the new slots.
    triggerRigState(fake, { slots: slotsOf(1, 2, 3, 4) });
    expect(screen.getByRole("button", { name: "Focus squeeze" })).toBeInTheDocument();
  });

  it("fills the first empty slot even when it sits between pedals", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(1, 0, 3) });

    fireEvent.click(screen.getByRole("button", { name: "add pedal" }));
    fireEvent.click(screen.getByRole("button", { name: "wobble" }));

    expect(fake.emitted.at(-1)).toMatchObject({ id: "setSlotType", data: { type: "setSlotType", slot: 1, pedalType: 5 } });
  });

  it("hides the ＋ spot when all six slots are full", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf(1, 2, 3, 4, 5, 6) });
    expect(screen.queryByRole("button", { name: "add pedal" })).not.toBeInTheDocument();
  });

  it("an empty board renders just the ＋ spot -- no pedals, no cables", () => {
    const fake = renderWithFakeBackend();
    triggerRigState(fake, { slots: slotsOf() });

    expect(screen.queryByRole("button", { name: /^Focus (screamer|echoes|chamber|squeeze|wobble|shiver)$/ })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "add pedal" })).toBeInTheDocument();
    expect(document.querySelectorAll(".cable-layer path")).toHaveLength(0);
  });
});

describe("Remove", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("the focused pedal's remove action empties its slot and steps back out", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus echoes" }));
    fireEvent.click(screen.getByRole("button", { name: "remove" }));

    expect(fake.emitted.at(-1)).toMatchObject({ id: "setSlotType", data: { type: "setSlotType", slot: 1, pedalType: 0 } });
    // Unfocused: no knobs on screen anymore.
    expect(screen.queryByRole("slider")).not.toBeInTheDocument();

    // The engine echoes the emptied slot; the row reflows without echoes.
    triggerRigState(fake, { slots: slotsOf(1, 0, 3) });
    expect(screen.queryByRole("button", { name: "Focus echoes" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Focus screamer" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Focus chamber" })).toBeInTheDocument();
  });

  it("the wide shot has no remove action -- it belongs to the focus view only", () => {
    renderWithFakeBackend();
    expect(screen.queryByRole("button", { name: "remove" })).not.toBeInTheDocument();
  });
});
