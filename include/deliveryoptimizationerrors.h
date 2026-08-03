/*
 * Delivery Optimization error codes
 *
 * Copyright 2026 Wine4Office project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_DELIVERYOPTIMIZATIONERRORS_H
#define __WINE_DELIVERYOPTIMIZATIONERRORS_H

#define DO_E_NO_SERVICE                          _HRESULT_TYPEDEF_(0x80d01001)
#define DO_E_DOWNLOAD_NO_PROGRESS                _HRESULT_TYPEDEF_(0x80d02002)
#define DO_E_JOB_NOT_FOUND                       _HRESULT_TYPEDEF_(0x80d02003)
#define DO_E_JOB_EMPTY                           _HRESULT_TYPEDEF_(0x80d02004)
#define DO_E_NO_DOWNLOADS                        _HRESULT_TYPEDEF_(0x80d02005)
#define DO_E_FILE_NOT_AVAILABLE                  _HRESULT_TYPEDEF_(0x80d02010)
#define DO_E_UNKNOWN_PROPERTY_ID                 _HRESULT_TYPEDEF_(0x80d02011)
#define DO_E_READ_ONLY_PROPERTY                  _HRESULT_TYPEDEF_(0x80d02012)
#define DO_E_INVALID_STATE                       _HRESULT_TYPEDEF_(0x80d02013)
#define DO_E_ERROR_INFORMATION_UNAVAILABLE       _HRESULT_TYPEDEF_(0x80d02014)
#define DO_E_WRITE_ONLY_PROPERTY                 _HRESULT_TYPEDEF_(0x80d02015)
#define DO_E_INTEGRITYCHECKINFO_UNSPECIFIED      _HRESULT_TYPEDEF_(0x80d02016)
#define DO_E_INTEGRITYCHECKINFO_UNAVAILABLE      _HRESULT_TYPEDEF_(0x80d02017)
#define DO_E_FILE_DOWNLOADSINK_UNSPECIFIED       _HRESULT_TYPEDEF_(0x80d02018)
#define DO_E_FILE_DOWNLOADSINK_ALREADY_SET       _HRESULT_TYPEDEF_(0x80d02019)
#define DO_E_FILE_SIZE_UNKNOWN_HTTP_200          _HRESULT_TYPEDEF_(0x80d0201a)
#define DO_E_FILE_ENCRYPTION_EXPECTED            _HRESULT_TYPEDEF_(0x80d0201b)
#define DO_E_FILE_SIZE_UNKNOWN_HTTP_206          _HRESULT_TYPEDEF_(0x80d0201c)
#define DO_E_FILE_SIZE_UNKNOWN_HTTP_2XX          _HRESULT_TYPEDEF_(0x80d0201d)
#define DO_E_INVALID_RANGE                       _HRESULT_TYPEDEF_(0x80d05010)
#define DO_E_INSUFFICIENT_RANGE_SUPPORT          _HRESULT_TYPEDEF_(0x80d05011)
#define DO_E_OVERLAPPING_RANGES                  _HRESULT_TYPEDEF_(0x80d05012)

#endif /* __WINE_DELIVERYOPTIMIZATIONERRORS_H */
