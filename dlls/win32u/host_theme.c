/*
 * Host color-scheme integration
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#endif

#include "windef.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(system);

#ifdef SONAME_LIBDBUS_1

#define DBUS_FUNCS                                      \
    DO_FUNC(dbus_bus_get);                              \
    DO_FUNC(dbus_connection_send_with_reply_and_block); \
    DO_FUNC(dbus_connection_unref);                     \
    DO_FUNC(dbus_error_free);                           \
    DO_FUNC(dbus_error_init);                           \
    DO_FUNC(dbus_error_is_set);                         \
    DO_FUNC(dbus_message_iter_append_basic);            \
    DO_FUNC(dbus_message_iter_get_arg_type);            \
    DO_FUNC(dbus_message_iter_get_basic);               \
    DO_FUNC(dbus_message_iter_init);                    \
    DO_FUNC(dbus_message_iter_init_append);             \
    DO_FUNC(dbus_message_iter_recurse);                 \
    DO_FUNC(dbus_message_new_method_call);              \
    DO_FUNC(dbus_message_unref);                        \
    DO_FUNC(dbus_threads_init_default)

#define DO_FUNC(f) static typeof(f) *p_##f
DBUS_FUNCS;
#undef DO_FUNC

static pthread_once_t dbus_once = PTHREAD_ONCE_INIT;
static BOOL dbus_available;
static uint64_t portal_retry_after;

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime( CLOCK_MONOTONIC, &now )) return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void load_dbus_functions(void)
{
    void *handle;

    if (!(handle = dlopen( SONAME_LIBDBUS_1, RTLD_NOW )))
    {
        WARN( "failed to load DBus support: %s\n", dlerror() );
        return;
    }

#define DO_FUNC(f) if (!(p_##f = dlsym( handle, #f ))) goto failed
    DBUS_FUNCS;
#undef DO_FUNC

    if (!p_dbus_threads_init_default()) goto failed;
    dbus_available = TRUE;
    return;

failed:
    WARN( "failed to load DBus support: %s\n", dlerror() );
    dlclose( handle );
}

static unsigned int get_portal_color_scheme(void)
{
    static const char desktop[] = "org.freedesktop.portal.Desktop";
    static const char path[] = "/org/freedesktop/portal/desktop";
    static const char settings[] = "org.freedesktop.portal.Settings";
    static const char appearance[] = "org.freedesktop.appearance";
    static const char color_scheme[] = "color-scheme";
    const char *appearance_arg = appearance, *color_scheme_arg = color_scheme;
    DBusMessageIter iter, value;
    DBusMessage *request = NULL, *reply = NULL;
    DBusConnection *connection = NULL;
    uint64_t now = monotonic_milliseconds();
    DBusError error;
    unsigned int ret = 0;

    pthread_once( &dbus_once, load_dbus_functions );
    if (!dbus_available) return 0;

    p_dbus_error_init( &error );
    if (portal_retry_after && now < portal_retry_after) return 0;
    if (!(connection = p_dbus_bus_get( DBUS_BUS_SESSION, &error ))) goto done;
    if (!(request = p_dbus_message_new_method_call( desktop, path, settings, "Read" ))) goto done;

    p_dbus_message_iter_init_append( request, &iter );
    if (!p_dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &appearance_arg ) ||
        !p_dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &color_scheme_arg ))
        goto done;

    if (!(reply = p_dbus_connection_send_with_reply_and_block( connection, request, 250, &error ))) goto done;
    if (!p_dbus_message_iter_init( reply, &iter )) goto done;

    while (p_dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_VARIANT)
    {
        p_dbus_message_iter_recurse( &iter, &value );
        iter = value;
    }
    if (p_dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_UINT32)
        p_dbus_message_iter_get_basic( &iter, &ret );

    if (ret > 2) ret = 0;

done:
    portal_retry_after = ret ? 0 : now + 30000;

    if (p_dbus_error_is_set( &error ))
    {
        TRACE( "portal color-scheme query failed: %s\n", error.message );
        p_dbus_error_free( &error );
    }
    if (reply) p_dbus_message_unref( reply );
    if (request) p_dbus_message_unref( request );
    if (connection) p_dbus_connection_unref( connection );
    return ret;
}

#else

static unsigned int get_portal_color_scheme(void)
{
    return 0;
}

#endif

static BOOL contains_dark( const char *str )
{
    static const char dark[] = "dark";
    unsigned int i;

    if (!str) return FALSE;
    while (*str)
    {
        for (i = 0; dark[i] && str[i] && tolower( (unsigned char)str[i] ) == dark[i]; i++);
        if (!dark[i]) return TRUE;
        str++;
    }
    return FALSE;
}

static unsigned int get_toolkit_color_scheme(void)
{
    enum section { SECTION_OTHER, SECTION_GENERAL, SECTION_KDE, SECTION_WINDOW };
    const char *config_home = getenv( "XDG_CONFIG_HOME" );
    const char *gtk_theme = getenv( "GTK_THEME" );
    const char *home = getenv( "HOME" );
    char path[4096], line[1024];
    enum section section = SECTION_OTHER;
    unsigned int inferred = 0;
    FILE *file;

    if (gtk_theme && *gtk_theme) return contains_dark( gtk_theme ) ? 1 : 2;

    if (config_home && *config_home)
        snprintf( path, sizeof(path), "%s/kdeglobals", config_home );
    else if (home && *home)
        snprintf( path, sizeof(path), "%s/.config/kdeglobals", home );
    else
        return 0;

    if (!(file = fopen( path, "r" ))) return 0;
    while (fgets( line, sizeof(line), file ))
    {
        char *end;

        if (line[0] == '[')
        {
            if (!strncmp( line, "[General]", 9 )) section = SECTION_GENERAL;
            else if (!strncmp( line, "[KDE]", 5 )) section = SECTION_KDE;
            else if (!strncmp( line, "[Colors:Window]", 15 )) section = SECTION_WINDOW;
            else section = SECTION_OTHER;
            continue;
        }

        end = line + strlen( line );
        while (end > line && isspace( (unsigned char)end[-1] )) *--end = 0;

        if (section == SECTION_GENERAL && !strncmp( line, "ColorScheme=", 12 ))
        {
            inferred = contains_dark( line + 12 ) ? 1 : 2;
            break;
        }
        if (section == SECTION_KDE && !strncmp( line, "LookAndFeelPackage=", 19 ))
        {
            inferred = contains_dark( line + 19 ) ? 1 : 2;
            continue;
        }
        if (section == SECTION_WINDOW && !strncmp( line, "BackgroundNormal=", 17 ))
        {
            unsigned int red, green, blue;

            if (sscanf( line + 17, "%u,%u,%u", &red, &green, &blue ) == 3)
                inferred = red + green + blue < 384 ? 1 : 2;
        }
    }
    fclose( file );
    return inferred;
}

/* Returns the org.freedesktop.appearance color-scheme values: 0 = unavailable,
 * 1 = prefer dark, 2 = prefer light. The desktop portal is authoritative; the
 * toolkit setting is only used when the portal has no preference. */
unsigned int get_host_color_scheme(void)
{
    unsigned int scheme = get_portal_color_scheme();

    if (!scheme) scheme = get_toolkit_color_scheme();
    TRACE( "host color scheme %u\n", scheme );
    return scheme;
}
