import { describe, expect, it } from "vitest";
import { spinorLanguage } from "../../src/languages/spinor";
import { phononLanguage } from "../../src/languages/phonon";
import { photonLanguage } from "../../src/languages/photon";

describe("language definitions", () => {
  it("spinor declares the standard gates", () => {
    const gates = (spinorLanguage as any).gates as string[];
    expect(gates).toContain("h");
    expect(gates).toContain("cx");
    expect(gates).toContain("ecr");
    expect(gates).toContain("rzz");
  });

  it("phonon includes control flow keywords", () => {
    const keywords = (phononLanguage as any).keywords as string[];
    expect(keywords).toContain("if");
    expect(keywords).toContain("for");
    expect(keywords).toContain("def");
  });

  it("photon includes kernel and QReg", () => {
    const keywords = (photonLanguage as any).keywords as string[];
    expect(keywords).toContain("kernel");
    expect(keywords).toContain("QReg");
  });
});
