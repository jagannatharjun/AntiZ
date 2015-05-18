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

void pause(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}

class fileOffset{
public:
    fileOffset(){
        offset=0;
        offsetType=1;
        abort();//the default constructor should not be used in this version
    }
    fileOffset(int_fast64_t os, int ot){
        offset=os;
        offsetType=ot;
    }
    int_fast64_t offset;
    int offsetType;
};

class streamOffset{
public:
    streamOffset(){
        offset=0;
        offsetType=1;
        streamLength=1;
        inflatedLength=1;
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

void searchBuffer(unsigned char buffer[], std::vector<fileOffset>& offsets, uint_fast64_t buffLen){
    //this function searches a buffer for zlib headers, count them and fill a vector of fileOffsets

    //try to guess the number of potential zlib headers in the file from the file size
	//this value is purely empirical, may need tweaking
	offsets.reserve(static_cast<int_fast64_t>(buffLen/1912));
	#ifdef debug
	std::cout<<"Offset list initial capacity:"<<offsets.capacity()<<std::endl;
	pause();
	#endif

	uint_fast64_t i;
	uint_fast64_t redlen=buffLen-1;//it is pointless to test the last byte and it could cause and out of bounds read
	//a new variable is used so the substraction is only performed once, not every time it loops

    for(i=0;i<redlen;i++){
        //search for 7801, 785E, 789C, 78DA, 68DE, 6881, 6843, 6805, 58C3, 5885, 5847, 5809,
        //           48C7, 4889, 484B, 480D, 38CB, 388D, 384F, 3811, 28CF, 2891, 2853, 2815
	    int hbits = buffer[i]>>4;//upper 4 bits
	    int lbits = buffer[i]&15;//lower 4 bits
        if ((lbits==8)&&(hbits>=2)&&(hbits<=7)){//if the current byte is good check the next byte
            int v = buffer[i+1];
            v=(v&(255-32-1))+((v&32)?1:0)+(v&1)*32;//swap 1st and 5th bit
            if ((v+hbits*4)%62==60){
                #ifdef debug
                std::cout<<"Found zlib header("<<std::hex<<std::setfill('0')<<std::uppercase<<std::setw(2)<<(int)buffer[i]
                <<" "<<std::setw(2)<<(int)buffer[i+1]<<std::dec<<") with "<<(1<<(hbits-2))<<"K window at offset: "<<i<<std::endl;
                #endif // debug
                switch(hbits){
					case 7://hex 78
					{
						switch(buffer[i+1]){
							case 1:{//hex 78 01
								offsets.push_back(fileOffset(i, 1));
								break;
							}
							case 94:{//hex 78 5E
								offsets.push_back(fileOffset(i, 2));
								break;
							}
							case 156:{//hex 78 9C
								offsets.push_back(fileOffset(i, 3));
								break;
							}
							case 218:{//hex 78 DA
								offsets.push_back(fileOffset(i, 4));
								break;
							}
						}
						break;//from case 7
					}
					case 6://hex 68
					{
						switch(buffer[i+1]){
							case 222:{//hex 68 DE
								offsets.push_back(fileOffset(i, 5));
								break;
							}
							case 129:{//hex 68 81
								offsets.push_back(fileOffset(i, 6));
								break;
							}
							case 67:{//hex 68 43
								offsets.push_back(fileOffset(i, 7));
								break;
							}
							case 5:{//hex 68 05
								offsets.push_back(fileOffset(i, 8));
								break;
							}
						}
						break;//from case 6
					}
					case 5://hex 58
					{
						switch(buffer[i+1]){
							case 195:{//hex 58 C3
								offsets.push_back(fileOffset(i, 9));
								break;
							}
							case 133:{//hex 58 85
								offsets.push_back(fileOffset(i, 10));
								break;
							}
							case 71:{//hex 58 47
								offsets.push_back(fileOffset(i, 11));
								break;
							}
							case 9:{//hex 58 09
								offsets.push_back(fileOffset(i, 12));
								break;
							}
						}
						break;//from case 5
					}
					case 4://hex 48
					{
						switch(buffer[i+1]){
							case 199:{//hex 48 C7
								offsets.push_back(fileOffset(i, 13));
								break;
							}
							case 137:{//hex 48 89
								offsets.push_back(fileOffset(i, 14));
								break;
							}
							case 75:{//hex 48 4B
								offsets.push_back(fileOffset(i, 15));;
								break;
							}
							case 13:{//hex 48 0D
								offsets.push_back(fileOffset(i, 16));
								break;
							}
						}
						break;//from case 4
					}
					case 3://hex 38
					{
						switch(buffer[i+1]){
							case 203:{
								offsets.push_back(fileOffset(i, 17));
								break;
							}
							case 141:{
								offsets.push_back(fileOffset(i, 18));
								break;
							}
							case 79:{
								offsets.push_back(fileOffset(i, 19));
								break;
							}
							case 17:{
								offsets.push_back(fileOffset(i, 20));
								break;
							}
						}
						break;//from case 3
					}
					case 2://hex 28
					{
						switch(buffer[i+1]){
							case 207:{
								offsets.push_back(fileOffset(i, 21));
								break;
							}
							case 145:{
								offsets.push_back(fileOffset(i, 22));
								break;
							}
							case 83:{
								offsets.push_back(fileOffset(i, 23));
								break;
							}
							case 21:{
								offsets.push_back(fileOffset(i, 24));
								break;
							}
						}
						break;//from case 2
                    }
                }
            }
        }
    }
    #ifdef debug
    std::cout<<std::endl;
	std::cout<<"Number of collected offsets:"<<offsets.size()<<std::endl;
    pause();
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
			pause();
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
            pause();
            abort();
        }
        delete [] decompBuffer;
        if (ret!=Z_BUF_ERROR) break;
        memScale++;//increase buffer size for the next iteration
    };
    return success;
}

void testOffsetList(unsigned char buffer[], uint64_t bufflen, std::vector<fileOffset>& fileoffsets, std::vector<streamOffset>& streamoffsets){
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
	uint64_t lastos=0;
    uint64_t lastlen=0;
    uint64_t atzlen=0;//placeholder for the length of the atz file
    std::ofstream outfile;
    uint_fast64_t numGoodOffsets;
    int_fast64_t identicalBytes;
    #ifdef debug
    int_fast64_t numFullmatch=0;
    #endif // debug
    uint_fast64_t j=0;
    int memlevel=9;
    int clevel=9;
    int window=15;
    bool fullmatch=false;
    z_stream strm1;
    uint64_t recomp=0;

    int recompTresh=128;//streams are only recompressed if the best match differs from the original in <= recompTresh bytes
    int sizediffTresh=128;//streams are only compared when the size difference is <= sizediffTresh
    //DO NOT turn off slowmode, the alternative code (optimized mode) does not work at all
    bool slowmode=true;//slowmode bruteforces the zlib parameters, optimized mode only tries probable parameters based on the 2-byte header
    int_fast64_t concentrate=-404;//only try to recompress the stream# givel here, -1 disables this and runs on all streams

	int ret=-9;
	vector<streamOffset> streamOffsetList;
	z_stream strm;

	//offsetList stores memory offsets where potential headers can be found, and the type of the offset
	vector<fileOffset> offsetList;
	uint_fast64_t i;
	unsigned char* rBuffer;
	std::ifstream infile;

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
	if (argc>=2){// if we get at least one string use it as input file name
        cout<<"Input file: "<<argv[1]<<endl;
        if (argc>=3){//if we get at least two strings use the second as a parameter
            if (strcmp(argv[2], "-r")==0){//if we get -r, treat the file as an ATZ file and skip to reconstruction
                atzfile_name=argv[1];

                reconfile_name= new char[strlen(argv[1])+5];
                memset(reconfile_name, 0, (strlen(argv[1])+5));//null out the entire string
                strcpy(reconfile_name, argv[1]);
                reconfile_name=strcat(reconfile_name, ".rec");
                cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<endl;
                cout<<"overwriting "<<reconfile_name<<" if present"<<endl;
                goto PHASE5;
            }else{//if the third stig is not a valid parameter then ignore it
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
            }
        }else{//if we get only the filename go forward to creating an ATZ file from it
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
        }
	}else{//if we get nothing from the CLI
        cout<<"no input specified, trying to open test.bin"<<endl;
        infile_name= new char[9];
        infile_name=default_infile;
        cout<<"overwriting atztest.atz and recon.bin if present"<<endl;
        atzfile_name= new char[12];
        atzfile_name=default_atzfile;
        reconfile_name= new char[10];
        reconfile_name=default_reconfile;
	}

	infile.open(infile_name, std::ios::in | std::ios::binary);
	if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
       pause();
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
    pause();
    #endif // debug

    //PHASE 3
    //start trying to find the parameters to use for recompression

    numGoodOffsets=streamOffsetList.size();
    for (j=0; j<numGoodOffsets; j++){
        if ((concentrate>=0)&&(j==0)) {
            j=concentrate;
            numGoodOffsets=concentrate;
        }
        //reset the Zlib stream to do decompression
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        fullmatch=false;
        memlevel=9;
        window=15;
        //the lengths of the zlib streams have been saved by the previous phase
        strm.avail_in = streamOffsetList[j].streamLength;
        strm.next_in=rBuffer+streamOffsetList[j].offset;//this is effectively adding an integer to a pointer, resulting in a pointer
        //initialize the stream for decompression and check for error
        ret=inflateInit(&strm);
        if (ret != Z_OK)
        {
            cout<<"inflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
            pause();
            abort();
        }
        //a buffer needs to be created to hold the resulting decompressed data
        //since we have already deompressed the data before, we know exactly how large of a buffer we need to allocate
        unsigned char* decompBuffer= new unsigned char[streamOffsetList[j].inflatedLength];
        strm.next_out=decompBuffer;
        strm.avail_out=streamOffsetList[j].inflatedLength;
        ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        //check the return value
        switch (ret){
            case Z_STREAM_END: //decompression was succesful
            {
                #ifdef debug
                cout<<endl;
                cout<<"stream #"<<j<<"("<<streamOffsetList[j].offset<<")"<<" ready for recompression trials"<<endl;
                /*if(streamOffsetList[j].offset!=9887540){//debug code!!!! DISABLE IT UNLESS NEEDED
                    window=10;
                    clevel=1;
                    memlevel=1;
                }*/
                #endif // debug
                if (slowmode){
                    #ifdef debug
                    /*cout<<"   entering slow mode"<<endl;
                    cout<<"   stream type: "<<streamOffsetList[j].offsetType<<endl;
                    pause();*/
                    #endif // debug
                    do{
                        memlevel=9;
                        do {
                            clevel=9;
                            do {
                                //resetting the variables
                                strm1.zalloc = Z_NULL;
                                strm1.zfree = Z_NULL;
                                strm1.opaque = Z_NULL;
                                strm1.next_in=decompBuffer;
                                #ifdef debug
                                cout<<"-------------------------"<<endl;
                                cout<<"   memlevel:"<<memlevel<<endl;
                                cout<<"   clevel:"<<clevel<<endl;
                                cout<<"   window:"<<window<<endl;
                                #endif // debug
                                //use all default settings except clevel and memlevel
                                ret = deflateInit2(&strm1, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate stream init done"<<endl;
                                #endif // debug

                                //prepare for compressing in one pass
                                strm1.avail_in=streamOffsetList[j].inflatedLength;
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                strm1.next_out=recompBuffer;
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                                //check the return value to see if everything went well
                                if (ret != Z_STREAM_END){
                                    cout<<"recompression failed with exit code:"<<ret<<endl;
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate done"<<endl;
                                #endif // debug

                                //test if the recompressed stream matches the input data
                                if (strm1.total_out!=streamOffsetList[j].streamLength){
                                    identicalBytes=0;
                                    //cout<<"   size difference: "<<(strm1.total_out-static_cast<int64_t>(streamOffsetList[j].streamLength))<<endl;
                                    if (abs((strm1.total_out-streamOffsetList[j].streamLength))>sizediffTresh){
                                        #ifdef debug
                                        cout<<"   size difference is greater than "<<sizediffTresh<<" bytes, not comparing"<<endl;
                                        #endif // debug
                                    } else {
                                        if (strm1.total_out<streamOffsetList[j].streamLength){
                                            for (i=0; i<strm1.total_out;i++){
                                                if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
                                                    identicalBytes++;
                                                }
                                            }
                                        } else {
                                            for (i=0; i<streamOffsetList[j].streamLength;i++){
                                                if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
                                                    identicalBytes++;
                                                }
                                            }
                                        }
                                        #ifdef debug
                                        cout<<"   "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                        #endif // debug
                                        if (identicalBytes>streamOffsetList[j].identBytes){//if this recompressed stream has more matching bytes than the previous best
                                            streamOffsetList[j].identBytes=identicalBytes;
                                            streamOffsetList[j].clevel=clevel;
                                            streamOffsetList[j].memlvl=memlevel;
                                            streamOffsetList[j].window=window;
                                            streamOffsetList[j].firstDiffByte=-1;
                                            streamOffsetList[j].diffByteOffsets.clear();
                                            streamOffsetList[j].diffByteVal.clear();
                                            int_fast64_t last_i=0;
                                            if (strm1.total_out<streamOffsetList[j].streamLength){//the recompressed stream is shorter than the original
                                                for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                                                    if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                        if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                            streamOffsetList[j].firstDiffByte=(i);
                                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            #ifdef debug
                                                            cout<<"   first diff byte:"<<i<<endl;
                                                            #endif // debug
                                                            last_i=i;
                                                        } else {
                                                            streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            //cout<<"   different byte:"<<i<<endl;
                                                            last_i=i;
                                                        }
                                                    }
                                                }
                                                for (i=0; i<(streamOffsetList[j].streamLength-strm1.total_out); i++){
                                                    if ((i==0)&&((last_i+1)<strm1.total_out)){
                                                        streamOffsetList[j].diffByteOffsets.push_back(strm1.total_out-last_i);
                                                    } else{
                                                        streamOffsetList[j].diffByteOffsets.push_back(1);
                                                    }
                                                    streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+strm1.total_out+streamOffsetList[j].offset)]);
                                                    #ifdef debug
                                                    cout<<"   byte at the end added :"<<+rBuffer[(i+strm1.total_out+streamOffsetList[j].offset)]<<endl;
                                                    #endif // debug
                                                }
                                            } else {//the recompressed stream is longer than the original
                                                for (i=0; i<streamOffsetList[j].streamLength;i++){
                                                    if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                        if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                            streamOffsetList[j].firstDiffByte=(i);
                                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            #ifdef debug
                                                            cout<<"   first diff byte:"<<i<<endl;
                                                            #endif // debug
                                                            last_i=i;
                                                        } else {
                                                            streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                            //cout<<"   different byte:"<<i<<endl;
                                                            last_i=i;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    clevel--;
                                } else {
                                    #ifdef debug
                                    cout<<"   stream sizes match, comparing"<<endl;
                                    #endif // debug
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if (recompBuffer[i]==rBuffer[(i+streamOffsetList[j].offset)]){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamOffsetList[j].streamLength){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        numFullmatch++;
                                        #endif // debug
                                        fullmatch=true;
                                        streamOffsetList[j].identBytes=identicalBytes;
                                        streamOffsetList[j].clevel=clevel;
                                        streamOffsetList[j].memlvl=memlevel;
                                        streamOffsetList[j].window=window;
                                        streamOffsetList[j].firstDiffByte=-1;
                                        streamOffsetList[j].diffByteOffsets.clear();
                                        streamOffsetList[j].diffByteVal.clear();
                                    } else {
                                        #ifdef debug
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                        #endif // debug
                                        if (((streamOffsetList[j].streamLength-identicalBytes)==2)&&((recompBuffer[0]-rBuffer[streamOffsetList[j].offset])!=0)&&((recompBuffer[1]-rBuffer[(1+streamOffsetList[j].offset)])!=0)){
                                            #ifdef debug
                                            cout<<"   2 byte header mismatch, accepting"<<endl;
                                            numFullmatch++;
                                            #endif // debug
                                            fullmatch=true;
                                            streamOffsetList[j].identBytes=identicalBytes;
                                            streamOffsetList[j].clevel=clevel;
                                            streamOffsetList[j].memlvl=memlevel;
                                            streamOffsetList[j].window=window;
                                            streamOffsetList[j].firstDiffByte=0;
                                            streamOffsetList[j].diffByteOffsets.clear();
                                            streamOffsetList[j].diffByteVal.clear();
                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                            streamOffsetList[j].diffByteOffsets.push_back(1);
                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[streamOffsetList[j].offset]);
                                            streamOffsetList[j].diffByteVal.push_back(rBuffer[(1+streamOffsetList[j].offset)]);
                                        }
                                        if (((streamOffsetList[j].streamLength-identicalBytes)==1)&&(((recompBuffer[0]-rBuffer[streamOffsetList[j].offset])!=0)||((recompBuffer[1]-rBuffer[(1+streamOffsetList[j].offset)])!=0))){
                                            #ifdef debug
                                            cout<<"   1 byte header mismatch, accepting"<<endl;
                                            numFullmatch++;
                                            #endif // debug
                                            fullmatch=true;
                                            streamOffsetList[j].identBytes=identicalBytes;
                                            streamOffsetList[j].clevel=clevel;
                                            streamOffsetList[j].memlvl=memlevel;
                                            streamOffsetList[j].window=window;
                                            streamOffsetList[j].diffByteOffsets.clear();
                                            streamOffsetList[j].diffByteVal.clear();
                                            if (recompBuffer[0]!=rBuffer[streamOffsetList[j].offset]){
                                                streamOffsetList[j].firstDiffByte=0;
                                                streamOffsetList[j].diffByteVal.push_back(rBuffer[streamOffsetList[j].offset]);
                                            } else {
                                                streamOffsetList[j].firstDiffByte=1;
                                                streamOffsetList[j].diffByteVal.push_back(rBuffer[(1+streamOffsetList[j].offset)]);
                                            }
                                            streamOffsetList[j].diffByteOffsets.push_back(0);
                                        }
                                        if ((identicalBytes>streamOffsetList[j].identBytes)&&!fullmatch){
                                            streamOffsetList[j].identBytes=identicalBytes;
                                            streamOffsetList[j].clevel=clevel;
                                            streamOffsetList[j].memlvl=memlevel;
                                            streamOffsetList[j].window=window;
                                            streamOffsetList[j].firstDiffByte=-1;
                                            streamOffsetList[j].diffByteOffsets.clear();
                                            streamOffsetList[j].diffByteVal.clear();
                                            int_fast64_t last_i=0;
                                            for (i=0; i<strm1.total_out; i++){//compare the streams byte-by-byte
                                                if (recompBuffer[i]!=rBuffer[(i+streamOffsetList[j].offset)]){//if a mismatching byte is found
                                                    if (streamOffsetList[j].firstDiffByte<0){//if the first different byte is negative, then this is the first
                                                        streamOffsetList[j].firstDiffByte=(i);
                                                        streamOffsetList[j].diffByteOffsets.push_back(0);
                                                        streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                        #ifdef debug
                                                        cout<<"   first diff byte:"<<i<<endl;
                                                        #endif // debug
                                                        last_i=i;
                                                    } else {
                                                        streamOffsetList[j].diffByteOffsets.push_back(i-last_i);
                                                        streamOffsetList[j].diffByteVal.push_back(rBuffer[(i+streamOffsetList[j].offset)]);
                                                        #ifdef debug
                                                        //cout<<"   different byte:"<<i<<endl;
                                                        #endif // debug
                                                        last_i=i;
                                                    }
                                                }
                                            }
                                        }
                                        clevel--;
                                    }
                                }

                                //deallocate the Zlib stream and check if it went well
                                ret=deflateEnd(&strm1);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                delete [] recompBuffer;
                                #ifdef debug
                                cout<<"   deflate stream end done"<<endl;
                                #endif // debug
                            } while ((!fullmatch)&&(clevel>=1));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        window--;
                    } while ((!fullmatch)&&(window>=10));
                } else {
                #ifdef debug
                cout<<"   entering optimized mode"<<endl;
                #endif // debug
                /*switch (streamOffsetList[j].offsetType){
                    case 1:{
                        #ifdef debug
                        cout<<"   stream type: 1"<<endl;
                        #endif // debug
                        do {
                            //resetting the variables
                            strm1.zalloc = Z_NULL;
                            strm1.zfree = Z_NULL;
                            strm1.opaque = Z_NULL;
                            strm1.next_in=decompBuffer;
                            #ifdef debug
                            cout<<"   memlevel:"<<memlevel<<endl;
                            #endif // debug
                            //use all default settings except clevel and memlevel
                            ret = deflateInit2(&strm1, 1, Z_DEFLATED, 15, memlevel, Z_DEFAULT_STRATEGY); //only try clevel 1, 0 would be no compression, would be pointless
                            if (ret != Z_OK)
                            {
                                cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                pause();
                                abort();
                            }
                            #ifdef debug
                            cout<<"   deflate stream init done"<<endl;
                            #endif // debug

                            //prepare for compressing in one pass
                            strm1.avail_in=streamOffsetList[j].inflatedLength;
                            unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                            strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                            strm1.next_out=recompBuffer;
                            ret=deflate(&strm1, Z_FINISH);//do the actual compression
                            //check the return value to see if everything went well
                            if (ret != Z_STREAM_END){
                                cout<<"recompression failed with exit code:"<<ret<<endl;
                                pause();
                                abort();
                            }
                            #ifdef debug
                            //cout<<"   deflate done"<<endl;
                            #endif // debug

                            //test if the recompressed stream matches the input data
                            if (strm1.total_out!=streamOffsetList[j].streamLength){
                                cout<<"   recompression failed, size difference"<<endl;
                                memlevel--;
                            } else {
                                #ifdef debug
                                cout<<"   stream sizes match, comparing"<<endl;
                                #endif // debug
                                identicalBytes=0;
                                for (i=0; i<strm1.total_out;i++){
                                    if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j].offset)])==0){
                                        identicalBytes++;
                                    }
                                }
                                if (identicalBytes==streamOffsetList[j].streamLength){
                                    #ifdef debug
                                    cout<<"   recompression succesful, full match"<<endl;
                                    #endif // debug
                                    fullmatch=true;
                                    numFullmatch++;
                                } else {
                                    #ifdef debug
                                    cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                    pause();
                                    #endif // debug
                                    memlevel--;
                                }
                            }

                            //deallocate the Zlib stream and check if it went well
                            ret=deflateEnd(&strm1);
                            if (ret != Z_OK)
                            {
                                cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                pause();
                                abort();
                            }
                            delete [] recompBuffer;
                            #ifdef debug
                            cout<<"   deflate stream end done"<<endl;
                            #endif // debug
                        } while ((!fullmatch)&&(memlevel>=1));
                        break;
                    }
                    case 4:{
                        #ifdef debug
                        cout<<"   stream type: 4"<<endl;
                        #endif // debug
                        do {
                            clevel=9;
                            do {
                                //resetting the variables
                                strm1.zalloc = Z_NULL;
                                strm1.zfree = Z_NULL;
                                strm1.opaque = Z_NULL;
                                strm1.next_in=decompBuffer;
                                #ifdef debug
                                cout<<"   memlevel:"<<memlevel<<endl;
                                cout<<"   clevel:"<<clevel<<endl;
                                #endif // debug
                                //use all default settings except clevel and memlevel
                                ret = deflateInit2(&strm1, clevel, Z_DEFLATED, 15, memlevel, Z_DEFAULT_STRATEGY);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                cout<<"   deflate stream init done"<<endl;
                                #endif // debug

                                //prepare for compressing in one pass
                                strm1.avail_in=streamOffsetList[j].inflatedLength;
                                unsigned char* recompBuffer=new unsigned char[deflateBound(&strm1, streamOffsetList[j].inflatedLength)]; //allocate output for worst case
                                strm1.avail_out=deflateBound(&strm1, streamOffsetList[j].inflatedLength);
                                strm1.next_out=recompBuffer;
                                ret=deflate(&strm1, Z_FINISH);//do the actual compression
                                //check the return value to see if everything went well
                                if (ret != Z_STREAM_END){
                                    cout<<"recompression failed with exit code:"<<ret<<endl;
                                    pause();
                                    abort();
                                }
                                #ifdef debug
                                //cout<<"   deflate done"<<endl;
                                #endif // debug

                                //test if the recompressed stream matches the input data
                                if (strm1.total_out!=streamOffsetList[j].streamLength){
                                    #ifdef debug
                                    cout<<"   recompression failed, size difference"<<endl;
                                    #endif // debug
                                    clevel--;
                                } else {
                                    #ifdef debug
                                    cout<<"   stream sizes match, comparing"<<endl;
                                    #endif // debug
                                    identicalBytes=0;
                                    for (i=0; i<strm1.total_out;i++){
                                        if ((recompBuffer[i]-rBuffer[(i+streamOffsetList[j].offset)])==0){
                                            identicalBytes++;
                                        }
                                    }
                                    if (identicalBytes==streamOffsetList[j].streamLength){
                                        #ifdef debug
                                        cout<<"   recompression succesful, full match"<<endl;
                                        #endif // debug
                                        fullmatch=true;
                                        numFullmatch++;
                                    } else {
                                        #ifdef debug
                                        cout<<"   partial match, "<<identicalBytes<<" bytes out of "<<streamOffsetList[j].streamLength<<" identical"<<endl;
                                        pause();
                                        #endif // debug
                                        clevel--;
                                    }
                                }

                                //deallocate the Zlib stream and check if it went well
                                ret=deflateEnd(&strm1);
                                if (ret != Z_OK)
                                {
                                    cout<<"deflateInit() failed with exit code:"<<ret<<endl;//should never happen normally
                                    pause();
                                    abort();
                                }
                                delete [] recompBuffer;
                                #ifdef debug
                                cout<<"   deflate stream end done"<<endl;
                                #endif // debug
                            } while ((!fullmatch)&&(clevel>=7));
                            memlevel--;
                        } while ((!fullmatch)&&(memlevel>=1));
                        break;
                    }
                }*/
                }
                break;
            }
            case Z_DATA_ERROR: //the compressed data was invalid, this should never happen since the offsets have been checked
            {
                cout<<"inflate() failed with data error"<<endl;
                pause();
                abort();
            }
            case Z_BUF_ERROR: //this should not happen since the decompressed lengths are known
            {
                cout<<"inflate() failed with memory error"<<endl;
                pause();
                abort();
            }
            default: //shit hit the fan, should never happen normally
            {
                cout<<"inflate() failed with exit code:"<<ret<<endl;
                pause();
                abort();
            }
        }
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        ret=inflateEnd(&strm);
        if (ret!=Z_OK)
        {
            cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
            pause();
            abort();
        }
        delete [] decompBuffer;
    }
    if (concentrate>=0){
        numGoodOffsets=streamOffsetList.size();
    }
    cout<<endl;
    #ifdef debug
    cout<<"fullmatch streams:"<<numFullmatch<<" out of "<<numGoodOffsets<<endl;
    cout<<"streamOffsetList.size():"<<streamOffsetList.size()<<endl;
    cout<<endl;
    pause();
    cout<<"Stream info"<<endl;
    #endif // debug
    for (j=0; j<streamOffsetList.size(); j++){
        #ifdef debug
        cout<<"-------------------------"<<endl;
        cout<<"   stream #"<<j<<endl;
        cout<<"   offset:"<<streamOffsetList[j].offset<<endl;
        cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<endl;
        cout<<"   clevel:"<<+streamOffsetList[j].clevel<<endl;
        cout<<"   window:"<<+streamOffsetList[j].window<<endl;
        cout<<"   best match:"<<streamOffsetList[j].identBytes<<" out of "<<streamOffsetList[j].streamLength<<endl;
        cout<<"   diffBytes:"<<streamOffsetList[j].diffByteOffsets.size()<<endl;
        cout<<"   diffVals:"<<streamOffsetList[j].diffByteVal.size()<<endl;
        #endif // debug
        if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=recompTresh)&&(streamOffsetList[j].identBytes>0)){
            recomp++;
            streamOffsetList[j].recomp=true;
        }
        #ifdef debug
        cout<<"   mismatched bytes:";
        for (i=0; i<streamOffsetList[j].diffByteOffsets.size(); i++){
            cout<<streamOffsetList[j].diffByteOffsets[i]<<";";
        }
        cout<<endl;
        #endif // debug
    }
    cout<<"recompressed:"<<recomp<<"/"<<streamOffsetList.size()<<endl;
    #ifdef debug
    pause();
    #endif // debug

    //PHASE 4
    //take the information created in phase 3 and use it to create an ATZ file(see ATZ file format spec.)
    outfile.open(atzfile_name, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outfile.is_open()) {
       cout << "error: open file for output failed!" << endl;
       pause();
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
                pause();
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
                    pause();
                    abort();
                }
            }
            //deallocate the zlib stream, check for errors
            ret=inflateEnd(&strm);
            if (ret!=Z_OK)
            {
                cout<<"inflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                pause();
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
    pause();
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
       pause();
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
        pause();
        abort();
    }
    atzlen=*reinterpret_cast<uint64_t*>(&atzBuffer[4]);
    if (atzlen!=infileSize){
        cout<<"atzlen mismatch"<<endl;
        pause();
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
        pause();
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
                        pause();
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
                            pause();
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pause();
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
                        pause();
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
                            pause();
                            abort();
                        }
                    }
                    //deallocate the zlib stream, check for errors
                    ret=deflateEnd(&strm);
                    if (ret!=Z_OK)
                    {
                        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;//should never happen normally
                        pause();
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
    pause();
    #endif // debug
    delete [] atzBuffer;
    /*delete [] infile_name;
    delete [] atzfile_name;
    delete [] reconfile_name;*/
	return 0;
}
