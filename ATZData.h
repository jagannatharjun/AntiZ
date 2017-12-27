#ifndef _ATZDATA_HEADER
#define _ATZDATA_HEADER
namespace ATZdata {
struct programOptions {
    // parameters that some users may want to tweak
    uint_fast16_t recompTresh;     // streams are only recompressed if the best match
                                   // differs from the original in <= recompTresh
                                   // bytes
    uint_fast16_t sizediffTresh;   // streams are only compared when the size
                                   // difference is <= sizediffTresh
    uint_fast16_t shortcutLength;  // stop compression and count mismatches after
                                   // this many bytes, if we get more than
                                   // recompTresh then bail early
    uint_fast16_t mismatchTol;     // if there are at most this many mismatches
                                   // consider the stream a full match and stop
                                   // looking for better parameters
    bool bruteforceWindow = false; // bruteforce the zlib parameters, otherwise
                                   // only try probable parameters based on the
                                   // 2-byte header
    uint64_t
        chunksize; // the size of buffers used for file IO, controls memory usage

    // debug parameters, not useful for most users
    bool shortcutEnabled = true;   // enable speedup shortcut in phase 3
    int_fast64_t concentrate = -1; // only try to recompress the stream# givel
                                   // here, negative values disable this and run
                                   // on all streams, debug tool

    // command line switches
    bool recon;
    bool notest;
};
struct zlibParamPack {
    zlibParamPack() {}
    zlibParamPack(uint8_t c, uint8_t w, uint8_t m)
        : clevel(c), window(w), memlevel(m) {}
    uint8_t clevel, window, memlevel;
};
class streamOffset {
  public:
    streamOffset() =
        delete; // the default constructor should not be used in this version
    streamOffset(uint64_t os, int ot, uint64_t sl, uint64_t il) {
        offset = os;
        offsetType = ot;
        streamLength = sl;
        inflatedLength = il;
        zlibparams.clevel = 9;
        zlibparams.window = 15;
        zlibparams.memlevel = 9;
        identBytes = 0;
        firstDiffByte = -1;
        recomp = false;
        atzInfos = 0;
    }
    zlibParamPack zlibparams;
    uint64_t offset;
    int offsetType;
    uint64_t streamLength;
    uint64_t inflatedLength;
    uint64_t identBytes;
    int_fast64_t firstDiffByte;            // the offset of the first byte that does not
                                           // match, relative to stream start, not file start
    std::vector<uint64_t> diffByteOffsets; // offsets of bytes that differ, this
                                           // is an incremental offset list to
                                           // enhance recompression, kinda like a
                                           // PNG filter
    // this improves compression if the mismatching bytes are consecutive, eg.
    // 451,452,453,...(no repetitions, hard to compress)
    //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
    std::vector<uint8_t> diffByteVal;
    bool recomp;
    uint64_t atzInfos;
};
class fileOffset {
  public:
    fileOffset() =
        delete; // the default constructor should not be used in this version
    fileOffset(uint64_t os, int ot) {
        offset = os;
        offsetType = ot;
    }
    uint64_t offset;
    int offsetType;
};
} // namespace ATZdata
#endif