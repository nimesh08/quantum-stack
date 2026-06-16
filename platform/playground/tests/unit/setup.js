import "@testing-library/jest-dom";
// jsdom lacks ResizeObserver; recharts uses it.
class ResizeObserverStub {
    observe() { }
    unobserve() { }
    disconnect() { }
}
globalThis.ResizeObserver = ResizeObserverStub;
