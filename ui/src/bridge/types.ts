/**
 * Shared types for the JUCE WebView bridge. See ./juceClient.ts for the
 * transport contract and ./index.ts for the public façade.
 */

/** A live handle to a single normalized (0..1) parameter on the C++ side. */
export interface SliderStateHandle {
  /** Current normalized value in [0, 1]. */
  readonly value: number;
  /** Push a new normalized value to the backend (e.g. from a drag/scroll/key gesture). */
  setValue(value: number): void;
  /** Bracket a user gesture (mirrors JUCE's sliderDragStarted/Ended semantics). */
  beginGesture(): void;
  endGesture(): void;
  /** Subscribe to value changes (both local and backend-originated). Returns an unsubscribe fn. */
  subscribe(listener: (value: number) => void): () => void;
}

/** One frame of the input/output meter data emitted by the C++ audio thread's UI timer. */
export interface MeterFrame {
  inPeakDb: number;
  outPeakDb: number;
}
