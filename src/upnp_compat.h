/* upnp_compat.h - libupnp v1.8.x/v1.6.x compatibilty layer
 *
 * Copyright (C) 2019 Tucker Kern
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#ifndef _UPNP_COMPAT_H
#define _UPNP_COMPAT_H

#include <upnp.h>
#include <UpnpString.h>

#if UPNP_VERSION >= 11000
#define UpnpAddVirtualDir(x) UpnpAddVirtualDir(x, NULL, NULL)
#define VD_GET_INFO_CALLBACK(NAME, FILENAME, INFO, COOKIE) int NAME(const char* FILENAME, UpnpFileInfo* INFO, const void* COOKIE, const void ** REQUEST_COOKIE)
#define VD_OPEN_CALLBACK(NAME, FILENAME, MODE, COOKIE) UpnpWebFileHandle NAME(const char* FILENAME, enum UpnpOpenFileMode MODE, const void* COOKIE, const void * REQUEST_COOKIE)
#define VD_READ_CALLBACK(NAME, HANDLE, BUFFER, LENGTH, COOKIE) int NAME(UpnpWebFileHandle HANDLE, char* BUFFER, size_t LENGTH, const void* COOKIE, const void * REQUEST_COOKIE)
#define VD_WRITE_CALLBACK(...) VD_READ_CALLBACK(__VA_ARGS__)
#define VD_SEEK_CALLBACK(NAME, HANDLE, OFFSET, ORIGIN, COOKIE) int NAME(UpnpWebFileHandle HANDLE, off_t OFFSET, int ORIGIN, const void* COOKIE, const void * REQUEST_COOKIE)
#define VD_CLOSE_CALLBACK(NAME, HANDLE, COOKIE) int NAME(UpnpWebFileHandle HANDLE, const void* COOKIE, const void * REQUEST_COOKIE)
#elif UPNP_VERSION >= 10803
#define UpnpAddVirtualDir(x) UpnpAddVirtualDir(x, NULL, NULL)
#define VD_GET_INFO_CALLBACK(NAME, FILENAME, INFO, COOKIE) int NAME(const char* FILENAME, UpnpFileInfo* INFO, const void* COOKIE)
#define VD_OPEN_CALLBACK(NAME, FILENAME, MODE, COOKIE) UpnpWebFileHandle NAME(const char* FILENAME, enum UpnpOpenFileMode MODE, const void* COOKIE)
#define VD_READ_CALLBACK(NAME, HANDLE, BUFFER, LENGTH, COOKIE) int NAME(UpnpWebFileHandle HANDLE, char* BUFFER, size_t LENGTH, const void* COOKIE)
#define VD_WRITE_CALLBACK(...) VD_READ_CALLBACK(__VA_ARGS__)
#define VD_SEEK_CALLBACK(NAME, HANDLE, OFFSET, ORIGIN, COOKIE) int NAME(UpnpWebFileHandle HANDLE, off_t OFFSET, int ORIGIN, const void* COOKIE)
#define VD_CLOSE_CALLBACK(NAME, HANDLE, COOKIE) int NAME(UpnpWebFileHandle HANDLE, const void* COOKIE)
#else
#define VD_GET_INFO_CALLBACK(NAME, FILENAME, INFO, COOKIE) int NAME(const char* FILENAME, UpnpFileInfo* INFO)
#define VD_OPEN_CALLBACK(NAME, FILENAME, MODE, COOKIE) UpnpWebFileHandle NAME(const char* FILENAME, enum UpnpOpenFileMode MODE)
#define VD_READ_CALLBACK(NAME, HANDLE, BUFFER, LENGTH, COOKIE) int NAME(UpnpWebFileHandle HANDLE, char* BUFFER, size_t LENGTH)
#define VD_WRITE_CALLBACK(...) VD_READ_CALLBACK(__VA_ARGS__)
#define VD_SEEK_CALLBACK(NAME, HANDLE, OFFSET, ORIGIN, COOKIE) int NAME(UpnpWebFileHandle HANDLE, off_t OFFSET, int ORIGIN)
#define VD_CLOSE_CALLBACK(NAME, HANDLE, COOKIE) int NAME(UpnpWebFileHandle HANDLE)
#endif

#if UPNP_VERSION >= 10800 && UPNP_VERSION < 11426
#define UPNP_CALLBACK(NAME, TYPE, EVENT, COOKIE) int NAME(Upnp_EventType TYPE, const void* EVENT, void* COOKIE)
#else
#define UPNP_CALLBACK(NAME, TYPE, EVENT, COOKIE) int NAME(Upnp_EventType TYPE, void* EVENT, void* COOKIE)
#endif

#if UPNP_VERSION < 10626
// Compatibility defines from libupnp 1.6.26 to allow code targeting v1.8.x
// to compile for v1.6.x

/* compat code for libupnp-1.8 */
typedef struct Upnp_Action_Request UpnpActionRequest;
#define UpnpActionRequest_get_ErrCode(x) ((x)->ErrCode)
#define UpnpActionRequest_set_ErrCode(x, v) ((x)->ErrCode = (v))
#define UpnpActionRequest_get_Socket(x) ((x)->Socket)
#define UpnpActionRequest_get_ErrStr_cstr(x) ((x)->ErrStr)
#define UpnpActionRequest_set_ErrStr(x, v) (strncpy((x)->ErrStr, UpnpString_get_String((v)), LINE_SIZE))
#define UpnpActionRequest_get_ActionName_cstr(x) ((x)->ActionName)
#define UpnpActionRequest_get_DevUDN_cstr(x) ((x)->DevUDN)
#define UpnpActionRequest_get_ServiceID_cstr(x) ((x)->ServiceID)
#define UpnpActionRequest_get_ActionRequest(x) ((x)->ActionRequest)
#define UpnpActionRequest_set_ActionRequest(x, v) ((x)->ActionRequest = (v))
#define UpnpActionRequest_get_ActionResult(x) ((x)->ActionResult)
#define UpnpActionRequest_set_ActionResult(x, v) ((x)->ActionResult = (v))

/* compat code for libupnp-1.8 */
typedef struct Upnp_Action_Complete UpnpActionComplete;
#define UpnpActionComplete_get_ErrCode(x) ((x)->ErrCode)
#define UpnpActionComplete_get_CtrlUrl_cstr(x) ((x)->CtrlUrl)
#define UpnpActionComplete_get_ActionRequest(x) ((x)->ActionRequest)
#define UpnpActionComplete_get_ActionResult(x) ((x)->ActionResult)

/* compat code for libupnp-1.8 */
typedef struct Upnp_State_Var_Request UpnpStateVarRequest;
#define UpnpStateVarRequest_get_ErrCode(x) ((x)->ErrCode)
#define UpnpStateVarRequest_set_ErrCode(x, v) ((x)->ErrCode = (v))
#define UpnpStateVarRequest_get_Socket(x) ((x)->Socket)
#define UpnpStateVarRequest_get_ErrStr_cstr(x) ((x)->ErrStr)
#define UpnpStateVarRequest_get_DevUDN_cstr(x) ((x)->DevUDN)
#define UpnpStateVarRequest_get_ServiceID_cstr(x) ((x)->ServiceID)
#define UpnpStateVarRequest_get_StateVarName_cstr(x) ((x)->StateVarName)
#define UpnpStateVarRequest_get_CurrentVal(x) ((x)->CurrentVal)
#define UpnpStateVarRequest_set_CurrentVal(x, v) ((x)->CurrentVal = (v))

/* compat code for libupnp-1.8 */
typedef struct Upnp_State_Var_Complete UpnpStateVarComplete;
#define UpnpStateVarComplete_get_ErrCode(x) ((x)->ErrCode)
#define UpnpStateVarComplete_get_CtrlUrl_cstr(x) ((x)->CtrlUrl)
#define UpnpStateVarComplete_get_StateVarName_cstr(x) ((x)->StateVarName)

/* compat code for libupnp-1.8 */
typedef struct Upnp_Event UpnpEvent;
#define UpnpEvent_get_SID_cstr(x) ((x)->Sid)
#define UpnpEvent_get_EventKey(x) ((x)->EventKey)
#define UpnpEvent_get_ChangedVariables(x) ((x)->ChangedVariables)

/* compat code for libupnp-1.8 */
typedef struct Upnp_Discovery UpnpDiscovery;
#define UpnpDiscovery_get_ErrCode(x) ((x)->ErrCode)
#define UpnpDiscovery_get_Expires(x) ((x)->Expires)
#define UpnpDiscovery_get_DeviceID_cstr(x) ((x)->DeviceId)
#define UpnpDiscovery_get_DeviceType_cstr(x) ((x)->DeviceType)
#define UpnpDiscovery_get_ServiceType_cstr(x) ((x)->ServiceType)
#define UpnpDiscovery_get_ServiceVer_cstr(x) ((x)->ServiceVer)
#define UpnpDiscovery_get_Location_cstr(x) ((x)->Location)
#define UpnpDiscovery_get_Os_cstr(x) ((x)->Os)
#define UpnpDiscovery_get_Date_cstr(x) ((x)->Date)
#define UpnpDiscovery_get_Ext_cstr(x) ((x)->Ext)

/* compat code for libupnp-1.8 */
typedef struct Upnp_Event_Subscribe UpnpEventSubscribe;
#define UpnpEventSubscribe_get_SID_cstr(x) ((x)->Sid)
#define UpnpEventSubscribe_get_ErrCode(x) ((x)->ErrCode)
#define UpnpEventSubscribe_get_PublisherUrl_cstr(x) ((x)->PublisherUrl)
#define UpnpEventSubscribe_get_TimeOut(x) ((x)->TimeOut)

/* compat code for libupnp-1.8 */
typedef struct Upnp_Subscription_Request UpnpSubscriptionRequest;
#define UpnpSubscriptionRequest_get_ServiceId_cstr(x) ((x)->ServiceId)
#define UpnpSubscriptionRequest_get_UDN_cstr(x) ((x)->UDN)
#define UpnpSubscriptionRequest_get_SID_cstr(x) ((x)->Sid)

/* compat code for libupnp-1.8 */
typedef struct File_Info UpnpFileInfo;
#define UpnpFileInfo_get_FileLength(x) ((x)->file_length)
#define UpnpFileInfo_set_FileLength(x, v) ((x)->file_length = (v))
#define UpnpFileInfo_get_LastModified(x) ((x)->last_modified)
#define UpnpFileInfo_set_LastModified(x, v) ((x)->last_modified = (v))
#define UpnpFileInfo_get_IsDirectory(x) ((x)->is_directory)
#define UpnpFileInfo_set_IsDirectory(x, v) ((x)->is_directory = (v))
#define UpnpFileInfo_get_IsReadable(x) ((x)->is_readable)
#define UpnpFileInfo_set_IsReadable(x, v) ((x)->is_readable = (v))
#define UpnpFileInfo_get_ContentType(x) ((x)->content_type)
#define UpnpFileInfo_set_ContentType(x, v) ((x)->content_type = (v))

#endif

#endif /* _UPNP_COMPAT_H */
