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
#include <http_protocol.h>

using namespace std;

NS_AHTSE_USE
NS_ICD_USE

extern module AP_MODULE_DECLARE_DATA tartiles_module;
#if defined(APLOG_USE_MODULE)
APLOG_USE_MODULE(tartiles);
#endif

struct conf_t {
    // Expressions to match for requests
    apr_array_header_t* arr_rxp;
    // Raster configuration
    TiledRaster raster;
    // Source path and prefix
    char* source;
    char* suffix;
    // Set if module is not to be accessible directly
    int indirect;
    // Max linear number of tiles to combine, default 4
    // This means max area of 16 tiles per request
    int maxtiles;
};

static void* create_dir_config(apr_pool_t *p, char* /* path */)
{
    conf_t* cfg = (conf_t*)apr_pcalloc(p, sizeof(conf_t));
    cfg->maxtiles = 4;
    return cfg;
}

struct aoi_t {
    uint64_t m,l, r, c, w, h;
};

static int get_bbox(request_rec *r, aoi_t& loc, int need_m = 0)
{
    // Parse the URL to get tile indices
    auto tokens = tokenize(r->pool, r->uri);
    // Need L/R/C/W/H or M/L/R/C/W/H
    if (tokens->nelts < 5 || (need_m && tokens->nelts < 6))
        return APR_BADARG;
    // From the end
    loc.h = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    loc.w = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    loc.c = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    loc.r = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    loc.l = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    loc.m = need_m ? apr_atoi64(ARRAY_POP(tokens, char*)) : 0;
    // M defaults to 0
    loc.m = errno ? 0 : loc.m;
    return OK;
}

// A 512 bytes posix tar header
struct ustar_header_t {
    char name[100];
    char mode[8];    // File mode
    char uid[8];     // User id
    char gid[8];     // Group id
    char size[12];    // actual length of file, it will get padded to next 512
    char mtime[12];   // modification time
    char sum[8];      // file and header checksum
    char typeflag; // Type of entry
    char link[100];   // Linked path or file name
    char sig[6];      // "ustar"
    char version[2];  // literal 00
    char uname[32];   // User name
    char gname[32];   // Group name
    char devmaj[8];   // Major device ID
    char devmin[8];   // Minor device ID
    char prefix[155]; // Path name, no trailing slashes
    char pad[12];     // To 512 bytes

    // Always a static file, no folder, with fixed fields
    // To be useful, fill the name, size and sum of header + file itself
    void init() {
        memset(this, 0, sizeof(*this));
        sprintf(mode, "0000644");
        sprintf(uid, "0001234");  // made up
        sprintf(gid, "0001234");  // made up
        // Skip the size
        sprintf(mtime, "15106450176"); // Sat Nov 15 23:38:03 2025 UTC
        memset(sum, ' ', 8); // Init with spaces before computing the sum
        typeflag = '0'; // Regular file
        sprintf(sig, "ustar"); // null terminated
        version[0] = version[1] = '0'; // Not terminated
        sprintf(uname, "user");
        sprintf(gname, "users");
        sprintf(devmaj, "0000000");
        sprintf(devmin, "0000000");
        // Prefix is always empty
    };
};

static int handler(request_rec *r)
{
    if (r->method_number != M_GET)
        return DECLINED;

    auto cfg = get_conf<conf_t>(r, &tartiles_module);
    if ((cfg->indirect && !r->main)
        || !requestMatches(r, cfg->arr_rxp))
        return DECLINED;

    // It is our request to handle
    auto p = r->pool;
    //ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "tartiles handler invoked for %s", r->uri);

    aoi_t aio;
    // No M for now
    int rc = get_bbox(r, aio);
    if (OK != rc) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r, "tartiles: invalid URL format %s", r->uri);
        return HTTP_BAD_REQUEST;
    }

    // Check the level is valid
    if (aio.l >= cfg->raster.n_levels)
        return HTTP_BAD_REQUEST;
    auto level = cfg->raster.rsets[aio.l];
    // Box has to be contained in the level
    if ((aio.r + aio.h >= level.h) || (aio.c + aio.w >= level.w))
        return HTTP_BAD_REQUEST;
    // Reject if too many tiles on either dimension
    if (aio.h > uint64_t(cfg->maxtiles) || aio.w > uint64_t(cfg->maxtiles))
        return HTTP_BAD_REQUEST;

    // Get the tiles, build simple ustar stream
    storage_manager tile_sm;
    // Can be set in the configuration file
    tile_sm.size = cfg->raster.maxtilesize;
    tile_sm.buffer = apr_palloc(p, tile_sm.size);

    sz5 tile;
    tile.z = aio.m; // Just in case
    tile.l = aio.l;
    // On the stack
    ustar_header_t tarheader = {};
    apr_size_t size = 0; // Sent bytes
    for (int y = int(aio.r); y < int(aio.r + aio.h); ++y) {
        for (int x = int(aio.c); x < int(aio.c + aio.w); ++x) {
            // Reset buffer size
            tile_sm.size = cfg->raster.maxtilesize;
            // returns APR_SUCCESS if all worked, otherwise error
            tile.x = x;
            tile.y = y;
            char* ETag; 
            auto result = get_remote_tile(r, cfg->source, tile, tile_sm, &ETag, cfg->suffix);
            // If a relocation is is returned and a location returned, chase the pointer
            if ((HTTP_MOVED_PERMANENTLY == result || HTTP_MOVED_TEMPORARILY == result)
                && ETag && ETag[0])
            {
                string local_url(ETag);
                // This normally contains protocol://host:port/path, where host should be localhost
                // We just need the /path part
                auto off = local_url.find("//"); // The ones after protocol
                if (off != string::npos)
                    off = local_url.find('/', off + 2); // The one before path
                if (off) {
                    // Reset the storage manager size
                    tile_sm.size = cfg->raster.maxtilesize;
                    result = get_response(r, local_url.c_str() + off, tile_sm);
                }
            }
            // This will skip input tiles that are too large for the buffer
            if (APR_SUCCESS != result || tile_sm.size == 0)
                continue; // Ignore errors, missing tiles or possibly input tiles are too big
            // Got a tile, size is in tile_sm.size
            if (size == 0) { // Sending some data, set the headers
                // Mime type is application/tar
                ap_set_content_type(r, "application/tar");
                // Add cors header by default. Can be overwritten by apache header module
                apr_table_setn(r->headers_out, "Access-Control-Allow-Origin", "*");
            }
            tarheader.init(); // Reset header
            // File name, esri tile style
            sprintf(tarheader.name, "L%02d/R%04xC%04x.jpg", int(tile.l), y, x);
            // Fill in the size, 12 octal chars, null terminated
            sprintf(tarheader.size, "%011o", int(tile_sm.size));

            // Update the checksum over the header
            // This is required if the output is to be read by standard tar tools
            uint32_t sum(0);
            auto v = (uint8_t*)&tarheader;
            for (int i = 0; i < int(sizeof(tarheader)); i++)
                sum += v[i];
            // Keep no more than 7 octal digits to avoid overflow
            sprintf(tarheader.sum, "%07o", sum & 07777777);

            ap_rwrite(&tarheader, int(sizeof(tarheader)), r);
            ap_rwrite(tile_sm.buffer, (int)tile_sm.size, r);
            size += 512 + tile_sm.size;
            // Zero padded to 512 bytes
            if (tile_sm.size % 512) {
                // zero pad to next 512 bytes boundary
                auto pad_size = int(512 - tile_sm.size % 512);
                // Use the buffer, there is enough space
                memset(tile_sm.buffer, 0, pad_size);
                ap_rwrite(tile_sm.buffer, pad_size, r);
                size += pad_size;
            }
        }
    }
    // If total size is still zero, there was no data to send
    if (size == 0)
        return HTTP_NOT_FOUND;
    // Done writing
    ap_rflush(r);
    return OK;
}

static const char* configure(cmd_parms* cmd, conf_t* c, const char* fname) {
    const char* err_message;
    apr_table_t* kvp = readAHTSEConfig(cmd->temp_pool, fname, &err_message);
    if (!kvp)
        return err_message;
    err_message = configRaster(cmd->pool, kvp, c->raster);
    if (err_message)
        return err_message;

    // Get the max number of tiles, if present
    const char* line;
    line = apr_table_get(kvp, "maxtiles");
    if (line) {
        // Set some reasonable limit, this is 1e6 tiles in a single request
        if (apr_atoi64(line) > 1024)
            return "maxtiles too large";
        if (apr_atoi64(line) == 0)
            return "maxtiles cannot be zero";
        c->maxtiles = int(apr_atoi64(line));
    }

    return NULL;
}

static void register_hooks(apr_pool_t *p) {
    // Needs to go somewhere, this will do
    static_assert(sizeof(ustar_header_t) == 512, "Header member alignment is wrong");
    ap_hook_handler(handler, nullptr, nullptr, APR_HOOK_MIDDLE);
}

static const command_rec cmds[] = {
    AP_INIT_TAKE1(
    "TarTiles_RegExp",
        (cmd_func) set_regexp<conf_t>,
        0,
        ACCESS_CONF,
        "Add a regular expression to match tile URLs"
    ),
    AP_INIT_TAKE1(
        "TarTiles_Indirect",
        (cmd_func) ap_set_flag_slot,
        (void*)APR_OFFSETOF(conf_t, indirect),
        ACCESS_CONF,
        "Set to 'On' to make the module inaccessible directly"
    ),
    AP_INIT_TAKE12(
        "TarTiles_Source",
        (cmd_func) set_source<conf_t>,
        0,
        ACCESS_CONF,
        "Set the source and suffix URL for tile retrieval"
    ),
    AP_INIT_TAKE1(
        "TarTiles_ConfigurationFile",
        (cmd_func) configure,
        0,
        ACCESS_CONF,
        "Raster configuration file"
    ),
    { nullptr }
};

module AP_MODULE_DECLARE_DATA tartiles_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,                     // create per-directory config structures
    nullptr,                     // merge per-directory config structures
    nullptr,                     // create per-server config structures
    nullptr,                     // merge per-server config structures
    cmds,                     // command table
    register_hooks,                     // register hooks
};
