#include <string>
#include <zlib.h>

class BasicZlib {
  public:
    std::string errToString(int ret) {
        switch (ret) {
#define _ZERRORCASEFORSTRING(ERROR) \
    case ERROR:                     \
        return #ERROR;              \
        break;
            _ZERRORCASEFORSTRING(Z_OK)
            _ZERRORCASEFORSTRING(Z_DATA_ERROR)
            _ZERRORCASEFORSTRING(Z_NEED_DICT)
            _ZERRORCASEFORSTRING(Z_MEM_ERROR)
            _ZERRORCASEFORSTRING(Z_BUF_ERROR)
            _ZERRORCASEFORSTRING(Z_STREAM_ERROR)
        default:
            return std::string{"Unknown Error: "} + std::to_string(ret);
        }
    }
};

class ZlibInflator : public BasicZlib {
    z_stream strm;

  public:
    typedef decltype(strm.avail_in) size_type; // use for Size of Buffer
    typedef decltype(strm.next_in) byteP;

    ZlibInflator() {
        initializeStrm();
        if ((LastRet = inflateInit(&strm)) != Z_OK)
            throw std::runtime_error{"inflate init Failed"};
    }
    explicit ZlibInflator(int WindowBits) {
        initializeStrm();
        if ((LastRet = inflateInit2(&strm, WindowBits)) != Z_OK)
            throw std::runtime_error{"inflateInit2 failed"};
    }
    ~ZlibInflator() { inflateEnd(&strm); }

    // ZlibInflator is not Trivally Copyable
    ZlibInflator(const ZlibInflator &) = delete;
    ZlibInflator(const ZlibInflator &&) = delete;
    ZlibInflator &operator=(const ZlibInflator &) = delete;
    ZlibInflator &operator=(const ZlibInflator &&) = delete;

    auto totalInputByte() { return strm.total_in; }
    auto totalOutputByte() { return strm.total_out; }
    auto avail_out() { return strm.avail_out; }
    auto avail_in() { return strm.avail_in; }

    // inflate->Src, and output in dest
    // after Operation if avail_out is zero, call continuePrev to continue
    // Operation return inflate() return value
    auto operator()(void *dest, size_type destlen, void *src, size_type srclen,
                    decltype(Z_SYNC_FLUSH) FlushType = Z_SYNC_FLUSH) {
        LastRet = inflateReset(&strm);
        if (LastRet != Z_OK)
            throw errToString(LastRet);
        strm.next_out = reinterpret_cast<byteP>(dest); // avoid compiler warnings
        strm.next_in = reinterpret_cast<byteP>(src);   // avoid compiler warnings
        strm.avail_in = LastSrcLen = srclen;
        strm.avail_out = LastDestLen = destlen;
        return LastRet = inflate(&strm, FlushType);
    }

    auto continuePrev(void *dest, size_type destlen,
                      decltype(Z_SYNC_FLUSH) FlushType = Z_SYNC_FLUSH) {
        strm.next_out = reinterpret_cast<byteP>(dest);
        strm.avail_out = LastDestLen = destlen;
        return LastRet = inflate(&strm, FlushType);
    }

    auto refillInput(void *src, size_type srcLen,
                     decltype(Z_SYNC_FLUSH) FlushType = Z_SYNC_FLUSH) {
        strm.next_in = reinterpret_cast<byteP>(src);
        strm.avail_in = srcLen;
        return LastRet = inflate(&strm, FlushType);
    }

    auto lastRetVal() { return LastRet; }

  private:
    size_type LastSrcLen = 0, LastDestLen = 0;
    int LastRet = Z_OK;
    void initializeStrm() {
        /* allocate inflate state */
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.avail_out = 0;
        strm.total_in = 0;
        strm.total_out = 0;
        strm.next_in = Z_NULL;
    }
};
