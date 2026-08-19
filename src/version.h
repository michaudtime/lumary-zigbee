#pragma once

// The single source of the firmware version. Deliberately free of ESP-IDF
// headers so it can be unit-tested on the host -- config.h pulls in
// driver/ledc.h and therefore cannot be included from a test.
//
// Bump this block as a unit for a release, and pass the same ZB_FW_VERSION to
// ota_image_tool.py as --file-version. It is the only number a human types
// twice, and README.md says so.

#define FW_VERSION_MAJOR  2
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  1
#define FW_DATE_CODE      "20260819"   // ZCL DateCode, YYYYMMDD

// The coordinator only offers images numbered above the running one.
#define ZB_FW_VERSION     ((FW_VERSION_MAJOR << 24) | (FW_VERSION_MINOR << 16) | (FW_VERSION_PATCH << 8))

// DownloadedFileVersion. Espressif's own Arduino OTA example uses running + 1,
// and Z2M decides whether to offer an update from the RUNNING FileVersion, so
// this value is not load-bearing. Kept identical to what the fixture already
// reports -- derived here only so it cannot drift from ZB_FW_VERSION.
#define ZB_FW_VERSION_DL  (ZB_FW_VERSION + 1)

// Two-step indirection is required: stringifying directly yields the macro
// name rather than its value.
#define FW_STR_(x)        #x
#define FW_STR(x)         FW_STR_(x)
#define FW_VERSION_STRING FW_STR(FW_VERSION_MAJOR) "." FW_STR(FW_VERSION_MINOR) "." FW_STR(FW_VERSION_PATCH)
