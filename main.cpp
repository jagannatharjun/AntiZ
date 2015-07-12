#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdio>
#include <string>
#include <iomanip>
#include <zlib.h>
#include <tclap/CmdLine.h>

//parameters that some users may want to tweak
uint_fast16_t recompTresh;//streams are only recompressed if the best match differs from the original in <= recompTresh bytes
uint_fast16_t sizediffTresh;//streams are only compared when the size difference is <= sizediffTresh
uint_fast16_t shortcutLength;//stop compression and count mismatches after this many bytes, if we get more than recompTresh then bail early
uint_fast16_t mismatchTol;//if there are at most this many mismatches consider the stream a full match and stop looking for better parameters
bool bruteforceWindow=false;//bruteforce the zlib parameters, otherwise only try probable parameters based on the 2-byte header

//debug parameters, not useful for most users
bool shortcutEnabled=true;//enable speedup shortcut in slow mode
int_fast64_t concentrate=-1;//only try to recompress the stream# givel here, negative values disable this and run on all streams, debug tool

//filenames and command line switches
std::string infile_name;
std::string reconfile_name;
std::string atzfile_name;
bool recon;
bool notest;

class fileOffset{
public:
    fileOffset(){
        abort();//the default constructor should not be used in this version
    }
    fileOffset(uint64_t os, int ot){
        offset=os;
        offsetType=ot;
    }
    uint64_t offset;
    int offsetType;
};

class streamOffset{
public:
    streamOffset(){
        abort();//the default constructor should not be used in this version
    }
    streamOffset(uint64_t os, int ot, uint64_t sl, uint64_t il){
        offset=os;
        offsetType=ot;
        streamLength=sl;
        inflatedLength=il;
        clevel=9;
        window=15;
        memlvl=9;
        identBytes=0;
        firstDiffByte=-1;
        recomp=false;
        atzInfos=0;
    }
    ~streamOffset(){
        diffByteOffsets.clear();
        diffByteOffsets.shrink_to_fit();
        diffByteVal.clear();
        diffByteVal.shrink_to_fit();
    }
    uint64_t offset;
    int offsetType;
    uint64_t streamLength;
    uint64_t inflatedLength;
    uint8_t clevel;
    uint8_t window;
    uint8_t memlvl;
    uint64_t identBytes;
    int_fast64_t firstDiffByte;//the offset of the first byte that does not match, relative to stream start, not file start
    std::vector<uint64_t> diffByteOffsets;//offsets of bytes that differ, this is an incremental offset list to enhance recompression, kinda like a PNG filter
    //this improves compression if the mismatching bytes are consecutive, eg. 451,452,453,...(no repetitions, hard to compress)
    //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
    std::vector<uint8_t> diffByteVal;
    bool recomp;
    unsigned char* atzInfos;
};

inline void pauser();
void parseCLI(int argc, char* argv[]);
void searchBuffer(unsigned char buffer[], std::vector<fileOffset>& offsets, uint_fast64_t buffLen);
bool CheckOffset(unsigned char *next_in, uint64_t avail_in, uint64_t& total_in, uint64_t& total_out);
void testOffsetList(unsigned char buffer[], uint64_t bufflen, std::vector<fileOffset>& fileoffsets, std::vector<streamOffset>& streamoffsets);
int parseOffsetType(int header);
void doDeflate(unsigned char* next_in, uint64_t avail_in, unsigned char*& next_out, uint_fast8_t clvl, uint_fast8_t window, uint_fast8_t memlvl, uint64_t& total_in, uint64_t& total_out);
int doInflate(unsigned char* next_in, uint64_t avail_in, unsigned char* next_out, uint64_t avail_out);
bool testDeflateParams(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel, uint8_t window, uint8_t memlevel);
void findDeflateParams(unsigned char rBuffer[], std::vector<streamOffset>& streamOffsetList);
inline bool testParamRange(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel_min, uint8_t clevel_max, uint8_t window_min, uint8_t window_max, uint8_t memlevel_min, uint8_t memlevel_max);

void parseCLI(int argc, char* argv[]){
    // Wrap everything in a try block.  Do this every time,
	// because exceptions will be thrown for problems.
	try{
        // Define the command line object.
        TCLAP::CmdLine cmd("Visit https://github.com/Diazonium/AntiZ for source code and support.", ' ', "0.1.4-git");

        // Define a value argument and add it to the command line. This defines the input file.
        TCLAP::ValueArg<std::string> infileArg("i", "input", "Input file name", true, "", "string");
        cmd.add(infileArg);

        // Define the output file. This is optional, if not provided then it will be generated from the input file name.
        TCLAP::ValueArg<std::string> outfileArg("o", "output", "Output file name", false, "", "string");
        cmd.add(outfileArg);

        TCLAP::ValueArg<uint_fast16_t> recomptreshArg("", "recomp-tresh", "Recompression treshold in bytes. Streams are only recompressed if the best match differs from the original in at most recompTresh bytes. Increasing this treshold may allow more streams to be recompressed, but may increase ATZ file overhead and make it harder to compress. Default: 128  Maximum: 65535", false, 128, "integer");
        cmd.add(recomptreshArg);
        TCLAP::ValueArg<uint_fast16_t> sizedifftreshArg("", "sizediff-tresh", "Size difference treshold in bytes. If the size difference between a recompressed stream and the original is more than the treshold then do not even compare them. Increasing this treshold increases the chance that a stream will be compared to the original. The cost of comparing is relatively low, so setting this equal to the recompression treshold should be fine. Default: 128  Maximum: 65535", false, 128, "integer");
        cmd.add(sizedifftreshArg);
        TCLAP::ValueArg<uint_fast16_t> shortcutlenArg("", "shortcut-len", "Length of the shortcut in bytes. If a stream is longer than the shortcut, then stop compression after <shortcut> compressed bytes have been obtained and compare this portion to the original. If this comparison yields more than recompTresh mismatches, then do not compress the entire stream. Lowering this improves speed, but it must be significantly greater than recompTresh or the speed benefit will decrease. Default: 512  Maximum: 65535", false, 512, "integer");
        cmd.add(shortcutlenArg);
        TCLAP::ValueArg<uint_fast16_t> mismatchtolArg("", "mismatch-tol", "Mismatch tolerance in bytes. If a set of parameters are found that give at most this many mismatches, then accept them and stop looking for a better set of parameters. Increasing this improves speed at the cost of more ATZ file overhead that may hurt compression. Default: 2  Maximum: 65535", false, 2, "integer");
        cmd.add(mismatchtolArg);

        // Define a switch and add it to the command line.
        TCLAP::SwitchArg reconSwitch("r", "reconstruct", "Assume the input file is an ATZ file and attempt to reconstruct the original file from it", false);
        cmd.add(reconSwitch);
        TCLAP::SwitchArg notestSwitch("", "notest", "Skip comparing the reconstructed file to the original at the end. This is not recommended, as AntiZ is still experimental software and my contain bugs that corrupt data.", false);
        cmd.add(notestSwitch);
        TCLAP::SwitchArg brutewindowSwitch("", "brute-window", "Bruteforce deflate window size if there is a chance that recompression could be improved by it. This can have a major performance penalty. Default: disabled", false);
        cmd.add(brutewindowSwitch);

        // Parse the args.
        cmd.parse( argc, argv );

        std::cout<<"Input file: "<<infileArg.getValue()<<std::endl;
        //use the parameters we get
        recompTresh= recomptreshArg.getValue();
        sizediffTresh= sizedifftreshArg.getValue();
        shortcutLength= shortcutlenArg.getValue();
        mismatchTol= mismatchtolArg.getValue();
        bruteforceWindow= brutewindowSwitch.getValue();

        recon = reconSwitch.getValue();//check if we need to reconstruct only
        notest= notestSwitch.getValue();
        if (recon){
            std::cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<std::endl;
            atzfile_name= infileArg.getValue();
            if (outfileArg.isSet()){//if the output is specified use that
                reconfile_name= outfileArg.getValue();
            }else{//if not, append .rec to the input file name
                reconfile_name= atzfile_name;
                reconfile_name.append(".rec");
            }
            std::cout<<"overwriting "<<reconfile_name<<" if present"<<std::endl;
        }else{
            infile_name= infileArg.getValue();
            if (outfileArg.isSet()){//if the output is specified use that
                atzfile_name= outfileArg.getValue();
            }else{//if not, append .atz to the input file name
                atzfile_name= infile_name;
                atzfile_name.append(".atz");
            }
            reconfile_name= infile_name;
            reconfile_name.append(".rec");
            std::cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<std::endl;
        }
	} catch (TCLAP::ArgException &e){  // catch any exceptions
        std::cout << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    }
}

inline void pauser(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}

inline bool testParamRange(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel_min, uint8_t clevel_max, uint8_t window_min, uint8_t window_max, uint8_t memlevel_min, uint8_t memlevel_max){
    //this function tests a given range of deflate parameters
    uint8_t clevel, memlevel, window;
    bool fullmatch;
    for(window=window_max; window>=window_min; window--){
        for(memlevel=memlevel_max; memlevel>=memlevel_min; memlevel--){
            for(clevel=clevel_max; clevel>=clevel_min; clevel--){
                fullmatch=testDeflateParams(origbuff, decompbuff, offsets, offsetno, clevel, window, memlevel);
                if (fullmatch){
                    #ifdef debug
                    std::cout<<"   recompression succesful within tolerance, bailing"<<std::endl;
                    #endif // debug
                    return true;
                }
            }
        }
    }
    return false;
}

void findDeflateParams(unsigned char rBuffer[], std::vector<streamOffset>& streamOffsetList){
    //this function takes a buffer and a vector containing information about the valid zlib streams in the buffer
    //it tries to find the best parameters for recompression, the results are stored in the vector
    uint64_t j;
    uint64_t numOffsets=streamOffsetList.size();
    for (j=0; j<numOffsets; j++){
        if ((concentrate>=0)&&(j==0)) {
            j=concentrate;
            numOffsets=concentrate;
        }
        //a buffer needs to be created to hold the resulting decompressed data
        //since we have already deompressed the data before, we know exactly how large of a buffer we need to allocate
        //the lengths of the zlib streams have been saved by the previous phase
        unsigned char* decompBuffer= new unsigned char[streamOffsetList[j].inflatedLength];
        int ret=doInflate((rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength, decompBuffer, streamOffsetList[j].inflatedLength);
        //check the return value
        switch (ret){
            case Z_STREAM_END: //decompression was succesful
            {
                uint8_t window= 10 + (streamOffsetList[j].offsetType / 4);
                uint8_t crange = streamOffsetList[j].offsetType % 4;
                #ifdef debug
                std::cout<<std::endl;
                std::cout<<"stream #"<<j<<"("<<streamOffsetList[j].offset<<")"<<" ready for recompression trials"<<std::endl;
                std::cout<<"   stream type: "<<streamOffsetList[j].offsetType<<std::endl;
                std::cout<<"   window and crange from header: "<<+window<<" ; "<<+crange<<std::endl;
                #endif // debug
                //try the most probable parameters first(supplied by header or default)
                switch (crange){//we need to switch based on the clevel
                    case 0:{//if the header signals fastest compression try clevel 1, header-supplied window and default memlvl(8)
                        #ifdef debug
                        std::cout<<"   trying most probable parameters: fastest compression"<<std::endl;
                        #endif // debug
                        if (testDeflateParams(rBuffer, decompBuffer, streamOffsetList, j, 1, window, 8)) break;
                        //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                        #ifdef debug
                        std::cout<<"   trying less probable parameters: fastest compression"<<std::endl;
                        #endif // debug
                        if (testDeflateParams(rBuffer, decompBuffer, streamOffsetList, j, 1, window, 9)) break;//try all memlvls for the most probable clvl
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 1, window, window, 1, 7)) break;
                        //try all clvl/memlvl combinations that have not been tried yet
                        testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 2, 9, window, window, 1, 9);
                        break;
                    }
                    case 1:{//if the header signals fast compression try clevel 2-5, header-supplied window and default memlvl(8)
                        #ifdef debug
                        std::cout<<"   trying most probable parameters: fast compression"<<std::endl;
                        #endif // debug
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 2, 5, window, window, 8, 8)) break;
                        //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                        #ifdef debug
                        std::cout<<"   trying less probable parameters: fast compression"<<std::endl<<std::endl;
                        #endif // debug
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 2, 5, window, window, 1, 7)) break;
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 2, 5, window, window, 9, 9)) break;

                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 1, window, window, 1, 9)) break;
                        testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 6, 9, window, window, 1, 9);
                        break;
                    }
                    case 2:{//if the header signals default compression only try clevel 6, header-supplied window and default memlvl(8)
                        #ifdef debug
                        std::cout<<"   trying most probable parameters: default compression"<<std::endl;
                        #endif // debug
                        if (testDeflateParams(rBuffer, decompBuffer, streamOffsetList, j, 6, window, 8)) break;
                        //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                        #ifdef debug
                        std::cout<<"   trying less probable parameters: default compression"<<std::endl<<std::endl;
                        #endif // debug
                        if (testDeflateParams(rBuffer, decompBuffer, streamOffsetList, j, 6, window, 9)) break;
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 6, 6, window, window, 1, 7)) break;

                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 5, window, window, 1, 9)) break;
                        testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 7, 9, window, window, 1, 9);
                        break;
                    }
                    case 3:{//if the header signals best compression only try clevel 7-9, header-supplied window and default memlvl(8)
                        #ifdef debug
                        std::cout<<"   trying most probable parameters: best compression"<<std::endl;
                        #endif // debug
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 7, 9, window, window, 8, 8)) break;
                        //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                        #ifdef debug
                        std::cout<<"   trying less probable parameters: best compression"<<std::endl<<std::endl;
                        #endif // debug
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 7, 9, window, window, 1, 7)) break;
                        if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 7, 9, window, window, 9, 9)) break;

                        testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 6, window, window, 1, 9);
                        break;
                    }
                    default:{//this should never happen
                        abort();
                    }
                }
                //if bruteforcing is turned on and needed, try all remaining combinations
                if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)>=mismatchTol)&&(bruteforceWindow)){//if bruteforcing is turned on try all remaining combinations
                    #ifdef debug
                    std::cout<<"bruteforcing strm #"<<j<<std::endl;
                    #endif // debug
                    if (window==10){
                        testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 9, 11, 15, 1, 9);
                    }else{
                        if (window==15){
                            testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 9, 10, 14, 1, 9);
                        }else{//if window is in the 11-14 range
                            if (testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 9, 10, (window-1), 1, 9)) break;
                            testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 9, (window+1), 15, 1, 9);
                        }
                    }
                }
                break;
            }
            case Z_DATA_ERROR: //the compressed data was invalid, this should never happen since the offsets have been checked
            {
                std::cout<<"inflate() failed with data error"<<std::endl;
                pauser();
                abort();
            }
            case Z_BUF_ERROR: //this should not happen since the decompressed lengths are known
            {
                std::cout<<"inflate() failed with memory error"<<std::endl;
                pauser();
                abort();
            }
            default: //shit hit the fan, should never happen normally
            {
                std::cout<<"inflate() failed with exit code:"<<ret<<std::endl;
                pauser();
                abort();
            }
        }
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            streamOffsetList[j].recomp=true;
        }
        delete [] decompBuffer;
    }
}

bool testDeflateParams(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel, uint8_t window, uint8_t memlevel){
    //tests if the supplied deflate params(clevel, memlevel, window) are better for recompressing the given streamoffset
    //if yes, then update the streamoffset object to the new best values, and if mismatch is within tolerance then return true
    int ret;
    uint64_t i;
    bool fullmatch=false;
    uint64_t identBytes;
    #ifdef debug
    std::cout<<"-------------------------"<<std::endl;
    std::cout<<"   memlevel:"<<+memlevel<<std::endl;
    std::cout<<"   clevel:"<<+clevel<<std::endl;
    std::cout<<"   window:"<<+window<<std::endl;
    #endif // debug
    z_stream strm;//prepare the z_stream
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in= decompbuff;
    ret = deflateInit2(&strm, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK){//initialize it and check for error
        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
    //create a buffer to hold the recompressed data
    unsigned char* recompBuffer= new unsigned char[deflateBound(&strm, offsets[offsetno].inflatedLength)];
    strm.avail_in= offsets[offsetno].inflatedLength;
    strm.next_out= recompBuffer;
    bool doFullStream=true;
    bool shortcut=false;
    if ((shortcutEnabled)&&(offsets[offsetno].streamLength>shortcutLength)){//if the stream is big and shortcuts are enabled
        shortcut=true;
        identBytes=0;
        strm.avail_out= shortcutLength;//only get a portion of the compressed data
        ret=deflate(&strm, Z_FINISH);
        if ((ret!=Z_STREAM_END)&&(ret!=Z_OK)){//most of the times the compressed data wont fit and we get Z_OK
            std::cout<<"deflate() in shorcut failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        #ifdef debug
        std::cout<<"   shortcut: "<<strm.total_in<<" bytes compressed to "<<strm.total_out<<" bytes"<<std::endl;
        #endif // debug
        for (i=0;i<strm.total_out;i++){
            if (recompBuffer[i]==origbuff[(i+offsets[offsetno].offset)]){
                identBytes++;
            }
        }
        if (identBytes<(shortcutLength-recompTresh)) doFullStream=false;//if we have too many mismatches bail early
        #ifdef debug
        std::cout<<"   shortcut: "<<identBytes<<" bytes out of "<<strm.total_out<<" identical"<<std::endl;
        #endif // debug
    }
    if (doFullStream){
        identBytes=0;
        if (shortcut){
            strm.avail_out=deflateBound(&strm, offsets[offsetno].inflatedLength)-shortcutLength;
        }else{
            strm.avail_out=deflateBound(&strm, offsets[offsetno].inflatedLength);
        }
        ret=deflate(&strm, Z_FINISH);//do the actual compression
        //check the return value to see if everything went well
        if (ret != Z_STREAM_END){
            std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
            abort();
        }
        #ifdef debug
        std::cout<<"   size difference: "<<(static_cast<int64_t>(strm.total_out)-static_cast<int64_t>(offsets[offsetno].streamLength))<<std::endl;
        #endif // debug
        uint64_t smaller;
        if (abs((strm.total_out-offsets[offsetno].streamLength))<=sizediffTresh){//if the size difference is not more than the treshold
            if (strm.total_out<offsets[offsetno].streamLength){//this is to prevent an array overread
                smaller=strm.total_out;
            } else {
                smaller=offsets[offsetno].streamLength;
            }
            for (i=0; i<smaller;i++){
                if (recompBuffer[i]==origbuff[(i+offsets[offsetno].offset)]){
                    identBytes++;
                }
            }
            #ifdef debug
            std::cout<<"   diffBytes: "<<(offsets[offsetno].streamLength-identBytes)<<std::endl;
            #endif // debug
            if (identBytes>offsets[offsetno].identBytes){//if this recompressed stream has more matching bytes than the previous best
                offsets[offsetno].identBytes=identBytes;
                offsets[offsetno].clevel=clevel;
                offsets[offsetno].memlvl=memlevel;
                offsets[offsetno].window=window;
                offsets[offsetno].firstDiffByte=-1;
                offsets[offsetno].diffByteOffsets.clear();
                offsets[offsetno].diffByteVal.clear();
                uint64_t last_i=0;
                if (identBytes==offsets[offsetno].streamLength){//if we have a full match set the flag to bail from the nested loops
                    #ifdef debug
                    std::cout<<"   recompression succesful, full match"<<std::endl;
                    #endif // debug
                    fullmatch=true;
                } else {//there are different bytes and/or bytes at the end
                    if (identBytes+mismatchTol>=offsets[offsetno].streamLength) fullmatch=true;//if at most mismatchTol bytes diff bail from the loop
                        for (i=0; i<smaller;i++){//diff it
                            if (recompBuffer[i]!=origbuff[(i+offsets[offsetno].offset)]){//if a mismatching byte is found
                                if (offsets[offsetno].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                    offsets[offsetno].firstDiffByte=(i);
                                    offsets[offsetno].diffByteOffsets.push_back(0);
                                    offsets[offsetno].diffByteVal.push_back(origbuff[(i+offsets[offsetno].offset)]);
                                    #ifdef debug
                                    std::cout<<"   first diff byte:"<<i<<std::endl;
                                    #endif // debug
                                    last_i=i;
                                } else {
                                    offsets[offsetno].diffByteOffsets.push_back(i-last_i);
                                    offsets[offsetno].diffByteVal.push_back(origbuff[(i+offsets[offsetno].offset)]);
                                    //cout<<"   different byte:"<<i<<endl;
                                    last_i=i;
                                }
                            }
                        }
                        if (strm.total_out<offsets[offsetno].streamLength){//if the recompressed stream is shorter we need to add bytes after diffing
                            for (i=0; i<(offsets[offsetno].streamLength-strm.total_out); i++){//adding bytes
                                if ((i==0)&&((last_i+1)<strm.total_out)){//if the last byte of the recompressed stream was a match
                                    offsets[offsetno].diffByteOffsets.push_back(strm.total_out-last_i);
                                } else{
                                    offsets[offsetno].diffByteOffsets.push_back(1);
                                }
                                offsets[offsetno].diffByteVal.push_back(origbuff[(i+strm.total_out+offsets[offsetno].offset)]);
                                #ifdef debug
                                std::cout<<"   byte at the end added :"<<+origbuff[(i+strm.total_out+offsets[offsetno].offset)]<<std::endl;
                                #endif // debug
                            }
                        }
                    }
                }
            }
            #ifdef debug
            else{
                std::cout<<"   size difference is greater than "<<sizediffTresh<<" bytes, not comparing"<<std::endl;
            }
            #endif // debug
        }
    ret=deflateEnd(&strm);
    if ((ret != Z_OK)&&!((ret==Z_DATA_ERROR) && (!doFullStream))){//Z_DATA_ERROR is only acceptable if we skipped the full recompression
        std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
    delete [] recompBuffer;
    return fullmatch;
}

int doInflate(unsigned char* next_in, uint64_t avail_in, unsigned char* next_out, uint64_t avail_out){
    //this function takes a zlib stream from next_in and decompresses it to next_out, returning the return value of inflate()
    //the zlib stream must be at most avail_in bytes long and the inflated data must be at most avail_out bytes long
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = avail_in;
    strm.next_in=next_in;
    //initialize the stream for decompression and check for error
    int ret=inflateInit(&strm);
    if (ret != Z_OK){
        std::cout<<"inflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
    strm.next_out=next_out;
    strm.avail_out=avail_out;
    int ret2=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
    //deallocate the zlib stream, check for errors and deallocate the decompression buffer
    ret=inflateEnd(&strm);
    if (ret!=Z_OK){
        std::cout<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
    return ret2;
}

void doDeflate(unsigned char* next_in, uint64_t avail_in, unsigned char*& next_out, uint_fast8_t clvl, uint_fast8_t window, uint_fast8_t memlvl, uint64_t& total_in, uint64_t& total_out){
    //this function takes avail_in bytes from next_in, deflates them using the clvl, window and memlvl parameters
    //allocates a new array and puts the pointer in next_out, and fills this array with the result of the compression
    //the consumed input and produced output byte counts are written in total_in and total_out
    //the dynamic array allocated by this function must be manually deleted to avoid a memory leak
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in=next_in;
    //use all default settings except clevel and memlevel
    int ret = deflateInit2(&strm, clvl, Z_DEFLATED, window, memlvl, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK){
        std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
    //prepare for compressing in one pass
    strm.avail_in=avail_in;
    next_out=new unsigned char[deflateBound(&strm, avail_in)]; //allocate output for worst case
    strm.avail_out=deflateBound(&strm, avail_in);
    strm.next_out=next_out;
    ret=deflate(&strm, Z_FINISH);//do the actual compression
    //check the return value to see if everything went well
    if (ret != Z_STREAM_END){
        std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
        abort();
    }
    total_in=strm.total_in;
    total_out=strm.total_out;
    //deallocate the Zlib stream and check if it went well
    ret=deflateEnd(&strm);
    if (ret != Z_OK){
        std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
        abort();
    }
}

// A zlib stream has the following structure: (http://tools.ietf.org/html/rfc1950)
//  +---+---+   CMF: bits 0 to 3  CM      Compression method (8 = deflate)
//  |CMF|FLG|        bits 4 to 7  CINFO   Compression info (base-2 logarithm of the LZ77 window size minus 8)
//  +---+---+
//              FLG: bits 0 to 4  FCHECK  Check bits for CMF and FLG (in MSB order (CMF*256 + FLG) is a multiple of 31)
//                   bit  5       FDICT   Preset dictionary
//                   bits 6 to 7  FLEVEL  Compression level (0 = fastest, 1 = fast, 2 = default, 3 = maximum)
int parseOffsetType(int header){
    switch (header){
        case 0x2815 : return 0;  case 0x2853 : return 1;  case 0x2891 : return 2;  case 0x28cf : return 3;
        case 0x3811 : return 4;  case 0x384f : return 5;  case 0x388d : return 6;  case 0x38cb : return 7;
        case 0x480d : return 8;  case 0x484b : return 9;  case 0x4889 : return 10; case 0x48c7 : return 11;
        case 0x5809 : return 12; case 0x5847 : return 13; case 0x5885 : return 14; case 0x58c3 : return 15;
        case 0x6805 : return 16; case 0x6843 : return 17; case 0x6881 : return 18; case 0x68de : return 19;
        case 0x7801 : return 20; case 0x785e : return 21; case 0x789c : return 22; case 0x78da : return 23;
        default: return -1;
    }
}

void searchBuffer(unsigned char buffer[], std::vector<fileOffset>& offsets, uint64_t buffLen){
    //this function searches a buffer for zlib headers, count them and fill a vector of fileOffsets

    //try to guess the number of potential zlib headers in the file from the file size
	//this value is purely empirical, may need tweaking
	offsets.reserve(static_cast<int_fast64_t>(buffLen/1912));
	#ifdef debug
	std::cout<<"Offset list initial capacity:"<<offsets.capacity()<<std::endl;
	pauser();
	#endif

	uint_fast64_t i;
	uint_fast64_t redlen=buffLen-1;//it is pointless to test the last byte and it could cause and out of bounds read
	//a new variable is used so the substraction is only performed once, not every time it loops

    for(i=0;i<redlen;i++){
        //search for 7801, 785E, 789C, 78DA, 68DE, 6881, 6843, 6805, 58C3, 5885, 5847, 5809,
        //           48C7, 4889, 484B, 480D, 38CB, 388D, 384F, 3811, 28CF, 2891, 2853, 2815
        int header = ((int)buffer[i]) * 256 + (int)buffer[i + 1];
        int offsetType = parseOffsetType(header);
        if (offsetType >= 0){
            #ifdef debug
            std::cout << "Zlib header 0x" << std::hex << std::setfill('0') << std::setw(4) << header << std::dec
                      << " with " << (1 << ((header >> 12) - 2)) << "K window at offset: " << i << std::endl;
            #endif // debug
            offsets.push_back(fileOffset(i, offsetType));
            }
        }
    #ifdef debug
    std::cout<<std::endl;
	std::cout<<"Number of collected offsets:"<<offsets.size()<<std::endl;
    pauser();
    #endif // debug
}

bool CheckOffset(unsigned char *next_in, uint64_t avail_in, uint64_t& total_in, uint64_t& total_out){
    //this function checks if there is a valid zlib stream at next_in
    //if yes, then return with true and set the total_in and total_out variables to the deflated and inflated length of the stream
	z_stream strm;
    strm.zalloc=Z_NULL;
    strm.zfree=Z_NULL;
    strm.opaque=Z_NULL;
    bool success=false;
    uint64_t memScale=1;

    while (true){
		strm.avail_in=avail_in;
		strm.next_in=next_in;
		//initialize the stream for decompression and check for error
		int ret=inflateInit(&strm);
		if (ret != Z_OK){
			std::cout<<"inflateInit() failed with exit code:"<<ret<<std::endl;
			abort();
		}
        //a buffer needs to be created to hold the resulting decompressed data
        //this is a big problem since the zlib header does not contain the length of the decompressed data
        //the best we can do is to take a guess, and see if it was big enough, if not then scale it up
        unsigned char* decompBuffer= new unsigned char[(memScale*5*avail_in)]; //just a wild guess, corresponds to a compression ratio of 20%
        strm.next_out=decompBuffer;
        strm.avail_out=memScale*5*avail_in;
        ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        if (ret==Z_STREAM_END && strm.total_in>=16)//decompression was succesful
        {
            total_in=strm.total_in;
			total_out=strm.total_out;
            success=true;
        }
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        if (inflateEnd(&strm)!=Z_OK){
			std::cout<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        delete [] decompBuffer;
        if (ret!=Z_BUF_ERROR) break;
        memScale++;//increase buffer size for the next iteration
    };
    return success;
}

void testOffsetList(unsigned char buffer[], uint64_t bufflen, std::vector<fileOffset>& fileoffsets, std::vector<streamOffset>& streamoffsets){
    //this function takes a vector of fileOffsets, a buffer of bufflen length and tests if the offsets in the fileOffset vector
    //are marking the beginning of a valid zlib stream
    //the offsets, types, lengths and inflated lengths of valid zlib streams are pushed to a vector of streamOffsets
	uint64_t numOffsets=fileoffsets.size();
	uint64_t lastGoodOffset=0;
	uint64_t lastStreamLength=0;
	uint64_t i;
    for (i=0; i<numOffsets; i++){
        //if the current offset is known to be part of the last stream it is pointless to check it
        if ((lastGoodOffset+lastStreamLength)<=fileoffsets[i].offset){
            //since we have no idea about the length of the zlib stream, take the worst case, i.e. everything after the header belongs to the stream
            uint64_t inbytes, outbytes;
            if (CheckOffset((buffer+fileoffsets[i].offset), (bufflen-fileoffsets[i].offset), inbytes, outbytes)){
                lastGoodOffset=fileoffsets[i].offset;
                lastStreamLength=inbytes;
                streamoffsets.push_back(streamOffset(fileoffsets[i].offset, fileoffsets[i].offsetType, inbytes, outbytes));
                #ifdef debug
                std::cout<<"Offset #"<<i<<" decompressed, "<<inbytes<<" bytes to "<<outbytes<<" bytes"<<std::endl;
                #endif // debug
            }
        }
        #ifdef debug
        else{
            std::cout<<"skipping offset #"<<i<<" ("<<fileoffsets[i].offset<<") because it cannot be a header"<<std::endl;
        }
        #endif // debug
    }
}

int main(int argc, char* argv[]) {
	using std::cout;
	using std::endl;
	using std::cin;
	using std::vector;

	uint_fast64_t i,j;
	std::ifstream infile;
	std::ofstream outfile;
	uint64_t infileSize;
	vector<fileOffset> offsetList;//offsetList stores memory offsets where potential headers can be found, and the type of the offset
	vector<streamOffset> streamOffsetList;//streamOffsetList stores offsets of confirmed zlib streams and a bunch of data on them
	z_stream strm;
	unsigned char* rBuffer;

	uint64_t lastos=0;
    uint64_t lastlen=0;
    uint64_t atzlen=0;//placeholder for the length of the atz file
    int ret=-9;
    #ifdef debug
    uint_fast64_t numFullmatch=0;
    #endif // debug
    uint64_t recomp=0;
    cout<<"AntiZ 0.1.4-git"<<endl;

	//PHASE 0
	//parse CLI arguments, open input file and read it into memory

	//make sure the file name stings do not hold any uninitialized data
	infile_name.clear();
	reconfile_name.clear();
	atzfile_name.clear();
    //parse CLI arguments and if needed jump to reconstruction
	parseCLI(argc, argv);
    if (recon) goto PHASE5;
    //open the input file and check for error
	infile.open(infile_name, std::ios::in | std::ios::binary);
	if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
       pauser();
 	   return -1;
	}
	//getting the size of the file
	infile.seekg (0, infile.end);
	infileSize=infile.tellg();
	infile.seekg (0, infile.beg);
	cout<<"Input size:"<<infileSize<<endl;
    //setting up read buffer and reading the entire file into the buffer
    rBuffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
    infile.close();

    //PHASE 1
	//search the file for zlib headers, count them and create an offset list
	searchBuffer(rBuffer, offsetList, infileSize);
    #ifdef debug
    cout<<"Found "<<offsetList.size()<<" zlib headers"<<endl;
    #endif // debug

    //PHASE 2
    //start trying to decompress at the collected offsets
    //test all offsets found in phase 1
    testOffsetList(rBuffer, infileSize, offsetList, streamOffsetList);
    cout<<"Good offsets: "<<streamOffsetList.size()<<endl;
    offsetList.clear();//we only need the good offsets
    offsetList.shrink_to_fit();
    #ifdef debug
    pauser();
    #endif // debug

    //PHASE 3
    //start trying to find the parameters to use for recompression

    findDeflateParams(rBuffer, streamOffsetList);
    cout<<endl;

    #ifdef debug
    for (j=0; j<streamOffsetList.size(); j++){
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=mismatchTol)&&(streamOffsetList[j].identBytes>0)) numFullmatch++;
    }
    cout<<"fullmatch streams:"<<numFullmatch<<" out of "<<streamOffsetList.size()<<endl;
    cout<<endl;
    pauser();
    cout<<"Stream info"<<endl;
    for (j=0; j<streamOffsetList.size(); j++){
        cout<<"-------------------------"<<endl;
        cout<<"   stream #"<<j<<endl;
        cout<<"   offset:"<<streamOffsetList[j].offset<<endl;
        cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<endl;
        cout<<"   clevel:"<<+streamOffsetList[j].clevel<<endl;
        cout<<"   window:"<<+streamOffsetList[j].window<<endl;
        cout<<"   best match:"<<streamOffsetList[j].identBytes<<" out of "<<streamOffsetList[j].streamLength<<endl;
        cout<<"   diffBytes:"<<streamOffsetList[j].diffByteOffsets.size()<<endl;
        cout<<"   diffVals:"<<streamOffsetList[j].diffByteVal.size()<<endl;
        cout<<"   mismatched bytes:";
        for (i=0; i<streamOffsetList[j].diffByteOffsets.size(); i++){
            cout<<streamOffsetList[j].diffByteOffsets[i]<<";";
        }
        cout<<endl;
    }
    #endif // debug

    for (j=0; j<streamOffsetList.size(); j++){
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            recomp++;
        }
    }
    cout<<"recompressed:"<<recomp<<"/"<<streamOffsetList.size()<<endl;

    #ifdef debug
    pauser();
    #endif // debug

    //PHASE 4
    //take the information created in phase 3 and use it to create an ATZ file
    //currently ATZ1 is in use, no specifications yet, and will be deprecated when ATZ2 comes
    outfile.open(atzfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outfile.is_open()) {
       cout << "error: open file for output failed!" << endl;
       pauser();
 	   abort();
	}
    {//write file header and version
        unsigned char* atz1=new unsigned char[4];
        atz1[0]=65;
        atz1[1]=84;
        atz1[2]=90;
        atz1[3]=1;
        outfile.write(reinterpret_cast<char*>(atz1), 4);
        delete [] atz1;
    }

    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    outfile.write(reinterpret_cast<char*>(&infileSize), 8);//the length of the original file
    outfile.write(reinterpret_cast<char*>(&recomp), 8);//number of recompressed streams

    for(j=0;j<streamOffsetList.size();j++){//write recompressed stream descriptions
        if (streamOffsetList[j].recomp==true){//we are operating on the j-th stream
            #ifdef debug
            cout<<"recompressing stream #"<<j<<endl;
            #endif // debug
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].offset), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].streamLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].inflatedLength), 8);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].clevel), 1);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].window), 1);
            outfile.write(reinterpret_cast<char*>(&streamOffsetList[j].memlvl), 1);

            {
                uint64_t diffbytes=streamOffsetList[j].diffByteOffsets.size();
                outfile.write(reinterpret_cast<char*>(&diffbytes), 8);
                if (diffbytes>0){
                    uint64_t firstdiff=streamOffsetList[j].firstDiffByte;
                    outfile.write(reinterpret_cast<char*>(&firstdiff), 8);
                    uint64_t diffos;
                    uint8_t diffval;
                    for(i=0;i<diffbytes;i++){
                        diffos=streamOffsetList[j].diffByteOffsets[i];
                        outfile.write(reinterpret_cast<char*>(&diffos), 8);
                    }
                    for(i=0;i<diffbytes;i++){
                        diffval=streamOffsetList[j].diffByteVal[i];
                        outfile.write(reinterpret_cast<char*>(&diffval), 1);
                    }
                }
            }

            //create a new Zlib stream to do decompression
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in= streamOffsetList[j].streamLength;
            strm.next_in=rBuffer+streamOffsetList[j].offset;
            //initialize the stream for decompression and check for error
            ret=inflateInit(&strm);
            if (ret != Z_OK)
            {
                cout<<"inflateInit() failed with exit code:"<<ret<<endl;
                pauser();
                abort();
            }
            //a buffer needs to be created to hold the resulting decompressed data
            unsigned char* decompBuffer= new unsigned char[streamOffsetList[j].inflatedLength];
            strm.next_out=decompBuffer;
            strm.avail_out=streamOffsetList[j].inflatedLength;
            ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
            //check the return value
            switch (ret)
            {
                case Z_STREAM_END://decompression was succesful
                {
                    break;
                }
                default://shit hit the fan, should never happen normally
                {
                    cout<<"inflate() failed with exit code:"<<ret<<endl;
                    pauser();
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                pauser();
                return ret;
            }
            outfile.write(reinterpret_cast<char*>(decompBuffer), streamOffsetList[j].inflatedLength);
            delete [] decompBuffer;
        }
    }


    for(j=0;j<streamOffsetList.size();j++){//write the gaps before streams and non-recompressed streams to disk as the residue
        if ((lastos+lastlen)==streamOffsetList[j].offset){
            #ifdef debug
            cout<<"no gap before stream #"<<j<<endl;
            #endif // debug
            if (streamOffsetList[j].recomp==false){
                #ifdef debug
                cout<<"copying stream #"<<j<<endl;
                #endif // debug
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }else{
            #ifdef debug
            cout<<"gap of "<<(streamOffsetList[j].offset-(lastos+lastlen))<<" bytes before stream #"<<j<<endl;
            #endif // debug
            outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (streamOffsetList[j].offset-(lastos+lastlen)));
            if (streamOffsetList[j].recomp==false){
                #ifdef debug
                cout<<"copying stream #"<<j<<endl;
                #endif // debug
                outfile.write(reinterpret_cast<char*>(rBuffer+streamOffsetList[j].offset), streamOffsetList[j].streamLength);
            }
        }
        lastos=streamOffsetList[j].offset;
        lastlen=streamOffsetList[j].streamLength;
    }
    if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
        #ifdef debug
        cout<<(infileSize-(lastos+lastlen))<<" bytes copied from the end of the file"<<endl;
        #endif // debug
        outfile.write(reinterpret_cast<char*>(rBuffer+lastos+lastlen), (infileSize-(lastos+lastlen)));
    }

    atzlen=outfile.tellp();
    cout<<"Total bytes written: "<<atzlen<<endl;
    outfile.seekp(4);//go back to the placeholder
    outfile.write(reinterpret_cast<char*>(&atzlen), 8);
    #ifdef debug
    pauser();
    #endif // debug
    streamOffsetList.clear();
    streamOffsetList.shrink_to_fit();
    outfile.close();
    delete [] rBuffer;

    PHASE5:
    //PHASE 5: verify that we can reconstruct the original file, using only data from the ATZ file
    if (!notest){//dont reconstruct if we wont test it
    infileSize=0;
    atzlen=0;
    lastos=28;
    uint64_t origlen=0;
    uint64_t nstrms=0;

    std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary);
	if (!atzfile.is_open()) {
       cout << "error: open ATZ file for input failed!" << endl;
       pauser();
 	   abort();
	}
	cout<<"reconstructing from "<<atzfile_name<<endl;
    atzfile.seekg (0, atzfile.end);
	infileSize=atzfile.tellg();
	atzfile.seekg (0, atzfile.beg);
	cout<<"Input size:"<<infileSize<<endl;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* atzBuffer = new unsigned char[infileSize];
    atzfile.read(reinterpret_cast<char*>(atzBuffer), infileSize);
    atzfile.close();

    if ((atzBuffer[0]!=65)||(atzBuffer[1]!=84)||(atzBuffer[2]!=90)||(atzBuffer[3]!=1)){
        cout<<"ATZ1 header not found"<<endl;
        pauser();
        abort();
    }
    atzlen=*reinterpret_cast<uint64_t*>(&atzBuffer[4]);
    if (atzlen!=infileSize){
        cout<<"atzlen mismatch"<<endl;
        pauser();
        abort();
    }
    origlen=*reinterpret_cast<uint64_t*>(&atzBuffer[12]);
    nstrms=*reinterpret_cast<uint64_t*>(&atzBuffer[20]);
    #ifdef debug
    cout<<"nstrms:"<<nstrms<<endl;
    #endif // debug
    if (nstrms>0){
        streamOffsetList.reserve(nstrms);
        //reead in all the info about the streams
        for (j=0;j<nstrms;j++){
            #ifdef debug
            cout<<"stream #"<<j<<endl;
            #endif // debug
            streamOffsetList.push_back(streamOffset(*reinterpret_cast<uint64_t*>(&atzBuffer[lastos]), -1, *reinterpret_cast<uint64_t*>(&atzBuffer[8+lastos]), *reinterpret_cast<uint64_t*>(&atzBuffer[16+lastos])));
            streamOffsetList[j].clevel=atzBuffer[24+lastos];
            streamOffsetList[j].window=atzBuffer[25+lastos];
            streamOffsetList[j].memlvl=atzBuffer[26+lastos];
            #ifdef debug
            cout<<"   offset:"<<streamOffsetList[j].offset<<endl;
            cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<endl;
            cout<<"   clevel:"<<+streamOffsetList[j].clevel<<endl;
            cout<<"   window:"<<+streamOffsetList[j].window<<endl;
            #endif // debug
            //partial match handling
            uint64_t diffbytes=*reinterpret_cast<uint64_t*>(&atzBuffer[27+lastos]);
            if (diffbytes>0){//if the stream is just a partial match
                #ifdef debug
                cout<<"   partial match"<<endl;
                #endif // debug
                streamOffsetList[j].firstDiffByte=*reinterpret_cast<uint64_t*>(&atzBuffer[35+lastos]);
                streamOffsetList[j].diffByteOffsets.reserve(diffbytes);
                streamOffsetList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamOffsetList[j].diffByteOffsets.push_back(*reinterpret_cast<uint64_t*>(&atzBuffer[43+8*i+lastos]));
                    streamOffsetList[j].diffByteVal.push_back(atzBuffer[43+diffbytes*8+i+lastos]);
                }
                streamOffsetList[j].atzInfos=&atzBuffer[43+diffbytes*9+lastos];
                lastos=lastos+43+diffbytes*9+streamOffsetList[j].inflatedLength;
            } else{//if the stream is a full match
                #ifdef debug
                cout<<"   full match"<<endl;
                #endif // debug
                streamOffsetList[j].firstDiffByte=-1;//negative value signals full match
                streamOffsetList[j].atzInfos=&atzBuffer[35+lastos];
                lastos=lastos+35+streamOffsetList[j].inflatedLength;
            }
        }
        #ifdef debug
        cout<<"lastos:"<<lastos<<endl;
        #endif // debug
        uint64_t residueos=lastos;
        uint64_t gapsum=0;
        #ifdef debug
        pauser();
        #endif // debug
        //do the reconstructing
        lastos=0;
        lastlen=0;
        std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
        //write the gap before the stream(if the is one), then do the compression using the parameters from the ATZ file
        //then modify the compressed data according to the ATZ file(if necessary)
        for(j=0;j<streamOffsetList.size();j++){
            if ((lastos+lastlen)==streamOffsetList[j].offset){//no gap before the stream
                #ifdef debug
                cout<<"no gap before stream #"<<j<<endl;
                cout<<"reconstructing stream #"<<j<<endl;
                #endif // debug
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+32768];
                {
                    //do compression
                    #ifdef debug
                    cout<<"   compressing"<<endl;
                    #endif // debug
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
                        pauser();
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamOffsetList[j].streamLength+32768;
                    ret=deflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
                    //check the return value
                    switch (ret)
                    {
                        case Z_STREAM_END://decompression was succesful
                        {
                            break;
                        }
                        default://shit hit the fan, should never happen normally
                        {
                            cout<<"deflate() failed with exit code:"<<ret<<endl;
                            pauser();
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pauser();
                        return ret;
                    }
                }
                //do stream modification if needed
                if (streamOffsetList[j].firstDiffByte>=0){
                    #ifdef debug
                    cout<<"   modifying "<<streamOffsetList[j].diffByteOffsets.size()<<" bytes"<<endl;
                    #endif // debug
                    uint64_t db=streamOffsetList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamOffsetList[j].firstDiffByte+streamOffsetList[j].diffByteOffsets[i]+sum]=streamOffsetList[j].diffByteVal[i];
                        sum=sum+streamOffsetList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                delete [] compBuffer;
            }else{
                #ifdef debug
                cout<<"gap of "<<(streamOffsetList[j].offset-(lastos+lastlen))<<" bytes before stream #"<<j<<endl;
                #endif // debug
                recfile.write(reinterpret_cast<char*>(atzBuffer+residueos+gapsum), (streamOffsetList[j].offset-(lastos+lastlen)));
                gapsum=gapsum+(streamOffsetList[j].offset-(lastos+lastlen));
                #ifdef debug
                cout<<"reconstructing stream #"<<j<<endl;
                #endif // debug
                //a buffer needs to be created to hold the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+32768];
                {
                    //do compression
                    #ifdef debug
                    cout<<"   compressing"<<endl;
                    #endif // debug
                    strm.zalloc = Z_NULL;
                    strm.zfree = Z_NULL;
                    strm.opaque = Z_NULL;
                    strm.next_in=streamOffsetList[j].atzInfos;
                    strm.avail_in=streamOffsetList[j].inflatedLength;
                    //initialize the stream for compression and check for error
                    ret=deflateInit2(&strm, streamOffsetList[j].clevel, Z_DEFLATED, streamOffsetList[j].window, streamOffsetList[j].memlvl, Z_DEFAULT_STRATEGY);
                    if (ret != Z_OK)
                    {
                        cout<<"deflateInit() failed with exit code:"<<ret<<endl;
                        pauser();
                        abort();
                    }
                    strm.next_out=compBuffer;
                    strm.avail_out=streamOffsetList[j].streamLength+32768;
                    ret=deflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
                    //check the return value
                    switch (ret)
                    {
                        case Z_STREAM_END://decompression was succesful
                        {
                            break;
                        }
                        default://shit hit the fan, should never happen normally
                        {
                            cout<<"deflate() failed with exit code:"<<ret<<endl;
                            pauser();
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pauser();
                        return ret;
                    }
                }
                //do stream modification if needed
                if (streamOffsetList[j].firstDiffByte>=0){
                    #ifdef debug
                    cout<<"   modifying "<<streamOffsetList[j].diffByteOffsets.size()<<" bytes"<<endl;
                    #endif // debug
                    uint64_t db=streamOffsetList[j].diffByteOffsets.size();
                    uint64_t sum=0;
                    for(i=0;i<db;i++){
                        compBuffer[streamOffsetList[j].firstDiffByte+streamOffsetList[j].diffByteOffsets[i]+sum]=streamOffsetList[j].diffByteVal[i];
                        sum=sum+streamOffsetList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                delete [] compBuffer;
            }
            lastos=streamOffsetList[j].offset;
            lastlen=streamOffsetList[j].streamLength;
        }
        if ((lastos+lastlen)<origlen){
            #ifdef debug
            cout<<"copying "<<(origlen-(lastos+lastlen))<<" bytes to the end of the file"<<endl;
            #endif // debug
            recfile.write(reinterpret_cast<char*>(atzBuffer+residueos+gapsum), (origlen-(lastos+lastlen)));
        }
        recfile.close();
    }else{//if there are no recompressed streams
        #ifdef debug
        cout<<"no recompressed streams in the ATZ file, copying "<<origlen<<" bytes"<<endl;
        #endif // debug
        std::ofstream recfile(reconfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
        recfile.write(reinterpret_cast<char*>(atzBuffer+28), origlen);
        recfile.close();
    }

    #ifdef debug
    pauser();
    #endif // debug
    delete [] atzBuffer;
    }

    //PHASE 6: verify that the reconstructed file is identical to the original
    if((!recon)&&(!notest)){//if we are just reconstructing we dont have the original file
        cout<<"Testing...";
        //open the original file and read it in
        infile.open(infile_name, std::ios::in | std::ios::binary);
        if (!infile.is_open()) {
            cout << "error: open file for input failed!" << endl;
            abort();
        }
        //getting the size of the file
        infile.seekg (0, infile.end);
        infileSize=infile.tellg();
        infile.seekg (0, infile.beg);
        #ifdef debug
        cout<<endl<<"Original file size:"<<infileSize<<endl;
        #endif // debug
        //setting up read buffer and reading the entire file into the buffer
        rBuffer = new unsigned char[infileSize];
        infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
        infile.close();
        std::ifstream recfile;
        //open the reconstructed file and read it in
        recfile.open(reconfile_name, std::ios::in | std::ios::binary);
        if (!recfile.is_open()) {
            cout << "error: open file for input failed!" << endl;
            abort();
        }
        //getting the size of the file
        recfile.seekg (0, recfile.end);
        uint64_t recfileSize=recfile.tellg();
        recfile.seekg (0, recfile.beg);
        #ifdef debug
        cout<<endl<<"Reconstructed file size:"<<recfileSize<<endl;
        #endif // debug
        //setting up read buffer and reading the entire file into the buffer
        unsigned char* recBuffer = new unsigned char[recfileSize];
        recfile.read(reinterpret_cast<char*>(recBuffer), recfileSize);
        recfile.close();
        if(infileSize!=recfileSize){
            cout<<"error: size mismatch";
            abort();
        }
        for (i=0; i<infileSize; i++){
            if (rBuffer[i]!=recBuffer[i]){
                cout<<"error: byte mismatch "<<i;
                abort();
            }
        }
        cout<<"OK!"<<endl;
        #ifdef debug
        pauser();
        #endif // debug
        if (remove(reconfile_name.c_str())!=0){//delete the reconstructed file since it was only needed for testing
            cout<<"error: cannot delete recfile";
            abort();
        }
    }
	return 0;
}
