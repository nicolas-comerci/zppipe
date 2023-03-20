# PZpipe
ZPAQ streaming and multithreaded compressor/decompressor

The idea is to have the piping capabilities of ZPIpe and the multithreading capabilities of PZpaq at the same time.
Multithreading is achieved by chunking the input stream into chunks of 10mb and spawning a thread to compress each chunk.

Pretty rudimentary for now, is just a retrofitted version of my ZPAQ compression support for Precomp, but as a standalone program.
If I continue to work on this I might do a more thorough rewrite/refactoring.

In any case, I've used it a bunch and seems to work decently. No warranties at all though, use at your own risk, would recommend decompressing any compressed stream and hash checking that you are getting your original data back.

It just uses compression level 2 (ZPAQ streaming format supports 1-3 levels, in my experience 1 is not worth it, you are usually better using fast-lzma or something like that, and 3 might be worth it if you are looking for maximum compression and don't care about runtime at all, 2 being a more reasonable compromise).

Compression level can easily be made a parameter though, might do it for a future version. The same goes for chunk size.

If you want to limit memory usage (which is probably necessary on 32bit as exceeding 3gb of mem usage will probably cause a crash) you can use the -l parameter, -l4 as far as I tested is safe for 32bit.

Usage
-----
`pzpipe myfile.bin`  compresses to myfile.bin.zpaq\
`pzpipe -osome_name myfile.bin`  compresses to some_name\
`pzpipe -ostdout myfile.bin > myfile.bin.zpaq` you can use stdout as output name to output to pipe\
`pzpipe -d myfile.bin.zpaq`  decompresses to original filename (myfile.bin)\
`pzpipe -d -t4 myfile.bin.zpaq`  idem, but limit to 4 threads, as previously stated, also useful for limiting memory usage\
`pzpipe -osome_name -d myfile.bin.zpaq`  decompresses to some_name\
`cat myfile.bin.zpaq - | pzpipe -osome_name -d stdin`  decompresses from stdin to some_name\
`(pzpipe -ostdout stdin < myfile.bin) | pzpipe -ostdout -d stdin > myfile2.bin`  pointless, but shows how pzpipe can do piping from stdin and stdout at the same time\
