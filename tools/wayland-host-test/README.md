# Isolated compositor input

`kwin-input.py` uses KWin's EIS interface and libei to deliver pointer events
through the compositor. It requires `python-dbus` and `libei.so.1` on the test
machine. It does not connect to the default session bus or desktop.

Use an existing task-owned `kwin_wayland --virtual` instance with a scale-one
output and an explicitly verified PID, Wayland socket and private D-Bus bus.
The helper verifies all three IPC peers against that PID and checks that the
socket is inside the task directory. Without `--drag-title` it only negotiates
an input device and sends no input.

In a disposable prefix with a coherent experimental host runner, enable the
per-application developer setting for `winewayland-host.exe` as described in
`documentation/dcomp-wayland-host-progress.md`. Run one of these under a
30-second outer timeout, with the private Wayland display and no
`WINEWAYLAND_HOST_TEST_INPUT`:

```
wine winewayland-host.exe --dcomp-interactive-test
wine winewayland-host.exe --dcomp-interactive-resize-test
```

After `dcomp_interactive=ready`, pass the reported whole-window width and
height to the helper. Replace the placeholders with verified task values:

```
python3 kwin-input.py --task-root TASK_DIRECTORY --display ABSOLUTE_SOCKET \
    --bus PRIVATE_BUS_ADDRESS --pid KWIN_PID \
    --drag-title 'DComp host interactive' --width WIDTH --height HEIGHT
```

Add `--resize` for the resize fixture. Use a ten-second outer timeout for the
helper. It finds exactly one matching hosted window, checks its extents,
sends one drag of 60 by 40 pixels and checks the same native window afterward.
The helper pairs its press with a release, including on ordinary failures,
and disconnects its emulated device when finished. It does not provide a
physical-hardware input test.

Require both processes to exit successfully. The Wine fixture checks action
issuance; resizing also checks one enter/exit pair, larger client buffers and
new backend presentation. Both fixtures check that software frame snapshots
stop growing during the last idle second. A native shell request by itself
does not prove acceptance, final Windows position or scanout.

Restore the test opt-in and stop only the explicitly disposable test prefix's
Wine processes in the caller's exit trap, using that runner's `wineserver -k`
and a bounded `wineserver -w`. Retain logs and binary hashes. Do not stop the
private compositor, any shared Office environment or unrelated Wine prefixes.
