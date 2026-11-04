#include "version.h"

// set by git_version.py
#ifndef PAGERCALL_GIT_REV
#define PAGERCALL_GIT_REV     "unknown"
#endif
#ifndef PAGERCALL_BUILD_TIME
#define PAGERCALL_BUILD_TIME  "unknown"
#endif

const char pagercall_git_rev[]    = PAGERCALL_GIT_REV;
const char pagercall_build_time[] = PAGERCALL_BUILD_TIME;
