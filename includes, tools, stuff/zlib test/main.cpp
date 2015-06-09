#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <zlib.h>

#define filename "zlibtest.bin"
#define filename_out "zlibtest_out.bin"
#define clevel 9
#define windowbits 14
#define memlevel 9

void pause(){
    std::string dummy;
    std::cout << "Press enter to continue...";
    std::getline(std::cin, dummy);
}


int main() {
	using std::cout;
	using std::endl;
	using std::cin;
	using std::vector;
	// opening file
	std::ifstream infile(filename, std::ios::in | std::ios::binary);
	if (!infile.is_open()) {
       cout << "error: open file for input failed!" << endl;
       pause();
 	   abort();
	}
	std::ofstream outfile(filename_out, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outfile.is_open()) {
       cout << "error: open file for output failed!" << endl;
       pause();
 	   abort();
	}
	//getting the size of the file
	struct stat statresults;
    if (stat(filename, &statresults) == 0){
    	//cout<<"File size:"<<statresults.st_size<<endl;
    	//pause();
    }
    else{
    	cout<<"Error determining file size."<<endl;
    	pause();
    	abort();
    }
    unsigned long int infileSize=statresults.st_size;
    //setting up read buffer and reading the entire file into the buffer
    unsigned char* rBuffer = new unsigned char[infileSize];
    infile.read(reinterpret_cast<char*>(rBuffer), infileSize);
    infile.clear();
    infile.close();

    //print the first 8 bytes
    /*cout<<"First 8 bytes: ";
    unsigned long int i;
    for(i=0;i<8;i++) {
    	cout<<static_cast<unsigned short>(rBuffer[i])<<";";
	}*/
	//cout<<endl;
	//pause();
	//setting up the Zlib stream
	int ret;
	z_stream strm1;
	strm1.zalloc = Z_NULL;
    strm1.zfree = Z_NULL;
    strm1.opaque = Z_NULL;
    strm1.next_in=rBuffer;
    //initialize the deflate stream
 	ret = deflateInit2(&strm1, clevel, Z_DEFLATED, windowbits, memlevel, Z_DEFAULT_STRATEGY); //second value is the zlib compression level to be used
    if (ret != Z_OK){
    	cout<<"deflateInit() failed with exit code:"<<ret<<endl;
    	pause();
    	return ret;
    }
    //prepare for compressing in one pass
    strm1.avail_in=infileSize;
    unsigned char* compBuffer=new unsigned char[deflateBound(&strm1, infileSize)]; //allocate output for worst case
    strm1.avail_out=deflateBound(&strm1, infileSize);
    strm1.next_out=compBuffer;
    ret=deflate(&strm1, Z_FINISH);//do the actual compression
    //check the return value to see if everything went well
    if (ret!=Z_STREAM_END){
        cout<<"deflate() failed with exit code:"<<ret<<endl;
        pause();
        return ret;
    }
    cout<<"compressed "<<strm1.total_in<<" bytes to "<<strm1.total_out<<" bytes"<<endl;
    unsigned long int compSize=strm1.total_out;
    outfile.write(reinterpret_cast<char*>(compBuffer), strm1.total_out);
    outfile.clear();
    outfile.close();
    //deallocate the Zlib stream and check if it went well
    ret=deflateEnd(&strm1);
    if (ret!=Z_OK){
        cout<<"deflateEnd() failed with exit code:"<<ret<<endl;
        pause();
        return ret;
    }
    //pause();
    //create a new Zlib stream to do decompression
    z_stream strm2;
    strm2.zalloc = Z_NULL;
    strm2.zfree = Z_NULL;
    strm2.opaque = Z_NULL;
    strm2.avail_in= compSize;
    strm2.next_in=compBuffer;
    //initialize the stream for decompression and check for error
    ret=inflateInit(&strm2);
    if (ret != Z_OK){
    	cout<<"inflateInit() failed with exit code:"<<ret<<endl;
    	pause();
    	return ret;
    }
    //creating a buffer for the decompressed data
    unsigned char* decompBuffer= new unsigned char[infileSize];
    strm2.next_out=decompBuffer;
    strm2.avail_out=infileSize;
    ret=inflate(&strm2, Z_FINISH);
    if (ret!=Z_STREAM_END){
        cout<<"inflate() failed with exit code:"<<ret<<endl;
        pause();
        return ret;
    }
    //cout<<"decompressed "<<strm2.total_in<<" bytes to "<<strm2.total_out<<" bytes"<<endl;
    ret=inflateEnd(&strm2);
    if (ret!=Z_OK){
        cout<<"inflateEnd() failed with exit code:"<<ret<<endl;
        pause();
        return ret;
    }
    //pause();
    /*cout<<"First 8 bytes: ";
    for(i=0;i<8;i++) {
    	cout<<static_cast<unsigned short>(decompBuffer[i])<<";";
	}
	cout<<endl;
	pause();*/
	/*int err=0;
	for(i=0;i<infileSize;i++){
        if ((decompBuffer[i]-rBuffer[i])!=0){
            err++;
        }
	}*/
	//cout<<"errors: "<<err<<endl;
	//pause();
    delete decompBuffer;
    delete compBuffer;
    delete rBuffer;

}
