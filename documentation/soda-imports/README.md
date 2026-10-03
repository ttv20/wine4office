# Soda imports

This directory records changes imported from Bottles/Soda rather than independently reimplemented. Thanks to [Mirko Brombin](https://github.com/mirkobrombin) and the [Bottles/Soda contributors](https://github.com/bottlesdevs/wine).

## October 2026 (batch 2)

Batch 2 reviews the Soda commits after `2958bf5ebeb2ae1f578413e6f0e9bf95516d9233` up to `6a584a70a2d72ee8afabb840592c80fa712474e3`, on Wine4Office base `abf6dffa4604101a5166511631b37d3d07a96019`. See [the manifest](20261004.json) for the original patch links, introducing commits and SHA-256 checksums.

| Import | Observable behavior | Regression |
| --- | --- | --- |
| Display mode refresh | When the host screen resolution changes, the reported current mode follows it unless the application chose an emulated mode | Runtime check on Xvnc with emulated mode setting; no Wine unit test can change the host resolution |
| Empty scissor rectangles | Direct3D scissor rectangles with left > right or top > bottom draw nothing in the OpenGL and Vulkan renderers | `d3d11:d3d11` (`test_scissor`) |
| SharedModeSettings | `Windows.System.Profile.SharedModeSettings` activates and reports that Shared PC mode is off | `twinapi.appcore:twinapi` |

Not imported:

| Soda change | Reason |
| --- | --- |
| Repaint newly drawable window surfaces | Only affects windows made visible by changing their style. Wine skips the repaint there on purpose, as Windows does. Deferred until there is a Wine4Office reproduction. |
| Restore exposed client surfaces | OpenGL child content already stays in the window surface. Wine4Office's Wayland present callback is not safe to call from the expose path. |
| WAM web-token errors | Already covered: Wine4Office's own WAM code returns a provider error with a message for every failed token request. |
| CompositionGraphicsDevice2 and virtual drawing surfaces | Deferred: no Wine4Office log shows Office asking for it. Needs a reproduction and a port onto `dlls/windows.ui/compositor.c`. |
| CompositionEffectSourceParameter | Deferred: Office has not requested it in Wine4Office logs, and composition effect factories are not implemented yet. |

Soda's identity bridge, declarative WinRT factories and CI changes in this range were not reviewed for import.
