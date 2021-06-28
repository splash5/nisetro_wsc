#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.

// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.

// #include <SDKDDKVer.h>
#include <winsdkver.h>

#ifndef WINVER
#define WINVER _WIN32_WINNT_WINXP		// Originally 0x0600
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WINXP	// Originally 0x0600
#endif 
