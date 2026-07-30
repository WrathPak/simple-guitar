import { act, fireEvent, render, screen, within } from "@testing-library/react";
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

function triggerPresetsState(
  fake: ReturnType<typeof createFakeJuceBackend>,
  overrides: Partial<{ current: { name: string; path: string } | null; dirty: boolean; presets: { name: string; path: string }[] }>,
) {
  act(() => {
    fake.trigger("presetsState", {
      type: "presetsState",
      schemaVersion: 5,
      current: null,
      dirty: false,
      presets: [],
      ...overrides,
    });
  });
}

describe("Preset pill", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("shows the dimmed empty-state name when there's no current preset", () => {
    renderWithFakeBackend();
    expect(screen.getByText("no preset")).toBeInTheDocument();
    expect(document.querySelector(".chrome-pill-dot")).not.toBeInTheDocument();
  });

  it("renders the current preset's name, and the dot only while dirty", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { current: { name: "Lead Tone", path: "C:\\Presets\\Lead Tone.sgpreset" }, dirty: false });

    expect(screen.getByText("Lead Tone")).toBeInTheDocument();
    expect(document.querySelector(".chrome-pill-dot")).not.toBeInTheDocument();

    triggerPresetsState(fake, { current: { name: "Lead Tone", path: "C:\\Presets\\Lead Tone.sgpreset" }, dirty: true });
    expect(document.querySelector(".chrome-pill-dot")).toBeInTheDocument();
  });

  it("‹ › are disabled with no presets, and send prevPreset/nextPreset once presets exist", () => {
    const fake = renderWithFakeBackend();
    const prevBtn = screen.getByRole("button", { name: "Previous preset" });
    const nextBtn = screen.getByRole("button", { name: "Next preset" });
    expect(prevBtn).toBeDisabled();
    expect(nextBtn).toBeDisabled();

    triggerPresetsState(fake, { presets: [{ name: "A", path: "C:\\A.sgpreset" }] });
    expect(prevBtn).not.toBeDisabled();
    expect(nextBtn).not.toBeDisabled();

    fireEvent.click(nextBtn);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "nextPreset", data: { type: "nextPreset" } });

    fireEvent.click(prevBtn);
    expect(fake.emitted.at(-1)).toMatchObject({ id: "prevPreset", data: { type: "prevPreset" } });
  });

  it("the small save action saves the current preset directly when one exists", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { current: { name: "Lead", path: "C:\\Lead.sgpreset" }, dirty: true, presets: [{ name: "Lead", path: "C:\\Lead.sgpreset" }] });

    fireEvent.click(screen.getByRole("button", { name: "Save preset" }));
    expect(fake.emitted.at(-1)).toMatchObject({ id: "saveCurrentPreset", data: { type: "saveCurrentPreset" } });
  });

  it("the small save action opens save-as when there's no current preset", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "Save preset" }));
    expect(screen.getByRole("dialog", { name: "Save as" })).toBeInTheDocument();
  });
});

describe("Presets overlay", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("opens from clicking the pill name and sends requestPresets", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByText("no preset"));
    expect(fake.emitted.at(-1)).toMatchObject({ id: "requestPresets", data: { type: "requestPresets" } });
    expect(screen.getByRole("dialog", { name: "Presets" })).toBeInTheDocument();
  });

  it("also opens from the ⋯ menu's Presets item", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Presets" }));
    expect(screen.getByRole("dialog", { name: "Presets" })).toBeInTheDocument();
  });

  it("shows the plain empty-state copy when there are no presets", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Presets" }));
    expect(screen.getByText("no presets yet — save one")).toBeInTheDocument();
  });

  it("selecting a row sends loadPreset with its exact path", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { presets: [{ name: "Lead", path: "C:\\Presets\\Lead.sgpreset" }] });

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Presets" }));
    fireEvent.click(screen.getByText("Lead"));

    expect(fake.emitted.at(-1)).toMatchObject({ id: "loadPreset", data: { type: "loadPreset", path: "C:\\Presets\\Lead.sgpreset" } });
  });

  it("marks the current preset's row quietly, and not the others", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, {
      current: { name: "Lead", path: "C:\\Lead.sgpreset" },
      presets: [
        { name: "Lead", path: "C:\\Lead.sgpreset" },
        { name: "Rhythm", path: "C:\\Rhythm.sgpreset" },
      ],
    });

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Presets" }));

    const dialog = screen.getByRole("dialog", { name: "Presets" });
    const leadRow = within(dialog).getByText("Lead").closest("button")!;
    const rhythmRow = within(dialog).getByText("Rhythm").closest("button")!;
    expect(within(leadRow).getByText("current")).toBeInTheDocument();
    expect(within(rhythmRow).queryByText("current")).not.toBeInTheDocument();
  });

  it("shows the engine's failure message inline when a load fails", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { presets: [{ name: "Lead", path: "C:\\Presets\\Lead.sgpreset" }] });

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Presets" }));
    fireEvent.click(screen.getByText("Lead"));

    act(() => {
      fake.trigger("presetResult", { type: "presetResult", ok: false, message: "couldn't read preset (missing, corrupt, or wrong format)" });
    });

    expect(screen.getByText("couldn't read preset (missing, corrupt, or wrong format)")).toBeInTheDocument();
    expect(screen.getByRole("dialog", { name: "Presets" })).toBeInTheDocument();
  });
});

describe("Save-as overlay", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("prefills the input with the current preset's name", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { current: { name: "Lead", path: "C:\\Lead.sgpreset" } });

    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Save as" }));

    expect(screen.getByRole("textbox", { name: "Preset name" })).toHaveValue("Lead");
  });

  it("Enter sends savePresetAs with the trimmed name, and closes once the engine confirms success", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Save as" }));

    const input = screen.getByRole("textbox", { name: "Preset name" });
    fireEvent.change(input, { target: { value: "  My Lead Tone  " } });
    fireEvent.keyDown(input, { key: "Enter" });

    expect(fake.emitted.at(-1)).toMatchObject({ id: "savePresetAs", data: { type: "savePresetAs", name: "My Lead Tone" } });
    expect(screen.getByRole("dialog", { name: "Save as" })).toBeInTheDocument(); // still open, waiting on presetResult

    act(() => {
      fake.trigger("presetResult", { type: "presetResult", ok: true, message: "saved" });
    });
    expect(screen.queryByRole("dialog", { name: "Save as" })).not.toBeInTheDocument();
  });

  it("shows the engine's failure message inline and stays open", () => {
    const fake = renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Save as" }));

    const input = screen.getByRole("textbox", { name: "Preset name" });
    fireEvent.change(input, { target: { value: "***" } });
    fireEvent.keyDown(input, { key: "Enter" });

    act(() => {
      fake.trigger("presetResult", { type: "presetResult", ok: false, message: "name required" });
    });

    expect(screen.getByText("name required")).toBeInTheDocument();
    expect(screen.getByRole("dialog", { name: "Save as" })).toBeInTheDocument();
  });

  it("Esc cancels without sending anything", () => {
    renderWithFakeBackend();
    fireEvent.click(screen.getByRole("button", { name: "⋯" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Save as" }));
    expect(screen.getByRole("dialog", { name: "Save as" })).toBeInTheDocument();

    fireEvent.keyDown(window, { key: "Escape" });
    expect(screen.queryByRole("dialog", { name: "Save as" })).not.toBeInTheDocument();
  });
});

describe("ctrl/cmd+S", () => {
  afterEach(() => {
    delete window.__JUCE__;
  });

  it("saves the current preset directly when one exists", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { current: { name: "Lead", path: "C:\\Lead.sgpreset" }, dirty: true, presets: [{ name: "Lead", path: "C:\\Lead.sgpreset" }] });

    fireEvent.keyDown(window, { key: "s", ctrlKey: true });
    expect(fake.emitted.at(-1)).toMatchObject({ id: "saveCurrentPreset", data: { type: "saveCurrentPreset" } });
  });

  it("opens the save-as overlay when there's no current preset", () => {
    renderWithFakeBackend();
    fireEvent.keyDown(window, { key: "s", ctrlKey: true });
    expect(screen.getByRole("dialog", { name: "Save as" })).toBeInTheDocument();
  });

  it("also routes via the meta key (macOS cmd+S)", () => {
    const fake = renderWithFakeBackend();
    triggerPresetsState(fake, { current: { name: "Lead", path: "C:\\Lead.sgpreset" } });

    fireEvent.keyDown(window, { key: "s", metaKey: true });
    expect(fake.emitted.at(-1)).toMatchObject({ id: "saveCurrentPreset" });
  });
});
