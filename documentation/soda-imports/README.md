# Soda imports

This directory records changes imported from Bottles/Soda rather than independently reimplemented. The September 2026 import uses Soda commit `2958bf5ebeb2ae1f578413e6f0e9bf95516d9233` and Wine4Office base `abf6dffa4604101a5166511631b37d3d07a96019`. See [the manifest](20260930.json) for the original patch links and SHA-256 checksums.

| Import | Observable behavior | Regression |
| --- | --- | --- |
| Free-threaded marshaler | Exposes `IAgileObject` on the inner COM object | `ole32:marshal` |
| WinHTTP deferred request body | A NULL optional buffer does not get dereferenced; `WinHttpWriteData` supplies the body afterwards | `winhttp:winhttp` |
| WinHTTP certificate failure | Clears the request connection pointer before releasing the connection on failure | Source/lifetime review; no injected certificate-retrieval failure yet |
| WinINet absolute redirects | Retries URL combination when a required buffer length is returned | `wininet:http` |
| XMLLite UTF-16 reader | Consumes decoded bytes so successive input buffers do not repeat previous text | `xmllite:reader` |
| XMLLite namespace writer | Reuses the explicitly declared element prefix instead of emitting it twice | `xmllite:writer` |
| DirectWrite glyph copying | Clips glyph alpha-texture copies to the destination bitmap and adjusts source coordinates | Existing `dwrite:font` coverage; no Office-specific crash reproduction yet |

The original COM, HTTP and XML regressions are retained. The deferred-body server additionally handles partial TCP reads and failed `recv` calls safely. DirectWrite's current Soda patch clips actual bitmap bounds; it is different from the earlier `volatile` scalar-copy variant.

## Existing implementations retained

Soda's shape-shader cache is not imported over Wine4Office's precompiled shader resources in `dlls/d2d1/device.c`. Wine4Office already avoids runtime compilation of these shaders. Its Wine 11.17 base also already has the client-surface cache in `dlls/win32u/window.c`; the older Soda cache patch cannot be substituted mechanically. These are code comparisons, not measured startup-speed claims.

## Remaining work

This is the first source import batch, not the complete Soda Office integration. Classic rendering, Wayland and installer/runtime differences still need semantic review. Soda's `windows-networking` patch only exposes a `MessageWebSocket` activation factory: creating an instance still returns `E_NOTIMPL`. Factory discovery must not be presented as a working WebSocket implementation. DirectComposition remains deferred unless a necessary classic Office workflow needs it.

DXVK 3 packaging and default selection remain separate work. All DXVK builds and runtime tests must take place on `testing-laptop`. Software Vulkan in WSL does not establish Intel GPU acceleration under Wine. No independent DXVK source patch is included in this import.
