#include <dirent.h>
#include <dlfcn.h>

#ifdef ANDROID_COMPILATION
#define OSCL_EXPORT_REF __attribute__ ((visibility("default")))
#define OSCL_IMPORT_REF __attribute__ ((visibility("default")))
#endif
