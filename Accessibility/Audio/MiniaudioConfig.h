// Copyright (c) 2026- PPSSPP Accessibility Scripting contributors.
// GPL 2.0 or later. miniaudio itself is public domain / MIT-0.

#pragma once

// miniaudio settings. Every file that includes miniaudio.h must include this
// instead, because some settings change the layout of miniaudio's structs.

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI      // Same backend PPSSPP itself uses on Windows.
#define MA_NO_ENCODING        // We only play, never record or write files.

#ifdef _MSC_VER
#pragma warning(push, 0)  // Third-party code: don't flood the build log.
#endif
#include "Accessibility/ext/miniaudio/miniaudio.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
