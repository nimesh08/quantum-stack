import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import Histogram from "../../src/components/Histogram";

describe("<Histogram />", () => {
  it("renders the chart container", () => {
    render(<Histogram counts={{ "00": 498, "11": 502 }} shots={1000} />);
    expect(screen.getByTestId("histogram")).toBeInTheDocument();
  });

  it("handles an empty histogram", () => {
    render(<Histogram counts={{}} shots={0} />);
    expect(screen.getByTestId("histogram")).toBeInTheDocument();
  });
});
