/*
 * Installed Office runtime checks.
 *
 * Inspired by Mirko Brombin and the Bottles/Soda Office WinRT package checks:
 * https://github.com/bottlesdevs/wine/blob/f0c5aa0cfabc1ab1e2fe08ab292b362bba4cf77f/.github/soda-office-winrt-smoke.c
 *
 * Copyright 2026 Elkana Bardugo
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* Public interface IDs from the corresponding Windows Runtime contracts. */
static const GUID iid_activation_factory =
    {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_core_application =
    {0x0aacf7a4, 0x5e1d, 0x49df, {0x80, 0x34, 0xfb, 0x6a, 0x68, 0xbc, 0x5e, 0xd1}};
static const GUID iid_protection_policy_statics2 =
    {0xb68f9a8c, 0x39e0, 0x4649, {0xb2, 0xe4, 0x07, 0x0a, 0xb8, 0xa5, 0x79, 0xb3}};
static const GUID iid_web_authentication_statics =
    {0x6aca7c92, 0xa581, 0x4479, {0x9c, 0x10, 0x75, 0x2e, 0xff, 0x44, 0xfd, 0x34}};
static const GUID iid_web_authentication_statics4 =
    {0x54e633fe, 0x96e0, 0x41e8, {0x98, 0x32, 0x12, 0x98, 0x89, 0x7c, 0x2a, 0xaf}};
static const GUID iid_web_token_request_factory =
    {0x6cf2141c, 0x0ff0, 0x4c67, {0xb8, 0x4f, 0x99, 0xdd, 0xbe, 0x4a, 0x72, 0xc9}};
static const GUID iid_compositor =
    {0xb403ca50, 0x7f8c, 0x4e83, {0x98, 0x5f, 0xcc, 0x45, 0x06, 0x00, 0x36, 0xd8}};
static const GUID iid_composition_device =
    {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
static const GUID iid_composition_device3 =
    {0x0987cb06, 0xf916, 0x48bf, {0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9}};

static int check_factory(const WCHAR *name, const GUID *iid, const char *label)
{
    IUnknown *factory = NULL;
    HSTRING class_id = NULL;
    HRESULT hr;

    hr = WindowsCreateString(name, wcslen(name), &class_id);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(class_id, iid, (void **)&factory);
    WindowsDeleteString(class_id);
    printf("%s:%08lx\n", label, (unsigned long)hr);
    if (FAILED(hr)) return 1;
    if (!factory)
    {
        printf("%s:null_interface\n", label);
        return 1;
    }
    IUnknown_Release(factory);
    return 0;
}

static int check_compositor(void)
{
    IInspectable *instance = NULL;
    IUnknown *compositor = NULL;
    HSTRING class_id = NULL;
    HRESULT hr;
    const WCHAR name[] = L"Windows.UI.Composition.Compositor";

    hr = WindowsCreateString(name, sizeof(name) / sizeof(name[0]) - 1, &class_id);
    if (SUCCEEDED(hr)) hr = RoActivateInstance(class_id, &instance);
    WindowsDeleteString(class_id);
    if (SUCCEEDED(hr) && !instance) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = IInspectable_QueryInterface(instance, &iid_compositor, (void **)&compositor);
    if (SUCCEEDED(hr) && !compositor) hr = E_UNEXPECTED;
    printf("compositor:%08lx\n", (unsigned long)hr);
    if (compositor) IUnknown_Release(compositor);
    if (instance) IInspectable_Release(instance);
    return FAILED(hr);
}

static int check_dcomp(void)
{
    HRESULT (WINAPI *create_device)(IUnknown *, REFIID, void **);
    IUnknown *device = NULL, *device3 = NULL;
    HMODULE module;
    HRESULT hr = E_FAIL;

    module = LoadLibraryW(L"dcomp.dll");
    if (module)
    {
        create_device = (void *)GetProcAddress(module, "DCompositionCreateDevice");
        if (create_device) hr = create_device(NULL, &iid_composition_device, (void **)&device);
        if (SUCCEEDED(hr) && !device) hr = E_UNEXPECTED;
        if (SUCCEEDED(hr)) hr = IUnknown_QueryInterface(device, &iid_composition_device3, (void **)&device3);
        if (SUCCEEDED(hr) && !device3) hr = E_UNEXPECTED;
    }
    printf("dcomp_device3:%08lx\n", (unsigned long)hr);
    if (device3) IUnknown_Release(device3);
    if (device) IUnknown_Release(device);
    if (module) FreeLibrary(module);
    return FAILED(hr);
}

int main(int argc, char **argv)
{
    BOOL soda = argc == 2 && !strcmp(argv[1], "--soda");
    unsigned int bits = sizeof(void *) * 8;
    int failures = 0;
    HRESULT hr;

    if (argc != 1 && !soda)
    {
        fprintf(stderr, "Usage: office-runtime.exe [--soda]\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("process_bits:%u\n", bits);
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    printf("ro_initialize:%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;

    failures += check_factory(L"Windows.Security.EnterpriseData.ProtectionPolicyManager",
            &iid_protection_policy_statics2, "protection_policy");
    failures += check_factory(L"Windows.ApplicationModel.Core.CoreApplication",
            &iid_core_application, "core_application");
    if (soda)
    {
        /* This readiness profile fails until MessageWebSocket is implemented. */
        failures += check_factory(L"Windows.Networking.Sockets.MessageWebSocket",
                &iid_activation_factory, "message_websocket");
    }
    else
    {
        failures += check_factory(L"Windows.ApplicationModel.Package", &iid_activation_factory, "package");
        failures += check_factory(L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager",
                &iid_web_authentication_statics, "web_authentication");
        failures += check_factory(L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager",
                &iid_web_authentication_statics4, "web_authentication4");
        failures += check_factory(L"Windows.Security.Authentication.Web.Core.WebTokenRequest",
                &iid_web_token_request_factory, "web_token_request");
        failures += check_compositor();
        failures += check_dcomp();
    }
    RoUninitialize();
    printf("office_runtime:%s:%u:%s\n", soda ? "soda" : "supported", bits, failures ? "failed" : "passed");
    return failures != 0;
}
