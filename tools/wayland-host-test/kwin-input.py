#!/usr/bin/env python3
"""Bounded compositor input for an explicitly selected, task-owned virtual KWin.

Requires python-dbus and libei. No default session bus or Wayland display is used.
Without --drag-title, negotiate a pointer device but do not send input.
"""

import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import time

import dbus


POINTER_ABSOLUTE = 1 << 1
BUTTON = 1 << 5
BTN_LEFT = 0x110


def peer_pid(fd):
    with socket.socket(fileno=os.dup(fd)) as connection:
        pid, uid, _ = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    if uid != os.getuid():
        raise RuntimeError("IPC peer belongs to another user")
    return pid


def validate_compositor(args, bus):
    root = Path(args.task_root).resolve(strict=True)
    display = Path(args.display).resolve(strict=True)
    if not display.is_relative_to(root):
        raise RuntimeError("Wayland socket is outside the explicit task directory")
    command = Path(f"/proc/{args.pid}/cmdline").read_bytes().split(b"\0")
    if b"--virtual" not in command or b"--socket" not in command:
        raise RuntimeError("Refusing input to a non-virtual compositor")
    if os.fsdecode(command[command.index(b"--socket") + 1]) != display.name:
        raise RuntimeError("Compositor command line names another display")
    daemon = dbus.Interface(bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus"), "org.freedesktop.DBus")
    if int(daemon.GetConnectionUnixProcessID("org.kde.KWin", timeout=3)) != args.pid:
        raise RuntimeError("D-Bus KWin owner does not match the explicit PID")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(3)
        connection.connect(str(display))
        if peer_pid(connection.fileno()) != args.pid:
            raise RuntimeError("Wayland and D-Bus endpoints belong to different compositors")


class Pointer:
    def __init__(self, fd):
        self.context = self.device = None
        self.resumed = self.started = self.pressed = False
        try:
            self.initialize_library()
            self.context = self.lib.ei_new_sender(None)
            if not self.context:
                raise MemoryError("Cannot allocate EI sender")
            self.lib.ei_configure_name(self.context, b"winewayland-host-isolated-test")
            transferred_fd, fd = fd, None
            if self.lib.ei_setup_backend_fd(self.context, transferred_fd) < 0:
                raise RuntimeError("Cannot initialize EI sender")
        except BaseException:
            self.close()
            raise
        finally:
            if fd is not None:
                os.close(fd)

    def initialize_library(self):
        self.lib = ct.CDLL("libei.so.1")
        pointer = ct.c_void_p
        signatures = {
            "ei_new_sender": (pointer, [pointer]),
            "ei_unref": (pointer, [pointer]),
            "ei_configure_name": (None, [pointer, ct.c_char_p]),
            "ei_setup_backend_fd": (ct.c_int, [pointer, ct.c_int]),
            "ei_get_fd": (ct.c_int, [pointer]),
            "ei_dispatch": (None, [pointer]),
            "ei_get_event": (pointer, [pointer]),
            "ei_event_unref": (pointer, [pointer]),
            "ei_event_get_type": (ct.c_int, [pointer]),
            "ei_event_type_to_string": (ct.c_char_p, [ct.c_int]),
            "ei_event_get_seat": (pointer, [pointer]),
            "ei_event_get_device": (pointer, [pointer]),
            "ei_seat_bind_capabilities": (None, [pointer]),
            "ei_device_ref": (pointer, [pointer]),
            "ei_device_unref": (pointer, [pointer]),
            "ei_device_has_capability": (ct.c_bool, [pointer, ct.c_int]),
            "ei_device_start_emulating": (None, [pointer, ct.c_uint32]),
            "ei_device_stop_emulating": (None, [pointer]),
            "ei_device_pointer_motion_absolute": (None, [pointer, ct.c_double, ct.c_double]),
            "ei_device_button_button": (None, [pointer, ct.c_uint32, ct.c_bool]),
            "ei_device_frame": (None, [pointer, ct.c_uint64]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.lib, name)
            function.restype, function.argtypes = result, arguments

    def pump(self, duration):
        deadline = time.monotonic() + duration
        while True:
            self.lib.ei_dispatch(self.context)
            while event := self.lib.ei_get_event(self.context):
                try:
                    kind = self.lib.ei_event_type_to_string(self.lib.ei_event_get_type(event)).decode()
                    if kind == "EI_EVENT_DISCONNECT":
                        raise RuntimeError("Compositor disconnected the EI sender")
                    if kind == "EI_EVENT_SEAT_ADDED":
                        self.lib.ei_seat_bind_capabilities(self.lib.ei_event_get_seat(event),
                                                         ct.c_int(POINTER_ABSOLUTE), ct.c_int(BUTTON), ct.c_void_p())
                    if kind == "EI_EVENT_DEVICE_RESUMED":
                        device = self.lib.ei_event_get_device(event)
                        if self.lib.ei_device_has_capability(device, POINTER_ABSOLUTE) and self.lib.ei_device_has_capability(device, BUTTON):
                            if self.device is None:
                                self.device = self.lib.ei_device_ref(device)
                            if self.device == device:
                                self.resumed = True
                    if kind in ("EI_EVENT_DEVICE_PAUSED", "EI_EVENT_DEVICE_REMOVED") and self.lib.ei_event_get_device(event) == self.device:
                        self.resumed = False
                finally:
                    self.lib.ei_event_unref(event)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            select.select([self.lib.ei_get_fd(self.context)], [], [], min(remaining, 0.05))

    def frame(self):
        self.lib.ei_device_frame(self.device, time.monotonic_ns() // 1000)

    def move(self, x, y):
        if not self.resumed:
            raise RuntimeError("Pointer is not resumed")
        self.lib.ei_device_pointer_motion_absolute(self.device, x, y)
        self.frame()

    def button(self, pressed):
        if not self.resumed:
            raise RuntimeError("Pointer is not resumed")
        self.lib.ei_device_button_button(self.device, BTN_LEFT, pressed)
        self.pressed = pressed
        self.frame()

    def close(self):
        if self.device:
            if self.pressed and self.resumed:
                self.button(False)
            if self.started:
                self.lib.ei_device_stop_emulating(self.device)
            self.lib.ei_device_unref(self.device)
            self.device = None
        if self.context:
            self.lib.ei_unref(self.context)
            self.context = None


def find_window(bus, title):
    runner = dbus.Interface(bus.get_object("org.kde.KWin", "/WindowsRunner"), "org.kde.krunner1")
    kwin = dbus.Interface(bus.get_object("org.kde.KWin", "/KWin"), "org.kde.KWin")
    deadline = time.monotonic() + 2
    while True:
        windows = {}
        for match in runner.Match(title, timeout=3):
            identifier = str(match[0]).split("_", 1)[1]
            info = kwin.getWindowInfo(identifier, timeout=3)
            if str(info.get("caption")) == title and str(info.get("resourceClass")) == "winewayland-host":
                windows[identifier] = info
        if windows or time.monotonic() >= deadline:
            break
        time.sleep(0.05)
    if len(windows) != 1:
        raise RuntimeError(f"Expected one hosted fixture window, found {len(windows)}")
    return kwin, next(iter(windows.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--task-root", required=True)
    parser.add_argument("--display", required=True, help="Absolute task-owned Wayland socket")
    parser.add_argument("--bus", required=True, help="Private compositor session bus address")
    parser.add_argument("--pid", required=True, type=int, help="Expected virtual KWin PID")
    parser.add_argument("--drag-title", help="Exact caption of the hosted Wine test window")
    parser.add_argument("--resize", action="store_true", help="Drag the bottom-right frame corner instead of the title")
    parser.add_argument("--width", type=int, help="Wine fixture's whole-window width at scale 1")
    parser.add_argument("--height", type=int, help="Wine fixture's whole-window height at scale 1")
    args = parser.parse_args()
    if args.resize and not args.drag_title:
        parser.error("Resizing requires an explicit fixture title")
    if args.drag_title and not (args.width and args.height and 64 <= args.width <= 4096 and 48 <= args.height <= 4096):
        parser.error("Dragging requires the fixture's bounded width and height")
    def terminate(signum, _frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    bus = dbus.bus.BusConnection(args.bus)
    pointer = None
    fd = None
    try:
        validate_compositor(args, bus)
        remote = dbus.Interface(bus.get_object("org.kde.KWin", "/org/kde/KWin/EIS/RemoteDesktop"), "org.kde.KWin.EIS.RemoteDesktop")
        descriptor, _cookie = remote.connectToEIS(dbus.Int32(2), timeout=3)
        fd = descriptor.take()
        if peer_pid(fd) != args.pid:
            raise RuntimeError("EI connection belongs to another compositor")
        transferred_fd, fd = fd, None
        pointer = Pointer(transferred_fd)
        deadline = time.monotonic() + 3
        while not pointer.resumed and time.monotonic() < deadline:
            pointer.pump(0.05)
        if not pointer.resumed:
            raise RuntimeError("No resumed absolute pointer within three seconds")
        print(json.dumps({"ei_pointer": "ready", "compositor_pid": args.pid}), flush=True)
        if args.drag_title:
            validate_compositor(args, bus)
            kwin, (identifier, before) = find_window(bus, args.drag_title)
            print(json.dumps({"fixture": {key: before.get(key) for key in
                  ("caption", "x", "y", "width", "height", "noBorder", "minimized", "fullscreen")}}), flush=True)
            # KWin's noBorder can describe policy rather than actual decoration.
            # At scale 1 an extra native frame instead changes these extents.
            if (before.get("minimized") or before.get("fullscreen") or
                    float(before["width"]) != args.width or float(before["height"]) != args.height):
                raise RuntimeError("Native frame extents do not match the visible Wine fixture")
            x = float(before["x"]) + (float(before["width"]) - 2 if args.resize else float(before["width"]) / 2)
            y = float(before["y"]) + (float(before["height"]) - 2 if args.resize else 12)
            pointer.lib.ei_device_start_emulating(pointer.device, 1)
            pointer.started = True
            pointer.move(x, y)
            pointer.pump(0.2)
            pointer.button(True)
            pointer.pump(0.3)
            for step in range(1, 5):
                pointer.move(x + step * 15, y + step * 10)
                pointer.pump(0.1)
            pointer.button(False)
            pointer.pump(0.5)
            deadline = time.monotonic() + 3
            while True:
                after = kwin.getWindowInfo(identifier, timeout=3)
                delta = [float(after[axis]) - float(before[axis]) for axis in
                         (("width", "height") if args.resize else ("x", "y"))]
                if (delta[0] >= 59 and delta[1] >= 39) or time.monotonic() >= deadline:
                    break
                pointer.pump(0.05)
            print(json.dumps({"native_resize_delta" if args.resize else "native_move_delta": delta,
                              "window": identifier}), flush=True)
            if delta[0] < 59 or delta[1] < 39:
                raise RuntimeError("Compositor did not resize the fixture window" if args.resize else
                                   "Compositor did not move the fixture window")
    finally:
        if pointer:
            pointer.close()
        if fd is not None:
            os.close(fd)
        bus.close()


if __name__ == "__main__":
    main()
