import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { Knob } from "./Knob";

describe("Knob", () => {
  it("renders the micro-label but no value text at rest", () => {
    render(<Knob label="Output" defaultValue={0.5} />);
    expect(screen.getByText("Output")).toBeInTheDocument();
    expect(screen.queryByRole("status")).not.toBeInTheDocument();
  });

  it("shows a value tooltip only while hovering", () => {
    render(<Knob label="Output" defaultValue={0.5} format={(v) => `${v}`} />);
    const cap = screen.getByRole("slider");
    expect(screen.queryByRole("status")).not.toBeInTheDocument();

    fireEvent.mouseEnter(cap);
    expect(screen.getByRole("status")).toBeInTheDocument();

    fireEvent.mouseLeave(cap);
    expect(screen.queryByRole("status")).not.toBeInTheDocument();
  });

  it("calls onChange while dragging, increasing value when dragging up", () => {
    const onChange = vi.fn();
    render(<Knob label="Output" value={0.5} onChange={onChange} />);
    const cap = screen.getByRole("slider");

    fireEvent.pointerDown(cap, { clientY: 200, pointerId: 1, button: 0 });
    fireEvent.pointerMove(cap, { clientY: 100, pointerId: 1 });

    expect(onChange).toHaveBeenCalled();
    const lastValue = onChange.mock.calls.at(-1)![0];
    expect(lastValue).toBeGreaterThan(0.5);
  });

  it("moves less per pixel while dragging with shift held (fine mode)", () => {
    const normalChanges: number[] = [];
    const fineChanges: number[] = [];

    const { unmount } = render(<Knob label="A" value={0.5} onChange={(v) => normalChanges.push(v)} />);
    let cap = screen.getByRole("slider");
    fireEvent.pointerDown(cap, { clientY: 200, pointerId: 1, button: 0 });
    fireEvent.pointerMove(cap, { clientY: 150, pointerId: 1 });
    unmount();

    render(<Knob label="B" value={0.5} onChange={(v) => fineChanges.push(v)} />);
    cap = screen.getByRole("slider");
    fireEvent.pointerDown(cap, { clientY: 200, pointerId: 1, button: 0, shiftKey: true });
    fireEvent.pointerMove(cap, { clientY: 150, pointerId: 1, shiftKey: true });

    const normalDelta = normalChanges.at(-1)! - 0.5;
    const fineDelta = fineChanges.at(-1)! - 0.5;
    expect(fineDelta).toBeGreaterThan(0);
    expect(fineDelta).toBeLessThan(normalDelta);
  });

  it("resets to the default value on double-click", () => {
    const onChange = vi.fn();
    render(<Knob label="Output" value={0.9} defaultValue={0.3} onChange={onChange} />);
    fireEvent.doubleClick(screen.getByRole("slider"));
    expect(onChange).toHaveBeenCalledWith(0.3);
  });

  it("is focusable and arrow-key adjustable", () => {
    const onChange = vi.fn();
    render(<Knob label="Output" value={0.5} onChange={onChange} />);
    const cap = screen.getByRole("slider");
    cap.focus();
    expect(cap).toHaveFocus();

    fireEvent.keyDown(cap, { key: "ArrowUp" });
    expect(onChange).toHaveBeenCalled();
    expect(onChange.mock.calls.at(-1)![0]).toBeGreaterThan(0.5);
  });
});
