import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import App from "./App";

describe("App", () => {
  it("renders the whole rig in one scene with no page scroll", () => {
    const { container } = render(<App />);

    // Chrome: wordmark + preset pill.
    expect(screen.getByText("Simple Guitar")).toBeInTheDocument();
    expect(screen.getByText("Blues Breakup 02")).toBeInTheDocument();

    // Backline: the amp is present (decorative, wide shot).
    expect(screen.getByRole("button", { name: "Focus amplifier" })).toBeInTheDocument();

    // Pedal row: the three demo pedals, in signal order.
    expect(screen.getByRole("button", { name: "Focus screamer" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Focus echoes" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Focus chamber" })).toBeInTheDocument();

    // No value text at rest anywhere (tooltips only appear during interaction).
    expect(screen.queryByRole("status")).not.toBeInTheDocument();

    // Wide shot only: nothing is focused yet, so no faceplate/panel is centered.
    expect(screen.queryByRole("slider")).not.toBeInTheDocument();

    // The app never scrolls at any window size — everything is laid out to fill it.
    const app = container.querySelector(".app");
    const room = container.querySelector(".room");
    expect(app).not.toBeNull();
    expect(room).not.toBeNull();
    expect(getComputedStyle(app!).overflow).toBe("hidden");
    expect(getComputedStyle(room!).overflow).toBe("hidden");
  });
});
