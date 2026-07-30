import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import App from "./App";

describe("App", () => {
  it("renders the top bar, stage, and bottom bar without crashing", () => {
    render(<App />);

    // TopBar: I/O labels + chain rail glyph titles + preset row.
    expect(screen.getAllByText("Output").length).toBeGreaterThan(0);
    expect(screen.getByText("Input")).toBeInTheDocument();
    expect(screen.getByText(/Blues Breakup 02/)).toBeInTheDocument();

    // Stage: the M0 demo Knob, labeled OUTPUT, wired to the bridge.
    const sliders = screen.getAllByRole("slider", { name: "Output" });
    expect(sliders.length).toBe(2); // TopBar's 34px knob + Stage's 74px demo knob

    // BotBar: static labels.
    expect(screen.getByText("Powered by NAM")).toBeInTheDocument();
    expect(screen.getByText("Settings")).toBeInTheDocument();

    // No value text at rest anywhere.
    expect(screen.queryByRole("status")).not.toBeInTheDocument();
  });
});
