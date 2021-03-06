// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <tchar.h>
#include <conio.h>
#include "libsodium/src/libsodium/include/sodium.h"
#endif

#ifdef _NIX
#include "win32emu/win32emu.h"
#include <sodium.h>
#endif

#include <malloc.h>
#include <locale.h>
#include <stdio.h>

#include <src/cairo.h>
#include "toolset/toolset.h"
#include "rsvg/rsvg.h"
#include "rasp.h"
#include "client.h"

