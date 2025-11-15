/*
 * An AHTSE module that combines multiple tiles into a single tar file stream.
 * 
 * Lucian Plesea
 * (C) 2025
 * 
 */

#include <ahtse.h>
#include <receive_context.h>
#include <http_log.h>
#include <http_request.h>

using namespace std;

NS_AHTSE_USE

extern module AP_MODULE_DECLARE_DATA tartiles_module;

#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(tartiles);
#endif

module AP_MODULE_DECLARE_DATA tartiles_module = {
    STANDARD20_MODULE_STUFF,
    nullptr,                     // create per-directory config structures
    nullptr,                     // merge per-directory config structures
    nullptr,                     // create per-server config structures
    nullptr,                     // merge per-server config structures
    nullptr,                     // command table
    nullptr,                     // register hooks
};
