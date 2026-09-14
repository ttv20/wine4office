The Wine development release 11.17 is now available.

What's new in this release:
  - Bundled vkd3d upgraded to version 2.1.
  - Initial support for display mode emulation.
  - Better support for preferred UI languages.
  - Various bug fixes.

The source is available at <https://dl.winehq.org/wine/source/11.x/wine-11.17.tar.xz>

Binary packages for various distributions will be available
from the respective [download sites][1].

You will find documentation [here][2].

Wine is available thanks to the work of many people.
See the file [AUTHORS][3] for the complete list.

[1]: https://gitlab.winehq.org/wine/wine/-/wikis/Download
[2]: https://gitlab.winehq.org/wine/wine/-/wikis/Documentation
[3]: https://gitlab.winehq.org/wine/wine/-/raw/wine-11.17/AUTHORS

----------------------------------------------------------------

### Bugs fixed in 11.17 (total 44):

 - #23364  Tetris JR (win16) exits immediately
 - #25373  Nokia S60 5th Edition SDK 1.0: epoc32 emulator and other tools fail to run (Wine's 'hal.dll' is preferred over native, causing failure to load app provided library with same name)
 - #29851  Some part of PDFCreator installer cannot display Chinese correctly even Font Replacement is setting correctly
 - #30155  SafeDisc v2.05.030 fails due to driver dispatch routine status and irp.IoStatus.u.Status differing (Command & Conquer: Red Alert 2)
 - #32368  Bigrats: UI can't display normally and all the widgets didn't work
 - #35056  64-bit PTC Pro Engineer Wildfire V5 installer fails (64-bit processors not reported in volatile hardware registry keys)
 - #39336  VisualSubSync (ItaSA version) 1.0.2.1 crashes when pressing audio play button
 - #42383  certmgr.exe is unable to import certificate in PKCS12 format
 - #44137  Wine64 crashes with any 64-bit binary
 - #45979  Advanced SystemCare 6.4 needs 'SWbemPropertySet::get__NewEnum' to iterate over 'Win32_PhysicalMedia' properties
 - #46671  Biamp canvas crashes on startup
 - #46815  PS4 Remote Play 2.x (.NET 4.x app) fails on startup, reports 'Cannot connect to the server.' (missing TEMP directory in user's Local AppData directory)
 - #47062  Multiple E-Banking applications by KOBIL Systems GmbH crash on startup due to ntdll.NtQueryDirectoryObject '\\KnownDlls' failure (MigrosBank EBanking 8.2.x, Sparda Bank SecureApp 1.x)
 - #47070  DA: Inquisition can't see gamepad
 - #48534  32-bit MSXML 6.0 package fails to install in 64-bit WINEPREFIX 'package is not supported on the current processor type.'
 - #49078  Sennheiser Wireless Systems Manager 4.4 server (.NET 4.5 app) crashes with 'System.ComponentModel.Win32Exception: Unknown error (0x80090308)'
 - #49079  Multiple applications want support for CREATE_NO_WINDOW flag (0x08000000) in CreateProcess
 - #50233  Signtools from Windows 10 SDK needs 'wintrust.CryptCATAdminAcquireContext2' implementation
 - #50323  Multiple applications need 'Ws2_32.getservbyname' to read information from '%SystemRoot%\System32\Drivers\Etc\services' (Autodesk 3ds Max 9 RaySat service)
 - #50572  LDAP_AUTH_NEGOTIATE login fails as authorization user is used instead of authentication user
 - #53047  SketchUp 2021 installer quits with "IDS_ERROR_NO_WIZARD_PAGES" (native xmllite works around)
 - #53241  wtsapi32:wtsapi - test_WTSEnumerateProcessesW() crashes if a process exits during the test?
 - #56366  Worms Blast characters become grey/untextured
 - #57983  getaddrinfo cannot handle "https" as the service string on some distros, but windows 10 can
 - #58046  Horizon Chase Turbo: gamepad hotplugging doesn't work
 - #58511  GunBound crashes on unimplemented function ntoskrnl.exe.KeAcquireGuardedMutex
 - #59963  DWARF unwinding crashes on i386
 - #60080  GTA: Vice City - Blank white screen during intro video
 - #60094  Marathon crashes on launch
 - #60095  GTA: San Andreas videos are not displayed when using exclusive fullscreen
 - #60140  Signature from BCryptSignHash with RSA 512 key and PSS padding cannot be verified
 - #60218  Ground Control: Mouse movement is confined to a small area
 - #60219  Ankh - Anniversary Edition: Some textures are rendered black
 - #60221  Clive Barker's Jericho Demo has a non-fatal msiexec crash during install
 - #60223  IMFTimedText and related APIs missing
 - #60226  Wine XInput: XInputGetState returns 0 buttons for connected Bluetooth Xbox One controller
 - #60229  Netscape Navigator 3.04 for Windows 3.1 crashes on start on a null pointer
 - #60247  SetWindowLongPtr succeeds on a window owned by another process, resulting in a crash
 - #60252  winedmo: Issue with demuxer destructor logic
 - #60256  Wine application prevents X11 root window from receiving mouse button events
 - #60257  win32u: send_mouse_motion() skips resetting raw_mouse.count on empty input, causing a heap buffer overflow
 - #60267  Multiple E-Banking applications by KOBIL Systems GmbH terminate on startup due to missing WBEM namespace 'ROOT\SecurityCenter2' (MigrosBank EBanking 8.2.x, Sparda Bank SecureApp 1.x)
 - #60275  xEdit (fo4edit, fo3edit) cannot run after 11.15
 - #60285  Star Wars: Knights of the Old Republic I/II crash after start

### Changes since 11.16:
```
Akihiro Sagawa (5):
      winegstreamer: Make sure every stream has a buffer or an initial gap on initialization.
      quartz/tests: Add a timestamp test for the first sample.
      winegstreamer: Change wg_parser_create argument from bool to bit flags.
      winegstreamer: Rebase PTS values so the stream starts at zero.
      winegstreamer: Fix GstCaps reference leak in wg_parser_stream.

Alex Henrie (2):
      krnl386: Don't free DISCARDABLE resources in FreeResource16.
      compobj: Ensure compobj_malloc is initialized in CoGetMalloc16.

Alexandre Julliard (33):
      kernel32/tests: Fix incorrect tests for UI languages with zero flags.
      vkd3d: Import upstream release 2.1.
      ntdll: Load the system and user UI languages from the registry.
      ntdll: Implement RtlGetSystemPreferredUILanguages and RtlGetUserPreferredUILanguages.
      ntdll: Implement RtlGet/SetProcessPreferredUILanguages.
      ntdll: Partially implement RtlGet/SetThreadPreferredUILanguages.
      kernelbase: Reimplement GetThreadUILanguage on top of GetThreadPreferredUILanguages.
      kernelbase: Reimplement SetThreadUILanguage on top of SetThreadPreferredUILanguages.
      mlang/tests: Enable some ifdef'ed out traces.
      mlang/tests: Remove skips for old Windows versions.
      mlang/tests: Run some tests in an English locale.
      kernel32/tests: Run some tests in an English locale.
      jscript/tests: Run some tests in an English locale.
      wininet/tests: Run some tests in an English locale.
      ieframe/tests: Run tests in an English locale.
      mshtml/tests: Run tests in an English locale.
      pdh/tests: Run tests in an English locale.
      ntdll: Implement RtlpQueryDefaultUILanguage.
      ntdll: Return the default user UI language for LOCALE_CUSTOM_UI_DEFAULT.
      ntdll: Return the default system UI language for LOCALE_CUSTOM_DEFAULT.
      kernelbase: Reimplement GetSystem/UserDefaultUILanguage on top of RtlpQueryDefaultUILanguage.
      kernelbase: Add a helper to convert a LCID to a sort locale.
      kernelbase: Use the default user UI language for LOCALE_CUSTOM_UI_DEFAULT.
      kernelbase: Use the default system UI language for LOCALE_CUSTOM_DEFAULT.
      ntdll: Use the preferred UI language list to load resources.
      include: Fix wscanf* prototypes.
      include: Fix typos in Unicode macros.
      include: Fix typos in vtbl macros.
      ntdll: Fix mmap_is_in_reserved_area when area starts in the middle of the specified range.
      wow64: Check that the entire memory allocation is within the user space range.
      vbscript/tests: Run some tests with an English UI language.
      shlwapi/tests: Run some tests with an English UI language.
      faudio: Import upstream release 26.09.

Alistair Leslie-Hughes (1):
      include: Use RECT directly instead of tagRECT define.

Alvin Philips (1):
      winebus: Clarify that the Disable Hidraw toggle also affects the IOHID backend in trace message.

Bernhard M. Wiedemann (1):
      wscript: Use angle brackets to include the generated ihost.h.

Bernhard Übelacker (3):
      wbemdisp: Avoid out-of-bounds read in objectpath_get_DisplayName.
      mfplat/tests: Add broken to avoid some test failures.
      msv1_0: Fix string termination in ntlm_pool_get_ctx.

Brendan Shanks (7):
      ntdll: Spawn a Wine system thread for the macOS main thread on launch.
      winemac: Fix memory leak in get_format_entries().
      winemac: Stop using deprecated kUTTypeContent/kUTTypeData.
      winemac: Remove WineDisplayLink.
      winemac: Remove now-unnecessary check for MTLDevice.registryID.
      winemac: Stop using deprecated [NSGraphicsContext graphicsPort].
      winemac: Stop setting deprecated NSWindow.oneShot.

Connor McAdams (6):
      setupapi: Add tests for class device enumeration order.
      setupapi: Add a few more SetupDiGetClassDevs() tests.
      setupapi: Validate enumerator string passed into SetupDiGetClassDevs().
      setupapi: Use CM_Get_Device_ID_List() to implement SetupDiGetClassDevs() for devices.
      setupapi: Use CM_Get_Device_Interface_List() to implement SetupDiGetClassDevs() for interfaces.
      setupapi: Use cfgmgr32 functions to implement remove_all_device_ifaces().

Conor McCarthy (8):
      wbemdisp: Rename propertyset object to class_object.
      wbemdisp: Implement ISWbemPropertySet::NewEnum().
      wbemdisp: Initialise the variants in enumvar_Next().
      wbemdisp: Implement ISWbemMethodSet::NewEnum().
      wbemdisp: Implement get ISWbemProperty::Name.
      wbemdisp: Add ISWbemQualifierSet stub implementation.
      wbemdisp: Add ISWbemQualifierSet enumerator stub implementation.
      mfreadwrite: Queue only one output sample while draining transforms.

Daniel Lehman (2):
      ntdll/tests: Add tests for RemoveDirectory with open FindFirstFile handle.
      ntdll: Open directory handle with FILE_SHARE_DELETE in FindFirstFileW.

Dmitry Timoshkov (3):
      crypt32/tests: Add missing CryptEncodeObjectEx() return value check.
      crypt32: CryptMsgGetParam(CMSG_SIGNER_UNAUTH_ATTR_PARAM) should set returned size to 0 if there's no attributes.
      crypt32: CryptMsgGetParam(CMSG_SIGNER_AUTH_ATTR_PARAM) should set returned size to 0 if there's no attributes.

Elizabeth Figura (4):
      ntdll: Implement NtSetVolumeInformationFile(FileFsLabelInformation).
      kernel32: Reimplement SetVolumeLabel() using NtSetVolumeInformationFile().
      d3d9: Fix clearing flags for managed resources.
      wined3d: Store the device functions in context_vk.

Esme Povirk (6):
      coml2: Fix missing check for bytes read.
      gdiplus: Fix GdipMeasureDriverString with length == 0.
      gdiplus: Touch all points in GdipClearPathMarkers.
      gdiplus: Add validation to GdipSetImageAttributesNoOp.
      gdiplus: Fix incorrect pointer cast writing pens to emf+.
      gdiplus: Pass through status code in metafile_deserialize_path.

Etaash Mathamsetty (3):
      ntoskrnl.exe: Implement PsGetProcessSessionId.
      ntoskrnl.exe: Use the cached session id in PsGetCurrentProcessSessionId.
      ntoskrnl.exe: Implement PsGetProcessCreateTimeQuadPart.

Evan Morse (5):
      ntdll: Return the object manager status from NtQueryFullAttributesFile.
      server: Rewind the directory stream when checking if it is empty.
      ntdll/tests: Test SYNCHRONIZE requirement for synchronous I/O options.
      ntdll: Request SYNCHRONIZE access with the synchronous I/O options.
      ntdll: Require SYNCHRONIZE access for the synchronous I/O options.

Francis De Brabandere (3):
      vbscript: Treat a dot right after a keyword as a with-statement dot.
      vbscript: Initialize the function pointer before the recursion limit check.
      vbscript: Return "Object not a collection" for For Each on Nothing.

Gabriel Ivăncescu (9):
      mshtml: Don't throw when retrieving window.external if the DocHost returns E_NOINTERFACE.
      jscript: Fix interpreted function.prototype on-demand creation by updating value in-place.
      jscript: Don't call the setter for non-writable builtin props.
      jscript/tests: Move some tests in lang.js down.
      jscript: Use no_gc_traverse for host functions and constructors.
      jscript: Get rid of the obj parameter in gc_process_linked_obj.
      jscript: Get rid of the obj parameter in gc_process_linked_val.
      jscript: Replace the addref/release methods for host objects with get_host_disp.
      jscript: Use get_host_disp in the vtbl when retrieving the host outer dispatch.

Gijs Vermeulen (1):
      msi: Pass deformated target to HANDLE_CustomType7 in ACTION_CustomAction.

Hans Leidekker (81):
      msi: Propagate ERROR_NO_MORE_ITEMS return from custom actions.
      msi: Handle NULL params in build_msiexec_args().
      wbemdisp: Implement property_get_CIMType().
      webservices: Fix nil value check for a couple of types.
      webservices: Fix error return in WsReadStartAttribute() and read_comment_bin().
      webservices: Handle NULL msg->action in message_insert_http_headers().
      webservices: Fix buffer size check in build_dict().
      webservices: Remove redundant assignment in grow_strs_array().
      webservices: Avoid NULL dereference on error in str_to_qname().
      webservices: Decrement len after consuming closing bracket in WsDecodeUrl().
      webservices: Correctly initialize loop counter in WsWriteArray().
      webservices: Use configured backlog value in open_listener_tcp().
      webservices: Fix memory leak on error path in dup_message_mapping().
      webservices: Fix memory leak on error in dup_fault().
      webservices: Use proper type in queue_write_message_end().
      webservices: Correctly parse fragment in WsDecodeUrl().
      winhttp: Use %lu to format unsigned integer in send_request().
      include: Remove duplicate defines.
      winhttp: Fix potential buffer overflow in finished_reading().
      winhttp: Validate scheme and hostname length in run_script().
      winhttp: Fix return length for ICU_ESCAPE with null-terminated string in get_comp_length().
      winhttp: Fix a trace in request_set_option().
      winhttp: Validate day of week and month in WinHttpTimeFromSystemTime().
      winhttp: Fix error value returned from send_socket_shutdown().
      winhttp: Check for allocation failure in add_domain().
      bcrypt: Remove duplicate chain mode field.
      bcrypt: Return proper values from ecc_blob_curve().
      bcrypt: Fix parameter validation in BCryptDeriveKeyCapi().
      bcrypt: Verify len against output buffer size in import_key().
      msi: Fix memory leak on error in append_productcode().
      msi: Fix handle leak on error in get_fusion_filename().
      msi: Fix row index in DELETE_execute().
      msi: Handle tables without primary key columns in add_table_to_db().
      msi: Handle empty string properties in read_properties_from_data().
      msi: Fix buffer size calculation in TransformView_add_column().
      msi: Handle NULL return from msi_dup_record_field() in get_signature().
      msi: Handle missing manifest file in msi_install_assembly().
      msi: Consistently check dialog_add_control() failure.
      msi: Handle NULL text in dialog_hyperlink().
      msi: Remove unused field from struct drop_view.
      msi: Fix error message in apply_substorage_transform().
      wbemprox: Add SecurityCenter2 namespace.
      msi: MsiSetMode() returns UINT.
      msi: Handle CreateNamedPipeW() failure.
      msi: Fix return values in cabinet_copy_file().
      msi: Validate buffer parameters in MsiGetPatchInfoA/W().
      msi: Handle NULL folder->ResolvedTarget in set_target_path().
      msi: Handle NULL guids in msi_check_patch_applicable().
      msi: Fix out-of-bounds read in decode_base85_guid().
      msi: Don't free caller owned buffer on error in TransformView_delete_row().
      msi: Handle empty file in read_text_archive().
      msi: Fix potential heap buffer overflow in DISTINCT_execute().
      msi: Avoid out-of-bounds read in read_properties_from_data().
      msi: Avoid out-of-bounds read in sqliteGetToken().
      widl: Fix buffer size in add_composable_attr_step2().
      widl: Pass correct target table in serialize_methodimpl_table().
      wbemprox: Zero-initialize columns in create_signature_columns_and_data().
      wbemprox: Add NULL check in class_object_GetMethod().
      wbemprox: Avoid using uninitialized pointer in wbem_context_Clone().
      wbemprox: Use appropriate value for pointer return.
      wbemprox: Pass correct buffer size to swprintf() in fill_cache_memory() and fill_processor().
      wbemprox: Fix memory leak on error in get_owner().
      wbemprox: Fix off-by-one in enum_class_object_Skip().
      wbemprox: Set query namespace in wbem_services_ExecNotificationQueryAsync().
      wbemprox: Fix cleanup of associations.
      wbemprox: Fix memory leak on error in get_pnp_entities() and get_display_adapters().
      wbemprox: Fix memory leak on error in class_object_SpawnInstance().
      wbemprox: Fix memory leak on error in create_signature_columns_and_data().
      wldap32: Fix a memory leak in WLDAP32_ldap_unbind_s().
      wldap32: Fix buffer overflow in escape_filter_element().
      wldap32: Don't try to free a static buffer in ldap_get_paged_count().
      wldap32: Avoid double free in ldap_search_init_pageW().
      wldap32: Free memory on error in create_page_control().
      secur32: Properly validate context handle in schan_Encrypt/DecryptMessage().
      secur32: Remove an outdated comment.
      secur32: Fix misplaced parentheses in schan_set_application_protocols().
      secur32: Fix memory leak on error in ensure_remote_cert().
      secur32: Fix a memory leak on error in get_key_container_path().
      secur32: Free GnuTLS credentials on error in acquire_credentials_handle().
      secur32: Pass correct comment length in _copyPackageInfoFlatWToA().
      secur32: Use the right deallocator in thunk_ContextAttributesAToW().

Henri Verbeet (2):
      wined3d: Don't link to libdxguid.
      d3dcompiler: Implement D3DReflect() on top of D3DReflectVKD3D().

Huoju Cheng (1):
      gdiplus: Fix font family lookup after font substitution.

Ivo Ivanov (3):
      dinput: Make instance guid Data4 last bytes similar to Windows.
      hidclass.sys: Use FDO instance_id instead of serial number for multi-TLC child PDOs.
      winebus.sys: Return an error on IOCTL_HID_GET_STRING when no serial number is available.

Jacek Caban (12):
      winegcc: Handle CPU_ARM64EC in get_multiarch_dir.
      ntdll: Check that the pointer is within the address space limits before accessing EcCodeBitMap.
      ntdll: Don't modify non-volatile registers before switching to the kernel stack in the ARM64 syscall dispatcher.
      ntdll: Avoid exposing ARM64 syscall dispatcher to the client side.
      wow64: Don't use static for pBTCpuSimulate.
      winegstreamer: Silence -Wunused-but-set-global warning.
      gitlab: Update to llvm-mingw 20260826.
      ntdll: To try to handle suspend doorbell in leave_syscall_callback if the thread is in simulation.
      ntdll: Move KiUserEmulationDispatcher handling to restore_context.
      ntdll: Define make_esr only on Linux targets.
      ntdll: Suppress debug events during CPU simulation.
      ntdll/tests: Adjust ARM64EC Rip tests in test_debugger for current Windows versions.

Ken Sharp (1):
      wined3d: Cast UINT64 handles to avoid compiler warnings.

Matteo Bruni (10):
      d3dx10/tests: Disable two cube textures tests with invalid texture dimensions.
      d3dx11/tests: Disable two cube textures tests with invalid texture dimensions.
      d3dx10/tests: Tweak a sRGB test to avoid undefined behavior on Windows.
      d3dx11/tests: Tweak a sRGB test to avoid undefined behavior on Windows.
      d3dx11/tests: Get rid of a debug trace.
      d3dcompiler/tests: Get rid of a debug trace.
      d3dx9: Move D3DXSHProjectCubeMap() implementation to texture.c.
      d3dx9: Move some exports from mesh.c to math.c.
      d3dx10: Use shared code for math exports.
      include: Add some function prototypes to d3dx10math.h.

Michel Weinachter (7):
      ntdll: Make RtlCopySid() return NTSTATUS instead of BOOLEAN.
      ntoskrnl.exe: Implement MmMapLockedPagesSpecifyCache().
      ntoskrnl.exe/tests: Test the MDL flags maintained by test_mdl_map().
      ntoskrnl.exe: Implement MmUnmapLockedPages().
      ntoskrnl.exe: Implement MmProbeAndLockPages() and MmUnlockPages().
      ntoskrnl.exe/tests: Test MmBuildMdlForNonPagedPool().
      ntoskrnl.exe: Implement MmBuildMdlForNonPagedPool().

Nat Brown (1):
      ntdll: Keep reserved area bounds page aligned when avoiding 4GB wrap-around.

Nello De Gregoris (12):
      evr: Implement IMFVideoDisplayControl::SetRenderingPrefs().
      evr/tests: Add tests for IMFVideoDisplayControl rendering preferences.
      winegstreamer/wma_decoder: Implement GetInputCurrentType.
      winegstreamer/wma_decoder: Implement GetOutputCurrentType.
      ntoskrnl.exe: Add stub for KeRegisterBugCheckCallback().
      ntoskrnl.exe: Add stub for KeRegisterBugCheckReasonCallback().
      ntoskrnl.exe: Add stub for KeDeregisterBugCheckReasonCallback().
      ntoskrnl.exe/tests: Add tests for PsGetProcessSessionId.
      winegstreamer: Add winegstreamer_create_wma_decoder().
      wmadmod: Create the decoder without CoCreateInstance().
      xaudio2/tests: Test creating an xWMA source voice without COM.
      ntoskrnl.exe/tests: Add tests for PsGetProcessCreateTimeQuadPart.

Nickita Biryulin (1):
      server: Remove dangling pointer from load_version_resource.

Nikolay Sivov (77):
      mfplat/tests: Remove todo on now succeeding test.
      include: Add IMFTimedText* definitions.
      comdlg32/filedlg: Improve CDM_GETFILEPATH when view has an active selection.
      comdlg32/filedlg: Handle allocation failures in the CDM_GETFILEPATH handler.
      comdlg32: Use ReleaseStgMedium() from ole32.
      msxml3: Move transformNodeToObject() to a reusable helper.
      msxml3: Add explicit traces for unsupported destination objects in transformNodeToObject().
      msxml3: Remove a workaround when transforming to a document by using the document stream.
      msxml3/tests: Add some tests for IXSL* object properties.
      msxml3: Implement stylesheet property getter.
      msxml3: Implement ownerTemplate() property.
      shell32/shellview: Implement Ctrl-A shortcut for the item list.
      msxml3/dom: Fix a typo in a node type string.
      msxml3: Unconditionally release bind context instance.
      mfplat: Fix a typo in the async write stream callback.
      mfplat: Remove duplicated checks from the wrapper stream methods.
      include: Fix PJOBOBJECT_END_OF_JOB_TIME_INFORMATION type.
      include: Fix duplicated EnumServicesStatus macro.
      include: Fix a typo in OPEN_VIRTUAL_DISK_VERSION enum member.
      include: Fix AW macros in shlwapi.h.
      include: Add missing macros for ID3DXRenderToEnvMap::OnResetDevice().
      include: Fix MoveFileTransacted macro.
      server: Fix freeing unlinked windows.
      msado15: Initialize variant before copying to it.
      msado15: Forward to correct method in IRowsetExactScroll::Hash().
      msado15: Fix allocation failure check in FindNextRow().
      msado15: Fix variant type check when getting bookmark data.
      msado15: Fix column info leak on failure paths.
      msado15: Initialize refcount for the Property objects.
      d2d1: Check for allocation failures when recording command lists.
      d2d1: Fix potential brush use-after-free in geometry fill recording.
      d2d1: Put command list in error state on brush creation failure right away.
      d2d1: Better handle NULL locale name when recording DrawGlyphRun() calls.
      dwrite/ot: Fix TTC header read size.
      dwrite: Remove duplicated weight pattern check.
      dwrite/format: Use consistent way to check for format property changes.
      dwrite: Fix a leak on error path in the resource stream.
      evr: Return failure for unsupported interfaces on MR_VIDEO_RENDER_SERVICE.
      evr/mixer: Fix a copy-paste type in aperture values handling.
      evr/presenter: Fix potential use-after-free on swapchain interface.
      scrrun/stream: Remove wrong object release on file read failure.
      oleaut32/olepicture: Consistently use CRT allocation functions.
      oleaut32: Fix allocation failure check in SafeArrayGetElement().
      oleaut32: Use correct types for UI8/I8 -> UI2 conversion.
      oleaut32: Use correct fields in VarSub() between UI1 values.
      dbgeng: Remove unnecessary static variables.
      dbgeng/tests: Add a GetModuleParameters() test with start module index.
      dbgeng: Fix GetModuleParameters() used with start module index.
      winedmo: Use correct parameter structure in wow64_demuxer_destroy().
      winedmo: Do not access freed context on cleanup.
      dxgi: Fix cleanup path on CreateSurface() failure.
      dxgi: Correctly initialize output pointer on GetResource() failure.
      dxgi: Use correct flags value in the trace message.
      atl: Fix typelib instance leak in AtlGetObjectSourceInterface().
      atl: Do not use interface pointer after it's been released in AtlAxCreateControlLicEx().
      d3d10: Do not reference adapter on failure path of the device creation.
      d3d10_1: Do not reference adapter on failure path of the device creation.
      d3d11: Do not reference adapter on failure path of the device creation.
      winewayland: Use null-terminated string for the clipboard class name.
      ntdll: Use appropriate status code on allocation failure in TpAllocIoCompletion().
      d3dx10/tests: Add another sprite rendering test to verify sampler filter type.
      d3dx10/tests: Add a sprite immediate rendering tests with non-empty batch.
      d3dx10: Partially implement sprite rendering.
      d3dx10: Implement sprite view transform methods.
      propsys: Remove duplicated property info entry.
      propsys: Do not check for null string pointers in InitPropVariantFromStringVector().
      propsys/tests: Add a test for PropVariantCompareEx(VT_CLSID,VT_LPWSTR).
      propsys: Use coerced value for VT_CLSID case in PropVariantCompareEx().
      propsys: Use coerced value for VT_UI1 vectors case in PropVariantCompareEx().
      kernel32: Add SetThreadpoolTimerEx() implementation.
      include: Remove duplicated macros from commdlg.h.
      include: Remove duplicated definitions from d3d9types.h.
      include: Remove duplicated prototype from gdiplusflat.h.
      include: Fix IF_TYPE_PLC value.
      include: Fix KSDATAFORMAT_ATTRIBUTES value.
      include: Remove duplicated prototype from corecrt_wstdio.h.
      include: Use correct type for SERVICE_INFOW fields.

Paul Gofman (3):
      win32u: Only resolve default draw framebuffer before blit or readback.
      ntdll: Remove a leftover no-op line in RtlCopyExtendedContext().
      ntdll/tests: Fix two tests in test_extended_context() wrongly using ternary expression.

Pavel Cheloveckov (1):
      po: Update Russian translation.

Piotr Caban (20):
      kerberos: Fix wow64 unseal_message wrapper.
      kerberos: Don't access Lsa mode data in user mode functions.
      kerberos: Fix copying data to wow client in LsaApCallPackageUntrusted.
      secur32: Avoid LSA_SEC_HANDLE truncation when moving handle between 32 and 64-bit processes.
      programs: Add SamSs service stub.
      secur32: Retrieve security packages list from lsass.exe.
      secur32: Call LSA mode functions from lsass.exe.
      msv1_0: Support ISC_REQ_MUTUAL_AUTH in ntlm_SpInitLsaModeContext.
      msv1_0: Reply with password if ntlm_auth asks for it.
      msv1_0: Reuse ntlm_auth processes.
      msv1_0: Initialize output buffer on successfull local authentication.
      lsass: Accept NULL timestamps in initialize_security_context and accept_security_context.
      secur32: Fix memory allocation in LsaGetLogonSessionData.
      rsaenh/tests: Add more tests for RSA keys with public exponent 1.
      symcrypt: Don't use SymCryptIntExtendedGcd to compute private exponent if public exponent is 1.
      rsaenh: Fix buffer size passed to SymCryptIntCreate in rsa_decrypt().
      msv1_0: Fix error handling in ntlm_SpAcceptLsaModeContext.
      msv1_0: Use NTLM local authentication when empty authentication data is provided.
      secur32: Handle SECPKG_ATTR_PACKAGE_INFO in lsa_QueryContextAttributesA.
      msv1_0: Handle SECPKG_ATTR_PACKAGE_INFO in ntlm_SpQueryContextAttributes.

Rémi Bernon (40):
      win32u: Flush mouse input motion when hitting the clipping rect edges.
      opengl32: Use the drawable latched virtual size for multisample resolve.
      opengl32: Use the drawable latched virtual size for front buffer emulation.
      win32u: Initialize framebuffer attachment storage before binding them.
      win32u: Avoid requiring OpenGL 4.5 for the framebuffer surface.
      opengl32: Always isolate some object names allocated in win32u.
      win32u: Create textures for the FBO single sampled surfaces.
      win32u: Swap front / back color attachments in framebuffer_surface_swap.
      win32u: Avoid unnecessary internal / client context switches.
      win32u: Introduce an optional target surface for the framebuffer surface.
      win32u: Allow creating client surfaces with raw physical coordinates.
      win32u: Support OpenGL scaling according to emulated resolution.
      win32u: Use the framebuffer target drawable in make_client_context_current.
      win32u: Only flag client surfaces as updated if something changed.
      win32u: Implement a default gamma ramp when emulating display modes.
      win32u: Support gamma ramp emulation in GL when emulating display modes.
      opengl32: Fix multisampled default framebuffer draw buffer resolution.
      win32u: Use SRGB framebuffer if target default FBO is SRGB capable.
      win32u: Restore set_window_long_internal GWLP_WNDPROC check.
      opengl32: Move extension string filtering to the PE side.
      win32u: Move context extensions parsing from opengl32.
      win32u: Initialize global extensions with the global context.
      win32u: Move some global extension checks from winemac.
      opengl32: Hide some unix-only extensions from the PE side.
      joy.cpl: Fix incorrect POV index in paint_povs_view.
      joy.cpl: Remove unnecessary thread_stop / state_event events creation.
      joy.cpl: Clear device interfaces when switching tab.
      joy.cpl: Check IID_IGameController QueryInterface result.
      joy.cpl: Avoid underflow when setting motor vibration.
      winex11: Keep track of mouse device and pointer button mappings.
      winex11: Listen to raw mouse button events on the root window.
      opengl32: Fix and simplify WOW64 string allocation.
      opengl32: Fix incorrect mask for GL_BACK_RIGHT.
      win32u: Fix egldrv_describe_pixel_format array bound check.
      win32u: Fix some truncated query_renderer_integer memcpy.
      user32: Fix some length overflows in init_class_name(_ansi).
      win32u: Avoid possible buffer overflow in NtUserGetAtomName.
      server: Cast cls_extra - size to int before comparison.
      winevulkan: Check for returnedonly on individual members.
      winevulkan: Don't flag dynamic array / lengths as returnedonly.

Shaun Ren (1):
      windowscodecs: Always pass a valid pcbRead pointer to IStream::Read().

Sven Baars (1):
      gitlab: Install libva-dev.

Thibault Payet (1):
      ntdll/unix: Fix logical_proc_info_add_numa_node parameter order.

Vibhav Pant (2):
      rometadata/tests: Add tests for IMetaDataImport::{EnumInterfaceImpl, GetInterfaceImplProps, FindTypeRef}.
      rometadata/tests: Add initial tests for WinRT metadata shipped with Windows.

Ziia Shi (2):
      pdh: Avoid access violation in PdhCloseQuery on bad handle.
      powrprof: Stub PowerReadACValueIndex.
```
