# AntiZ

AntiZ is a project to create an open source precompressor for lightly compressed data.(currently Zlib)
Zlib is very common, for example it is used in PDF, JAR, ZIP, PNG etc.
It is fast but it has a poor compression ratio, and it is usually not effective to use a stronger compression(eg. LZMA) on data that has been compressed with Zlib. It would be much better if the data was not compressed at all before the sron compression. For example:
   PDF file: 172KB---->compresses to 124KB with 7ZIP 9.38 beta (ultra preset)
   PDF file: 172KB---->expands to 745KB with AntiZ----->compresses to 104KB with 7ZIP

Of course this process is not trivial if you want to get back the original file, byte identical.
This project is inspiried by and aims to be a replacement for the long abandoned precomp project (non open source).
http://schnaader.info/precomp.php

AntiZ is currently in alpha stage, the ATZ1 file format is not very polished, has almost no integrity checksa and wastes space. There may also be bugs. I do not recommend using it on important data yet, especially since later versions will not support the ATZ1 file format.

AntiZ supports any file that contains a standard deflate stream, such as PDF, JAR, some PNGs and many others. Files that contain headerless or otherwise altered deflate streams(eg. ZIP) are currently not supported.

A number of people have contributed ideas, kind words, testing and code to the development of AntiZ:

   hxim (https://github.com/hxim)
   
   the encode.ru community (http://encode.ru/threads/2197-AntiZ-an-open-source-alternative-to-precomp)
   
Thank you!

USAGE:

   uncomp.exe  [--brute-window] [--notest] [-r] [--chunksize <integer>]
               [--mismatch-tol <integer>] [--shortcut-len <integer>]
               [--sizediff-tresh <integer>] [--recomp-tresh <integer>] [-o
               <string>] -i <string> [--] [--version] [-h]


Where:

   --brute-window
     Bruteforce deflate window size if there is a chance that recompression
     could be improved by it. This can have a major performance penalty.
     Default: disabled

   --notest
     Skip comparing the reconstructed file to the original at the end. This
     is not recommended, as AntiZ is still experimental software and my
     contain bugs that corrupt data.

   -r,  --reconstruct
     Assume the input file is an ATZ file and attempt to reconstruct the
     original file from it

   --chunksize <integer>
     Size of the memory buffer in bytes for chunked disk IO. This contorls
     memory usage to some extent, but memory usage control is not fully
     implemented yet. Smaller values result in more disk IO operations.
     Default: 524288

   --mismatch-tol <integer>
     Mismatch tolerance in bytes. If a set of parameters are found that
     give at most this many mismatches, then accept them and stop looking
     for a better set of parameters. Increasing this improves speed at the
     cost of more ATZ file overhead that may hurt compression. Default: 2
     Maximum: 65535

   --shortcut-len <integer>
     Length of the shortcut in bytes. If a stream is longer than the
     shortcut, then stop compression after <shortcut> compressed bytes have
     been obtained and compare this portion to the original. If this
     comparison yields more than recompTresh mismatches, then do not
     compress the entire stream. Lowering this improves speed, but it must
     be significantly greater than recompTresh or the speed benefit will
     decrease. Default: 512  Maximum: 65535

   --sizediff-tresh <integer>
     Size difference treshold in bytes. If the size difference between a
     recompressed stream and the original is more than the treshold then do
     not even compare them. Increasing this treshold increases the chance
     that a stream will be compared to the original. The cost of comparing
     is relatively low, so setting this equal to the recompression treshold
     should be fine. Default: 128  Maximum: 65535

   --recomp-tresh <integer>
     Recompression treshold in bytes. Streams are only recompressed if the
     best match differs from the original in at most recompTresh bytes.
     Increasing this treshold may allow more streams to be recompressed,
     but may increase ATZ file overhead and make it harder to compress.
     Default: 128  Maximum: 65535

   -o <string>,  --output <string>
     Output file name

   -i <string>,  --input <string>
     (required)  Input file name

   --,  --ignore_rest
     Ignores the rest of the labeled arguments following this flag.

   --version
     Displays version information and exits.

   -h,  --help
     Displays usage information and exits.
