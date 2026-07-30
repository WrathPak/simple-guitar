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
    fake.trigger("rigState", {
      type: "rigState",
      schemaVersion: 3,
      namModelName: null,
      namModelSampleRate: 0,
      irName: null,
      chainOrder: ["screamer", "echoes", "chamber"],
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
    fake.trigger("rigState", {
      type: "rigState",
      schemaVersion: 3,
      namModelName: null,
      namModelSampleRate: 0,
      irName: null,
      chainOrder: ["screamer", "echoes", "chamber"],
      library: { models: [], irs: [] },
    });

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
    fake.trigger("rigState", {
      type: "rigState",
      schemaVersion: 3,
      namModelName: null,
      namModelSampleRate: 0,
      irName: "4x12 V30",
      chainOrder: ["screamer", "echoes", "chamber"],
      library: { models: [], irs: [] },
    });

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
    fake.trigger("rigState", {
      type: "rigState",
      schemaVersion: 3,
      namModelName: null,
      namModelSampleRate: 0,
      irName: null,
      chainOrder: ["screamer", "echoes", "chamber"],
      library: { models: [], irs: [{ name: "2x12 Blue", path: "C:\\IRs\\2x12 Blue.wav" }] },
    });

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

describe("Floor pedal knob <-> bridge round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("Screamer's Drive knob emits on the screamerDrive channel", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus screamer" }));
    const driveKnob = screen.getByRole("slider", { name: "Drive" });

    fireEvent.pointerDown(driveKnob, { clientY: 200, pointerId: 10, button: 0 });
    fireEvent.pointerMove(driveKnob, { clientY: 100, pointerId: 10 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("screamerDrive");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged" });
    expect((lastEmit!.data as { value: number }).value).toBeGreaterThan(0.5);
  });

  it("Echoes' Time knob emits on the echoesTime channel", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus echoes" }));
    const timeKnob = screen.getByRole("slider", { name: "Time" });

    fireEvent.pointerDown(timeKnob, { clientY: 200, pointerId: 11, button: 0 });
    fireEvent.pointerMove(timeKnob, { clientY: 100, pointerId: 11 });

    expect(fake.emitted.at(-1)?.id).toBe("echoesTime");
  });

  it("Chamber's Decay knob emits on the chamberDecay channel", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Focus chamber" }));
    const decayKnob = screen.getByRole("slider", { name: "Decay" });

    fireEvent.pointerDown(decayKnob, { clientY: 200, pointerId: 12, button: 0 });
    fireEvent.pointerMove(decayKnob, { clientY: 100, pointerId: 12 });

    expect(fake.emitted.at(-1)?.id).toBe("chamberDecay");
  });
});

describe("Pedal footswitch <-> *On param round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("wide footswitch click emits the *On param, and the focused instance reflects the same shared state", () => {
    const fake = renderWithFakeBackend();

    // screamerOn defaults to false (bypassed) -- clicking the wide
    // footswitch should turn it on.
    const wideFootswitch = screen.getByRole("button", { name: "screamer bypass" });
    expect(wideFootswitch).toHaveAttribute("aria-pressed", "false");

    fireEvent.click(wideFootswitch);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "screamerOn", data: { type: "valueChanged", value: 1 } });
    expect(wideFootswitch).toHaveAttribute("aria-pressed", "true");

    // Focusing the pedal renders a second footswitch (wide row + focused
    // card); both must read the same on state -- there's no separate
    // UI-local bypass state left to fall out of sync (that's the whole
    // point of binding the footswitch straight to the shared *On param).
    fireEvent.click(screen.getByRole("button", { name: "Focus screamer" }));
    const footswitches = screen.getAllByRole("button", { name: "screamer bypass" });
    expect(footswitches.length).toBeGreaterThanOrEqual(2);
    for (const footswitch of footswitches) expect(footswitch).toHaveAttribute("aria-pressed", "true");
  });

  it("backend-originated *On changes update the footswitch too (host automation / preset restore)", () => {
    const fake = renderWithFakeBackend();
    const footswitch = screen.getByRole("button", { name: "chamber bypass" });
    expect(footswitch).toHaveAttribute("aria-pressed", "true"); // chamberOn defaults to true

    act(() => {
      fake.trigger("chamberOn", { type: "valueChanged", value: 0 });
    });
    expect(footswitch).toHaveAttribute("aria-pressed", "false");
  });
});

describe("Chain order <-> bridge round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("committing a drag-reorder sends setChainOrder with a permutation of the three pedal ids", () => {
    const fake = renderWithFakeBackend();

    const screamerSlot = screen.getByRole("button", { name: "Focus screamer" }).closest(".pedal-slot");
    expect(screamerSlot).not.toBeNull();

    fireEvent.pointerDown(screamerSlot!, { clientX: 100, clientY: 0, pointerId: 20, button: 0 });
    fireEvent.pointerMove(screamerSlot!, { clientX: 400, clientY: 0, pointerId: 20 });
    fireEvent.pointerUp(screamerSlot!, { pointerId: 20 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("setChainOrder");
    expect(lastEmit?.data).toMatchObject({ type: "setChainOrder" });

    const order = (lastEmit!.data as { order: string[] }).order;
    expect(order).toHaveLength(3);
    expect(new Set(order)).toEqual(new Set(["screamer", "echoes", "chamber"]));
  });

  it("adopts chainOrder from rigState, rearranging the floor (e.g. after a DAW session restore)", () => {
    const fake = renderWithFakeBackend();
    act(() => {
      fake.trigger("rigState", {
        type: "rigState",
        schemaVersion: 3,
        namModelName: null,
        namModelSampleRate: 0,
        irName: null,
        chainOrder: ["chamber", "screamer", "echoes"],
        library: { models: [], irs: [] },
      });
    });

    const focusButtons = screen.getAllByRole("button", { name: /^Focus (screamer|echoes|chamber)$/ });
    const order = focusButtons.map((button) => button.getAttribute("aria-label")!.replace("Focus ", ""));
    expect(order).toEqual(["chamber", "screamer", "echoes"]);
  });
});
