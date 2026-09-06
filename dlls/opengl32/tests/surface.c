/*
 * Concurrent window surface lifetime tests
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/test.h"
#include "wine/wgl.h"

struct render_state
{
    HDC dc;
    HGLRC context;
    HANDLE ready, frame;
    LONG stop, frames;
    BOOL current;
};

/* The window owner must dispatch messages while waiting for the rendering
 * thread: a WGL implementation may synchronously message that window. */
static DWORD wait_with_messages(HANDLE event, DWORD timeout)
{
    ULONGLONG end = GetTickCount64() + timeout, now;
    DWORD ret;
    MSG msg;

    for (;;)
    {
        now = GetTickCount64();
        ret = MsgWaitForMultipleObjects(1, &event, FALSE, now < end ? end - now : 0, QS_ALLINPUT);
        if (ret != WAIT_OBJECT_0 + 1) return ret;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

static DWORD WINAPI render_thread(void *arg)
{
    struct render_state *state = arg;
    BOOL ret;

    state->current = wglMakeCurrent(state->dc, state->context);
    ok(state->current, "wglMakeCurrent failed, error %lu.\n", GetLastError());
    SetEvent(state->ready);
    if (!state->current) return 0;
    trace("Renderer: %s.\n", glGetString(GL_RENDERER));
    while (!InterlockedCompareExchange(&state->stop, 0, 0))
    {
        glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ret = SwapBuffers(state->dc);
        ok(ret, "SwapBuffers failed, error %lu.\n", GetLastError());
        if (!ret) break;
        InterlockedIncrement(&state->frames);
        SetEvent(state->frame);
    }
    ok(wglMakeCurrent(NULL, NULL), "Failed to release the current context.\n");
    return 0;
}

static HWND create_surface_window(HWND parent, int x)
{
    HWND hwnd = CreateWindowW(L"static", L"surface concurrency", WS_VISIBLE | WS_CLIPSIBLINGS |
                              WS_CLIPCHILDREN | (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW),
                              x, 20, 240, 180, parent, NULL, NULL, NULL);
    ok(!!hwnd, "CreateWindow failed, error %lu.\n", GetLastError());
    return hwnd;
}

static void test_surface_child(void)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                                PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 24};
    struct render_state state = {0};
    HWND roots[2] = {0}, branches[2] = {0}, drawable = NULL, previous;
    HANDLE thread = NULL;
    DWORD ret;
    unsigned int i, old = 0, next;
    int format;

    roots[0] = create_surface_window(NULL, 20);
    roots[1] = create_surface_window(NULL, 300);
    if (!roots[0] || !roots[1]) goto done;
    branches[0] = create_surface_window(roots[0], 0);
    branches[1] = create_surface_window(roots[1], 0);
    if (!branches[0] || !branches[1]) goto done;
    if (!(drawable = create_surface_window(branches[0], 0))) goto done;
    state.dc = GetDC(drawable);
    ok(!!state.dc, "GetDC failed.\n");
    if (!state.dc) goto done;
    if (!(format = ChoosePixelFormat(state.dc, &pfd)))
    {
        win_skip("No window OpenGL pixel format.\n");
        goto done;
    }
    ret = SetPixelFormat(state.dc, format, &pfd);
    ok(ret, "SetPixelFormat failed, error %lu.\n", GetLastError());
    if (!ret) goto done;
    state.context = wglCreateContext(state.dc);
    ok(!!state.context, "wglCreateContext failed, error %lu.\n", GetLastError());
    if (!state.context) goto done;
    state.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    state.frame = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(state.ready && state.frame, "CreateEvent failed.\n");
    if (!state.ready || !state.frame) goto done;
    thread = CreateThread(NULL, 0, render_thread, &state, 0, NULL);
    ok(!!thread, "CreateThread failed.\n");
    if (!thread) goto done;
    ret = wait_with_messages(state.ready, 10000);
    ok(ret == WAIT_OBJECT_0, "Renderer startup timed out: %#lx.\n", ret);
    if (ret != WAIT_OBJECT_0) ExitProcess(1);
    if (!state.current) goto done;

    /* Keep the drawable and its DC alive until the renderer joins. Only the
     * window owner reparents/resizes/destroys HWNDs. Destroy the old ancestor
     * after moving the drawable out, while presentation continues on the new
     * hierarchy. This exercises stale driver snapshots without using a dead DC. */
    for (i = 0; i < 200; ++i)
    {
        winetest_push_context("iteration %u", i);
        next = old ^ 1;
        previous = SetParent(drawable, branches[next]);
        ok(previous == branches[old], "Unexpected previous parent %p.\n", previous);
        if (previous != branches[old])
        {
            winetest_pop_context();
            goto done;
        }
        ok(GetParent(drawable) == branches[next], "Wrong new parent.\n");
        ok(SetWindowPos(roots[next], NULL, 0, 0, 260 + i % 8, 200 + i % 8,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "Resize failed.\n");
        if (!(i % 8))
        {
            ShowWindow(roots[next], SW_MAXIMIZE);
            ShowWindow(roots[next], SW_RESTORE);
        }
        ok(RedrawWindow(roots[next], NULL, NULL, RDW_INVALIDATE | RDW_ERASE |
                       RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW), "RedrawWindow failed.\n");
        if (previous == branches[old])
        {
            ok(DestroyWindow(branches[old]), "DestroyWindow failed.\n");
            branches[old] = create_surface_window(roots[old], 0);
            if (!branches[old]) ExitProcess(1);
        }
        old = next;
        ResetEvent(state.frame);
        ret = wait_with_messages(state.frame, 10000);
        ok(ret == WAIT_OBJECT_0, "Presentation stopped after hierarchy change: %#lx.\n", ret);
        if (ret != WAIT_OBJECT_0) ExitProcess(1);
        winetest_pop_context();
    }
    trace("Completed %u hierarchy changes, %ld frames.\n", i,
          InterlockedCompareExchange(&state.frames, 0, 0));

done:
    if (thread)
    {
        InterlockedExchange(&state.stop, 1);
        ret = wait_with_messages(thread, 10000);
        ok(ret == WAIT_OBJECT_0, "Rendering thread failed to stop: %#lx.\n", ret);
        if (ret != WAIT_OBJECT_0) ExitProcess(1);
        CloseHandle(thread);
    }
    if (state.frame) CloseHandle(state.frame);
    if (state.ready) CloseHandle(state.ready);
    if (state.context) ok(wglDeleteContext(state.context), "wglDeleteContext failed.\n");
    if (state.dc) ReleaseDC(drawable, state.dc);
    if (drawable) DestroyWindow(drawable);
    for (i = 0; i < 2; ++i)
    {
        if (branches[i]) DestroyWindow(branches[i]);
        if (roots[i]) DestroyWindow(roots[i]);
    }
}

START_TEST(surface)
{
    STARTUPINFOA startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    char **argv, command[2 * MAX_PATH];
    DWORD ret;
    int argc = winetest_get_mainargs(&argv);
    BOOL created;

    if (argc > 2 && !strcmp(argv[2], "child"))
    {
        test_surface_child();
        return;
    }

    /* A deadlock can block the window owner inside SetParent/SetWindowPos.
     * A separate process bounds that failure without terminating a thread
     * holding locks in the rest of the test suite. */
    snprintf(command, sizeof(command), "\"%s\" surface child", argv[0]);
    created = CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process);
    ok(created, "CreateProcess failed, error %lu.\n", GetLastError());
    if (!created) return;
    ret = WaitForSingleObject(process.hProcess, 90000);
    ok(ret == WAIT_OBJECT_0, "Concurrent presentation/window updates hung: %#lx.\n", ret);
    if (ret != WAIT_OBJECT_0)
    {
        /* The child may exit between the timeout and TerminateProcess. */
        if (!TerminateProcess(process.hProcess, 1))
            ok(WaitForSingleObject(process.hProcess, 1000) == WAIT_OBJECT_0,
               "Failed to terminate hung test child, error %lu.\n", GetLastError());
    }
    wait_child_process(&process);
}
