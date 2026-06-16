import "@testing-library/jest-dom";

// jsdom lacks ResizeObserver; recharts uses it.
class ResizeObserverStub {
  observe(): void {}
  unobserve(): void {}
  disconnect(): void {}
}
(globalThis as any).ResizeObserver = ResizeObserverStub;
