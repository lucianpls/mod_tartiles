# mod_tartiles
An Apache HTTPD module that packs multiple AHTSE tiles into a tar output

This module responds to a "tiles" request that contains a width and height in addition to the level, 
row and column of the top-left tile. The request format is ".../tiles/Level/Row/Column/Width/Height". 
The width and heigh are measured in tiles and have to be at least 1.
The return is a streamed tar file that contains the tiles with the 
"RrrrrCcccc.jpg" names, where rrrr and cccc are the row and column of 
the respective tile, represented in at least four lower case hex digits, 
zero prefixed if needed.

When deployed collocated or near a standard tile service, this module 
can improve the performance of GIS applications that needs to request
multiple tiles in a local area. It avoids the problems related to making multiple requests 
in parallel, and can decrease the server overhead.
While changes are needed in a client application to make use of this
type of response, the changes are only related to the fetching of data, the returned 
tiles can continue to be used as single JPEG tiles would be used.

The response is in the posix tar format, a concatenation of the JPEG tiles padded to the next multiple of 512 bytes, 
each tile prefixed by a 512 byte header that starts with the file name. This format was chosen because it can be 
streamed by the server, tile at a time, without having to know the full content. Unpacking and JPEG use can also 
start as soon as some data is received, before the server completes the response.

The tartiles module allows the server configuration to specify what is the maximum value for 
width and height, avoiding very large tile requests.
