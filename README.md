# mod_tartiles
An Apache HTTPD module that packs multiple AHTSE tiles into a tar output

This module responds to a "tiles" request that contains a width and height in addition to the level, 
row and column of the top-left tile. The request format is ".../tiles/Level/Row/Column/Width/Height". 
The width and heigh are measured in tiles and have to be at least 1.
The return is a streamed tar file that contains the tiles with the 
"Lll/RrrrrCcccc.jpg" names, where ll is the level number as two decimal digits, zero prefixed if
needed, rrrr and cccc are the row and column of the respective tile, represented in at least four 
lower case hex digits, zero prefixed if needed. The tile order is not specified or guaranteed, clients
should not rely on any specific order. File names that do not match this format might be added in 
the future, as needed.

When deployed collocated or near a standard tile service, this module 
can improve the performance of GIS applications that needs to request
multiple tiles in a local area. It avoids the problems related to making multiple requests 
in parallel, and can decrease the server overhead. For example, a request of 4x4 tiles
will require one HTTP request and return up to 16 tiles in a single response.
If no tiles are found in the requested area, a 404 NOT_FOUND response is returned.
Missing tiles or errors retrieving specific tiles will not cause the entire request to fail,
the corresponding tiles will simply be missing from the tar file.
While changes are needed in a client application to make use of this
type of response, the changes are only related to the fetching of data, the returned 
tiles can continue to be used as single JPEG tiles would be used.

The response is in the posix tar format, a concatenation of the JPEG tiles padded to the 
next multiple of 512 bytes, each tile prefixed by a 512 byte header that starts with the 
file name. This format was chosen because it can be streamed by the server, tile at a time, 
without having to know the full content. Unpacking and JPEG use can also start as soon as 
some data is received, before the server completes the response.

The tartiles module allows the server configuration to specify what is the maximum 
value for width and height, avoiding very large tile requests.

The intent is to use this type of request as a streaming optimization. The tar format 
is not supposed to be saved or cached, it is only a transport format that can be used
for debugging. Future version of this module may support other formats.

Note that reading and parsing the output tar file is as simple as reading the tile 
name from the first few bytes, reading the ascii octal size at offset 124 and then 
reading that many bytes from offset 512. The next header will be located at the next 
multiple of 512 bytes after that.

The output tar stream is not compressed. If compression is needed, mod_deflate or 
similar Apache modules can be used. Since tar headers are rather verbose and the JPEG 
headers are repeated, such compression can be effective, especially for many small tiles.

## Apache Configuration Directives

* *TarTiles_RegExp* match  
May appear multiple times, pattern to activate the module. Must end with /tiles/Level/Row/Column/Width/Height 
where tiles is literal

* *TarTiles_Indirect* on|off  
If set, the module activates only on subrequests. Default is off.

* *TarTiles_Source* Redirect_path suffix  
The source for the tile data, where Redirect_path is the path prefix to be replaced. Suffix is optional, literal
string to append after Level/Row/Column

* *TarTiles_ConfigurationFile* path
Path to an AHTSE configuration file describing the raster source to use

## AHTSE Configuration File Directives
In addition to the standard AHTSE raster configuration directives, the following are supported:

* *MaxTileSize* n  
The maximum input tile size expected, in bytes. Default is 4MB. Tiles larger than this will fail.

* *MaxTiles* n  
The maximum value for width and height in the tiles request, default is 4 (16 tiles total). Requests exceeding this will fail.
Maximum possible value is hardcoded to 1024 (1,048,576 tiles total)

