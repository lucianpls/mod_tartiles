# mod_tartiles
An Apache HTTPD module that packs multiple AHTSE tiles into a tar output

This module responds to a "tiles" request that contains a width and height in addition to the level, row and column of the top-left tile. The request format is ".../tiles/Level/Row/Column/Width/Height". The width and heigh are measured in tiles and have to be at least 1.
The return is a streamed tar file that contains the tiles with the 
"RrrrrCcccc.jpg" names, where rrrr and cccc are the row and column of 
the respective tile, represented in at least four lower case hex digits, 
zero prefixed if needed.

When deployed collocated or near a standard tile service, this module 
can improve the performance of GIS applications that needs to request
multiple tiles in a local area. It avoids the problems related to making multiple requests in parallel, and can decrease the server overhead also.