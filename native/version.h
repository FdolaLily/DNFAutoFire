#pragma once
// Product version: the only place the EXE's version lives in source. client.rc (version
// resource), app_ids.h (kProductVersion) and client_ui.cpp (title bar / about) all use it.
// Written by scripts/bump-version.ps1 -- build.ps1 raises DAF_VERSION_BUILD automatically when
// the EXE sources change; do not edit by hand. Plain #defines so the resource compiler reads it.
// source-sha256: d3616fca42f7a26fb817e16c094da11682b6958b0ce461a5dc8629171a88a3c2
#define DAF_VERSION_MAJOR 0
#define DAF_VERSION_MINOR 3
#define DAF_VERSION_PATCH 0
#define DAF_VERSION_BUILD 6
#define DAF_VERSION_STRING "0.3.0.6"
#define DAF_VERSION_SHORT "0.3.0"
