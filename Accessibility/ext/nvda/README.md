# NVDA Controller Client

## What this is

`x64/nvdaControllerClient.dll` is the NVDA Controller Client from NV Access, unmodified. It lets a program send speech and braille to the NVDA screen reader.

## Licence

The NVDA Controller Client is licensed under the GNU Lesser General Public License, version 2.1 (LGPL 2.1). It is shipped as a separate, unmodified DLL. PPSSPP loads it at runtime with `LoadLibrary`, so it can be replaced with any newer version without rebuilding PPSSPP.

## Where it comes from

Official downloads, named `nvda_<version>_controllerClient.zip`, are published by NV Access with each NVDA release: https://www.nvaccess.org/files/nvda/releases/

## Functions PPSSPP uses

- `nvdaController_testIfRunning`
- `nvdaController_speakText`
- `nvdaController_cancelSpeech`
- `nvdaController_brailleMessage`
