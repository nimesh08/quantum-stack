# No automatic postselection fallback

The compiler does not replace unsupported feedforward with unconditional gates
or filter measurement outcomes to imitate the requested branch. Those changes
would alter the program's quantum instrument and its shot statistics.

If a target cannot execute a required branch or classical operation, compilation
or submission fails with a capability diagnostic. Select a compatible target or
change the program explicitly. A legacy feedforward flag does not authorize the
new finite controller arithmetic operations.

Loop exhaustion also preserves every shot; it is reported separately as an
application status. No extra shots are silently run to hide exhaustion.

See [feedforward capabilities](feedforward_legalisation.md) and the
[finite controller language](../../../../../language/controller.md).
