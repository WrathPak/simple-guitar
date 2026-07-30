import { describe, expect, it } from "vitest";
import {
  AMP_DB_RANGE,
  CAB_HIGH_CUT_RANGE,
  CAB_LOW_CUT_RANGE,
  GATE_THRESHOLD_RANGE,
  OUTPUT_GAIN_RANGE,
  boolParamValue,
  dbToNormalized,
  defaultNormalizedForLogHz,
  formatDb,
  formatHz,
  formatLogHz,
  isParamOn,
  logHzToNormalized,
  normalizedToDb,
  normalizedToLogHz,
} from "./paramMath";

describe("dB linear mapping", () => {
  it("maps the extremes of each range", () => {
    expect(normalizedToDb(0, GATE_THRESHOLD_RANGE)).toBeCloseTo(-90, 10);
    expect(normalizedToDb(1, GATE_THRESHOLD_RANGE)).toBeCloseTo(-20, 10);
    expect(normalizedToDb(0, AMP_DB_RANGE)).toBeCloseTo(-12, 10);
    expect(normalizedToDb(1, AMP_DB_RANGE)).toBeCloseTo(12, 10);
    expect(normalizedToDb(0, OUTPUT_GAIN_RANGE)).toBeCloseTo(-60, 10);
    expect(normalizedToDb(1, OUTPUT_GAIN_RANGE)).toBeCloseTo(12, 10);
  });

  it("maps the midpoint of the symmetric amp range to 0dB", () => {
    expect(normalizedToDb(0.5, AMP_DB_RANGE)).toBeCloseTo(0, 10);
  });

  it("dbToNormalized is the inverse of normalizedToDb", () => {
    for (const range of [GATE_THRESHOLD_RANGE, AMP_DB_RANGE, OUTPUT_GAIN_RANGE]) {
      for (const value of [0, 0.2, 0.5, 0.75, 1]) {
        const db = normalizedToDb(value, range);
        expect(dbToNormalized(db, range)).toBeCloseTo(value, 10);
      }
    }
  });

  it("clamps dbToNormalized outside the range", () => {
    expect(dbToNormalized(-1000, AMP_DB_RANGE)).toBe(0);
    expect(dbToNormalized(1000, AMP_DB_RANGE)).toBe(1);
  });

  it("formats with an explicit sign and one decimal", () => {
    expect(formatDb(0.5, AMP_DB_RANGE)).toBe("+0.0 dB");
    expect(formatDb(0, AMP_DB_RANGE)).toBe("-12.0 dB");
    expect(formatDb(1, AMP_DB_RANGE)).toBe("+12.0 dB");
  });
});

describe("cab cutoff log mapping", () => {
  it("maps the extremes to the range's start/end Hz", () => {
    expect(normalizedToLogHz(0, CAB_LOW_CUT_RANGE)).toBeCloseTo(20, 8);
    expect(normalizedToLogHz(1, CAB_LOW_CUT_RANGE)).toBeCloseTo(400, 8);
    expect(normalizedToLogHz(0, CAB_HIGH_CUT_RANGE)).toBeCloseTo(2000, 6);
    expect(normalizedToLogHz(1, CAB_HIGH_CUT_RANGE)).toBeCloseTo(20000, 6);
  });

  it("is a geometric (log) curve, not linear -- the midpoint is the geometric mean", () => {
    const mid = normalizedToLogHz(0.5, CAB_LOW_CUT_RANGE);
    expect(mid).toBeCloseTo(Math.sqrt(20 * 400), 6);
    expect(mid).not.toBeCloseTo((20 + 400) / 2, 0);
  });

  it("logHzToNormalized is the inverse of normalizedToLogHz", () => {
    for (const range of [CAB_LOW_CUT_RANGE, CAB_HIGH_CUT_RANGE]) {
      for (const value of [0, 0.1, 0.37, 0.6, 0.9, 1]) {
        const hz = normalizedToLogHz(value, range);
        expect(logHzToNormalized(hz, range)).toBeCloseTo(value, 8);
      }
    }
  });

  it("clamps Hz outside the range before inverting", () => {
    expect(logHzToNormalized(1, CAB_LOW_CUT_RANGE)).toBe(0);
    expect(logHzToNormalized(1_000_000, CAB_LOW_CUT_RANGE)).toBe(1);
  });

  it("the default normalized position reproduces the range's defaultHz", () => {
    expect(normalizedToLogHz(defaultNormalizedForLogHz(CAB_LOW_CUT_RANGE), CAB_LOW_CUT_RANGE)).toBeCloseTo(60, 6);
    expect(normalizedToLogHz(defaultNormalizedForLogHz(CAB_HIGH_CUT_RANGE), CAB_HIGH_CUT_RANGE)).toBeCloseTo(8000, 4);
  });
});

describe("Hz/kHz formatting", () => {
  it("formats sub-kHz values as whole Hz", () => {
    expect(formatHz(20)).toBe("20 Hz");
    expect(formatHz(60)).toBe("60 Hz");
    expect(formatHz(400)).toBe("400 Hz");
    expect(formatHz(999)).toBe("999 Hz");
  });

  it("formats kHz-and-above values with one decimal", () => {
    expect(formatHz(1000)).toBe("1.0 kHz");
    expect(formatHz(8000)).toBe("8.0 kHz");
    expect(formatHz(20000)).toBe("20.0 kHz");
  });

  it("formatLogHz composes the mapping and the formatter", () => {
    expect(formatLogHz(0, CAB_LOW_CUT_RANGE)).toBe("20 Hz");
    expect(formatLogHz(1, CAB_HIGH_CUT_RANGE)).toBe("20.0 kHz");
  });
});

describe("bool param convention", () => {
  it("reads >=0.5 as on", () => {
    expect(isParamOn(0)).toBe(false);
    expect(isParamOn(0.49)).toBe(false);
    expect(isParamOn(0.5)).toBe(true);
    expect(isParamOn(1)).toBe(true);
  });

  it("round-trips through boolParamValue", () => {
    expect(boolParamValue(true)).toBe(1);
    expect(boolParamValue(false)).toBe(0);
    expect(isParamOn(boolParamValue(true))).toBe(true);
    expect(isParamOn(boolParamValue(false))).toBe(false);
  });
});
