#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <zlib.h>

#define default_infile "test.bin"
#define default_atzfile "atztest.atz"
#define default_reconfile "recon.bin"

uint_fast16_t recompTresh=128;//streams are only recompressed if the best match differs from the original in <= recompTresh bytes
int sizediffTresh=128;//streams are only compared when the size difference is <= sizediffTresh
bool shortcutEnabled=true;//enable speedup shortcut in slow mode
uint_fast16_t shortcutLength=1024;//stop compression and count mismatches after this many bytes, if we get more than recompTresh then bail early
uint_fast16_t mismatchTol=2;//if there are at most this many mismatches consider the stream a full match and stop looking for better parameters

int_fast64_t concentrate=-1;//only try to recompress the stream# givel here, negative values disable this and run on all streams, debug tool

void pauser(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}

class fileOffset{
public:
    fileOffset(){
        abort();//the default constructor should not be used in this version
    }
    fileOffset(int_fast64_t os, int ot){
        offset=os;
        offsetType=ot;
    }
    uint_fast64_t offset;
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
    int_fast64_t identBytes;
    int_fast64_t firstDiffByte;//the offset of the first byte that does not match, relative to stream start, not file start
    std::vector<int_fast64_t> diffByteOffsets;//offsets of bytes that differ, this is an incremental offset list to enhance recompression, kinda like a PNG filter
    //this improves compression if the mismatching bytes are consecutive, eg. 451,452,453,...(no repetitions, hard to compress)
    //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
    std::vector<unsigned char> diffByteVal;
    bool recomp;
    unsigned char* atzInfos;
};
void searchBuffer(unsigned char buffer[], std::vector<fileOffset>& offsets, uint_fast64_t buffLen);
bool CheckOffset(unsigned char *next_in, uint64_t avail_in, uint64_t& total_in, uint64_t& total_out);
void testOffsetList(unsigned char buffer[], uint64_t bufflen, std::vector<fileOffset>& fileoffsets, std::vector<streamOffset>& streamoffsets);
int parseOffsetType(int header);
void doDeflate(unsigned char* next_in, uint64_t avail_in, unsigned char*& next_out, uint_fast8_t clvl, uint_fast8_t window, uint_fast8_t memlvl, uint64_t& total_in, uint64_t& total_out);
int doInflate(unsigned char* next_in, uint64_t avail_in, unsigned char* next_out, uint64_t avail_out);
bool testDeflateParams(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel, uint8_t window, uint8_t memlevel);
void findDeflateParams(unsigned char rBuffer[], std::vector<streamOffset>& streamOffsetList);
bool testParamRange(unsigned char origbuff[], unsigned char decompbuff[], std::vector<streamOffset>& offsets, uint64_t offsetno, uint8_t clevel_min, uint8_t clevel_max,
                    uint8_t window_min, uint8_t window_max, uint8_t memlevel_min, uint8_t memlevel_max)
{
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
                #ifdef debug
                std::cout<<std::endl;
                std::cout<<"stream #"<<j<<"("<<streamOffsetList[j].offset<<")"<<" ready for recompression trials"<<std::endl;
                #endif // debug
                #ifdef debug
                std::cout<<"   stream type: "<<streamOffsetList[j].offsetType<<std::endl;
                #endif // debug
                testParamRange(rBuffer, decompBuffer, streamOffsetList, j, 1, 9, 10, 15, 1, 9);
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
        delete [] decompBuffer;
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            streamOffsetList[j].recomp=true;
        }
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
        pauser();
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
            pauser();
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
            pauser();
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
        pauser();
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
        pauser();
        abort();
    }
    strm.next_out=next_out;
    strm.avail_out=avail_out;
    int ret2=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
    //deallocate the zlib stream, check for errors and deallocate the decompression buffer
    ret=inflateEnd(&strm);
    if (ret!=Z_OK){
        std::cout<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
        pauser();
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
        pauser();
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
        pauser();
        abort();
    }
    total_in=strm.total_in;
    total_out=strm.total_out;
    //deallocate the Zlib stream and check if it went well
    ret=deflateEnd(&strm);
    if (ret != Z_OK){
        std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
        pauser();
        abort();
    }
}

int parseOffsetType(int header){
    switch (header){
        case 0x2815 : return 0;  case 0x2853 : return 1;  case 0x2891 : return 2;  case 0x28cf : return 3;
        case 0x3811 : return 4;  case 0x384f : return 5;  case 0x388d : return 6;  case 0x38cb : return 7;
        case 0x480d : return 8;  case 0x484b : return 9;  case 0x4889 : return 10; case 0x48c7 : return 11;
        case 0x5809 : return 12; case 0x5847 : return 13; case 0x5885 : return 14; case 0x58c3 : return 15;
        case 0x6805 : return 16; case 0x6843 : return 17; case 0x6881 : return 18; case 0x68de : return 19;
        case 0x7801 : return 20; case 0x785e : return 21; case 0x789c : return 22; case 0x78da : return 23;
    }
    return 0;
}

void searchBuffer(unsigned char buffer[], std::vector<fileOffset>& offsets, uint_fast64_t buffLen){
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
        if (offsetType > 0){
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
			pauser();
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
            pauser();
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
    //the offsets, types, lengths and inflated lengths of valid zlib streams are pused to a vector of streamOffsets
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
	vector<fileOffset> offsetList;//offsetList stores memory offsets where potential headers can be found, and the type of the offset
	vector<streamOffset> streamOffsetList;//streamOffsetList stores offsets of confirmed zlib streams and a bunch of data on them
	z_stream strm;
	unsigned char* rBuffer;

	uint64_t lastos=0;
    uint64_t lastlen=0;
    uint64_t atzlen=0;//placeholder for the length of the atz file
    int ret=-9;
    #ifdef debug
    int_fast64_t numFullmatch=0;
    #endif // debug
    uint64_t recomp=0;

    //DO NOT turn off slowmode, the alternative code (optimized mode) does not work at all
    bool slowmode=true;//slowmode bruteforces the zlib parameters, optimized mode only tries probable parameters based on the 2-byte header


	//PHASE 0
	//opening file

	//this code dumps the strings from the CLI, can be useful for debug
	/*for (int i = 0; i < argc; ++i) {
        std::cout << argv[i] << std::endl;
    }*/
	uint64_t infileSize;
	char* infile_name;
	char* reconfile_name;
	char* atzfile_name;
	switch(argc){
        case 1:{//if we get nothing from the CLI
            cout<<"Error: no input specified"<<endl;
            cout<<"Usage: antiz.exe <input file> <switches>"<<endl;
            cout<<"Valid switches:"<<endl;
            cout<<"-r : assume the input file is an ATZ file, skip to reconstruction"<<endl;
            pauser();
            return -1;
            break;
        }
        case 2:{//if we only get a filename from the CLI
            cout<<"Input file: "<<argv[1]<<endl;
            infile_name=argv[1];
            atzfile_name= new char[strlen(argv[1])+5];
            memset(atzfile_name, 0, (strlen(argv[1])+5));//null out the entire string
            strcpy(atzfile_name, argv[1]);
            atzfile_name=strcat(atzfile_name, ".atz");

            reconfile_name= new char[strlen(argv[1])+5];
            memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
            strcpy(reconfile_name, argv[1]);
            reconfile_name=strcat(reconfile_name, ".rec");
            cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<endl;
            break;
        }
        case 3:{//if we get at least two strings try to use the second as a switch
            if (strcmp(argv[2], "-r")==0){//if we get -r, treat the file as an ATZ file and skip to reconstruction
                atzfile_name=argv[1];

                reconfile_name= new char[strlen(argv[1])+5];
                memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(reconfile_name, argv[1]);
                reconfile_name=strcat(reconfile_name, ".rec");
                cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<endl;
                cout<<"overwriting "<<reconfile_name<<" if present"<<endl;
                goto PHASE5;
            }else{//if the third string is not a valid switch then ignore it
                cout<<"invalid switch specified, ignoring"<<endl;
                infile_name=argv[1];
                atzfile_name= new char[strlen(argv[1])+5];
                memset(atzfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(atzfile_name, argv[1]);
                atzfile_name=strcat(atzfile_name, ".atz");

                reconfile_name= new char[strlen(argv[1])+5];
                memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(reconfile_name, argv[1]);
                reconfile_name=strcat(reconfile_name, ".rec");
                cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<endl;
                break;
            }
        }
        default:{//if there are more than 2 strings ignore them
            if (strcmp(argv[2], "-r")==0){//if we get -r, treat the file as an ATZ file and skip to reconstruction
                cout<<"some switch(es) were invalid, ignoring"<<endl;
                atzfile_name=argv[1];

                reconfile_name= new char[strlen(argv[1])+5];
                memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(reconfile_name, argv[1]);
                reconfile_name=strcat(reconfile_name, ".rec");
                cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<endl;
                cout<<"overwriting "<<reconfile_name<<" if present"<<endl;
                goto PHASE5;
            }else{//if the third string is not a valid switch then ignore it
                cout<<"invalid switches specified, ignoring"<<endl;
                infile_name=argv[1];
                atzfile_name= new char[strlen(argv[1])+5];
                memset(atzfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(atzfile_name, argv[1]);
                atzfile_name=strcat(atzfile_name, ".atz");

                reconfile_name= new char[strlen(argv[1])+5];
                memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(reconfile_name, argv[1]);
                reconfile_name=strcat(reconfile_name, ".rec");
                cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<endl;
                break;
            }
        }
    }
	infile.open(infile_name, std::ios::in | std::ios::binary);
	if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
       pauser();
 	   abort();
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
    //take the information created in phase 3 and use it to create an ATZ file(see ATZ file format spec.)
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
    /*delete [] infile_name;
    delete [] atzfile_name;
    delete [] reconfile_name;*/
	return 0;
}
