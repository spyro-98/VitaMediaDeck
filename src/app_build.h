#ifndef VITAMEDIADECK_APP_BUILD_H
#define VITAMEDIADECK_APP_BUILD_H
/* CMake supplies a validated identity for native test builds. Host utilities
 * and untagged consumers retain the historical default paths. */
#ifndef VT_BUILD_TEST_ID
#define VT_BUILD_TEST_ID "standard"
#endif
#ifndef VT_BUILD_NAME
#define VT_BUILD_NAME "VitaMediaDeck"
#endif
#ifndef VT_BUILD_VERSION
#define VT_BUILD_VERSION "1.4.3"
#endif
#ifndef VT_BUILD_LOG_FILE
#define VT_BUILD_LOG_FILE "session_log.txt"
#endif
#ifndef VT_BUILD_SW_UPLOAD
#define VT_BUILD_SW_UPLOAD "NV12_P2"
#endif
#endif
