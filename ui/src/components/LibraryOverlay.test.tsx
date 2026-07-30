import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import type { LoadResult } from "../bridge";
import { LibraryOverlay } from "./LibraryOverlay";

const MODELS = [
  { name: "Tweed Deluxe", path: "Documents\\Simple Guitar\\Models\\Tweed Deluxe.nam" },
  { name: "Plexi 800", path: "Documents\\Simple Guitar\\Models\\Plexi 800.nam" },
];

describe("LibraryOverlay", () => {
  it("calls onOpen once on mount (so the caller can requestRigState)", () => {
    const onOpen = vi.fn();
    render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={vi.fn()} onClose={vi.fn()} onOpen={onOpen} />);
    expect(onOpen).toHaveBeenCalledTimes(1);
  });

  it("shows the exact empty-library copy when there's nothing to pick", () => {
    render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="no models found — drop .nam files in Documents\Simple Guitar\Models" library={[]} loadResult={null} onSelect={vi.fn()} onClose={vi.fn()} />);
    expect(screen.getByText("no models found — drop .nam files in Documents\\Simple Guitar\\Models")).toBeInTheDocument();
  });

  it("lists each library entry by name only", () => {
    render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={vi.fn()} onClose={vi.fn()} />);
    expect(screen.getByText("Tweed Deluxe")).toBeInTheDocument();
    expect(screen.getByText("Plexi 800")).toBeInTheDocument();
  });

  it("selecting a row calls onSelect with that entry's exact library path", () => {
    const onSelect = vi.fn();
    render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={onSelect} onClose={vi.fn()} />);
    fireEvent.click(screen.getByText("Plexi 800"));
    expect(onSelect).toHaveBeenCalledWith("Documents\\Simple Guitar\\Models\\Plexi 800.nam");
  });

  it("closes once the matching loadResult reports success", () => {
    const onClose = vi.fn();
    const { rerender } = render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={vi.fn()} onClose={onClose} />);
    fireEvent.click(screen.getByText("Tweed Deluxe"));
    expect(onClose).not.toHaveBeenCalled();

    const ok: LoadResult = { type: "loadResult", kind: "nam", ok: true, message: "loaded Tweed Deluxe" };
    rerender(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={ok} onSelect={vi.fn()} onClose={onClose} />);
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it("renders the message inline and stays open when the matching loadResult reports failure", () => {
    const onClose = vi.fn();
    const { rerender } = render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={vi.fn()} onClose={onClose} />);
    fireEvent.click(screen.getByText("Tweed Deluxe"));

    const failed: LoadResult = { type: "loadResult", kind: "nam", ok: false, message: "model failed to parse" };
    rerender(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={failed} onSelect={vi.fn()} onClose={onClose} />);

    expect(screen.getByText("model failed to parse")).toBeInTheDocument();
    expect(onClose).not.toHaveBeenCalled();
  });

  it("ignores a loadResult of the other kind", () => {
    const onClose = vi.fn();
    const { rerender } = render(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={null} onSelect={vi.fn()} onClose={onClose} />);
    fireEvent.click(screen.getByText("Tweed Deluxe"));

    const irResult: LoadResult = { type: "loadResult", kind: "ir", ok: true, message: "loaded some ir" };
    rerender(<LibraryOverlay kind="nam" title="Change model" emptyCopy="empty" library={MODELS} loadResult={irResult} onSelect={vi.fn()} onClose={onClose} />);

    expect(onClose).not.toHaveBeenCalled();
  });

  it("Esc closes the overlay", () => {
    const onClose = vi.fn();
    render(<LibraryOverlay kind="ir" title="Change IR" emptyCopy="empty" library={[]} loadResult={null} onSelect={vi.fn()} onClose={onClose} />);
    fireEvent.keyDown(window, { key: "Escape" });
    expect(onClose).toHaveBeenCalledTimes(1);
  });
});
