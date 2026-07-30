import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { createFakeJuceBackend } from "../test/fakeJuceBackend";
import { Room } from "./Room";

describe("Room camera", () => {
  beforeEach(() => {
    delete window.__JUCE__;
  });

  it("wide shot has no focused device", () => {
    render(<Room />);
    expect(screen.queryByRole("slider")).not.toBeInTheDocument();
    expect(screen.getByText(/Click gear to focus/)).toBeInTheDocument();
  });

  it("dollies in on amp click and steps back on Esc", () => {
    render(<Room />);

    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    expect(screen.getByRole("slider", { name: "Output" })).toBeInTheDocument();
    expect(screen.getByText("change model ▾")).toBeInTheDocument();
    expect(screen.getByText(/step back/)).toBeInTheDocument();

    fireEvent.keyDown(window, { key: "Escape" });
    expect(screen.queryByRole("slider", { name: "Output" })).not.toBeInTheDocument();
    expect(screen.getByText(/Click gear to focus/)).toBeInTheDocument();
  });

  it("dollies in on a pedal click and steps back on backdrop click", () => {
    render(<Room />);

    fireEvent.click(screen.getByRole("button", { name: "Focus echoes" }));
    expect(screen.getByRole("slider", { name: "Time" })).toBeInTheDocument();

    const backdrop = document.querySelector(".room-scene-wrap");
    expect(backdrop).not.toBeNull();
    fireEvent.click(backdrop!);
    expect(screen.queryByRole("slider", { name: "Time" })).not.toBeInTheDocument();
  });

  it("shows no contextual line for any pedal -- no time-based feature is wired up yet", () => {
    render(<Room />);
    fireEvent.click(screen.getByRole("button", { name: "Focus echoes" }));
    expect(document.querySelector(".chrome-contextual")).not.toBeInTheDocument();
  });
});

describe("Pedal footswitch bypass", () => {
  beforeEach(() => {
    delete window.__JUCE__;
  });

  it("toggles the shell and LED without triggering focus", () => {
    render(<Room />);

    const footswitch = screen.getByRole("button", { name: "echoes bypass" });
    const pedal = footswitch.closest(".pedal");
    expect(pedal).not.toBeNull();
    expect(pedal).not.toHaveClass("pedal--bypassed");
    expect(footswitch).toHaveAttribute("aria-pressed", "true");

    fireEvent.click(footswitch);
    expect(pedal).toHaveClass("pedal--bypassed");
    expect(footswitch).toHaveAttribute("aria-pressed", "false");
    // Bypassing must not focus the pedal.
    expect(screen.queryByRole("slider")).not.toBeInTheDocument();

    fireEvent.click(footswitch);
    expect(pedal).not.toHaveClass("pedal--bypassed");
    expect(footswitch).toHaveAttribute("aria-pressed", "true");
  });
});

describe("AmpDevice OUTPUT knob <-> bridge round trip", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("emits valueChanged when dragged, and reflects backend-originated updates", () => {
    const fake = createFakeJuceBackend();
    window.__JUCE__ = { backend: fake.backend };

    render(<Room />);
    fireEvent.click(screen.getByRole("button", { name: "Focus amplifier" }));
    const outputKnob = screen.getByRole("slider", { name: "Output" });

    fireEvent.pointerDown(outputKnob, { clientY: 200, pointerId: 1, button: 0 });
    fireEvent.pointerMove(outputKnob, { clientY: 100, pointerId: 1 });

    const lastEmit = fake.emitted.at(-1);
    expect(lastEmit?.id).toBe("outputGain");
    expect(lastEmit?.data).toMatchObject({ type: "valueChanged" });
    expect((lastEmit!.data as { value: number }).value).toBeGreaterThan(0.75);

    // Backend -> JS direction: a native-originated value change updates the same knob.
    act(() => {
      fake.trigger("outputGain", { type: "valueChanged", value: 0.2 });
    });
    expect(outputKnob).toHaveAttribute("aria-valuenow", "0.2");
  });
});
