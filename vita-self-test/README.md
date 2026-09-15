# VitaCheat safe Vita self-test

This disposable title is the first Vita-facing milestone. It is an ordinary
user-mode application with title ID `VCHT00001`; it has no privileged or
kernel-module access and cannot inspect or modify another process.

Build it with VitaSDK:

```sh
make vita-self-test
```

The output is `build/vita-self-test/vitacheat-self-test.vpk`.

On the Vita, hold Select continuously for five seconds. The tested portable
activation state machine opens the menu exactly once and requires Select to be
released before it can trigger again. Press X in the menu to run six checks
against a fixed buffer owned by the app:

- exact U32 initial search;
- changed, increased, and unchanged refinements;
- decreased refinement using exact in-place candidate compaction; and
- bounded truncation with the full match count retained.

Circle navigates back or exits from the home screen. Triangle exits from the
menu or result screen. Start is intentionally left unbound so the Vita's
PS+Start screenshot shortcut cannot accidentally close the application. A
passing run is only evidence for the portable scanner, controller activation,
renderer, and launch/menu/result/exit path; it is not evidence of cross-process
access or cheat execution.

The recorded retail 3.65 run passed all six checks. See the
[hardware-gate record](hardware-result-retail-3.65.md) and retained screenshot.
