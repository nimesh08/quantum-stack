# Feedforward capability checks

A gate controlled by a measured bit requires device-side classical branching.
The compiler preserves the condition and checks the recorded target snapshot;
submission checks the current target capabilities again.

New Boolean/UInt operations require explicit `classical.*` feature support,
allowed integer widths, and supported output features. A legacy feedforward
flag alone does not establish those capabilities. Unknown or unsupported
requirements receive a diagnostic instead of an assumed provider capability.

Targets and serializers have different supported subsets. Provider identity
alone does not establish support for every processor or access route. Refresh
the concrete device snapshot and recompile when capabilities change.

There is no automatic postselection fallback. See
[postselection policy](post_selection.md) and the
[finite controller language](../../../../../language/controller.md).
