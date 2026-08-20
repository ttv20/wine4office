/*
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/***********************************************************************
 *           RegisterAppConstrainedChangeNotification (twinapi.appcore.@)
 */
ULONG WINAPI RegisterAppConstrainedChangeNotification( PAPPCONSTRAIN_CHANGE_ROUTINE routine, void *context, PAPPCONSTRAIN_REGISTRATION *reg )
{
    if (reg) *reg = NULL;
    FIXME( "routine %p, context %p, reg %p - unsupported.\n", routine, context, reg );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI UnregisterAppConstrainedChangeNotification( PAPPCONSTRAIN_REGISTRATION reg )
{
    FIXME( "reg %p - unsupported.\n", reg );
}


HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

static BOOL hstring_equals( HSTRING string, const WCHAR *value )
{
    UINT32 length = WindowsGetStringLen( string );
    SIZE_T value_length = wcslen( value );

    return length == value_length && !memcmp( WindowsGetStringRawBuffer( string, NULL ), value,
            length * sizeof(*value) );
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;
    if (!classid) return CLASS_E_CLASSNOTAVAILABLE;

    if (hstring_equals( classid, RuntimeClass_Windows_Security_ExchangeActiveSyncProvisioning_EasClientDeviceInformation ))
        IActivationFactory_QueryInterface( client_device_information_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_System_Profile_AnalyticsInfo ))
        IActivationFactory_QueryInterface( analytics_info_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_System_Profile_EducationSettings ))
        IActivationFactory_QueryInterface( education_settings_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_System_Profile_RetailInfo ))
        IActivationFactory_QueryInterface( retail_info_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_System_UserProfile_AdvertisingManager ))
        IActivationFactory_QueryInterface( advertising_manager_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_UI_ViewManagement_ApplicationView ))
        IActivationFactory_QueryInterface( application_view_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_ApplicationModel_Core_CoreApplication ))
        IActivationFactory_QueryInterface( core_application_factory, &IID_IActivationFactory, (void **)factory );
    else if (hstring_equals( classid, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager ))
        IActivationFactory_QueryInterface( data_transfer_manager_statics_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
