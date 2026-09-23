#pragma once
// Common Windows include preamble used across the project.
#ifndef AUDIOWATCHDOG_WIN_H
#define AUDIOWATCHDOG_WIN_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <combaseapi.h>
#include <propsys.h>
#include <propkey.h>
#include <functiondiscoverykeys.h>

#include <objbase.h>

#endif // AUDIOWATCHDOG_WIN_H