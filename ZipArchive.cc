#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include <ctime>
#include <cstring>

// local file header
struct LFH
{
  LFH( struct stat *fileInfo, std::string filename, uint32_t crc ) 
  {
    // todo: deal with this for ZIP64
    minZipVersion = ( 3 << 8 ) | 10 ;
    generalBitFlag = 0;
    compressionMethod = 0;
    ZCRC32 = crc;
    // todo: deal with this for ZIP64
    compressedSize = fileInfo->st_size;
    uncompressedSize = fileInfo->st_size;
    // todo: filepath vs filename
    this->filename = filename;
    filenameLength = this->filename.length();
    // todo: deal with this for ZIP64
    extra = "";
    extraLength = 0;
    
    SetDateTime( fileInfo );

    lfhSize = lfhBaseSize + filenameLength + extraLength;
  }
  
  void SetDateTime( struct stat *fileInfo )
  {
    // convert from Epoch time to local time
    struct tm *t = localtime( &fileInfo->st_mtime );
    // convert to MS-DOS time format
    uint16_t hour = t->tm_hour;
    uint16_t min = t->tm_min;
    uint16_t sec = t->tm_sec / 2;
    uint16_t year = t->tm_year - 80;
    uint16_t month = t->tm_mon + 1;
    uint16_t day = t->tm_mday;  
    
    lastModFileTime = ( hour << 11 ) | ( min << 5 ) | sec ;
    lastModFileDate =  ( year << 9 ) | ( month << 5 ) | day ;

    // tm format
    //std::cout << "hour: " << t->tm_hour << " min: " << t->tm_min << " sec: " << t->tm_sec << "\n";  
    //std::cout << "day: " << t->tm_mday << " month: " << t->tm_mon << " year: " << t->tm_year << "\n"; 
    // MS DOS format
    //std::cout << "hour: " << hour << " min: " << min << " sec: " << sec << "\n";  
    //std::cout << "day: " << day << " month: " << month << " year: " << year << "\n";
    //std::cout << "date: " << lastModFileDate << " time: " << lastModFileTime << "\n";
  }
 
  uint16_t minZipVersion;
  uint16_t generalBitFlag;
  uint16_t compressionMethod;
  uint16_t lastModFileTime;
  uint16_t lastModFileDate;
  uint32_t ZCRC32;
  uint32_t compressedSize;
  uint32_t uncompressedSize;
  uint16_t filenameLength;
  uint16_t extraLength;
  std::string filename;
  std::string extra;
  uint32_t lfhSize;
  
  static const uint32_t lfhBaseSize = 30;
  static const uint32_t lfhSign = 0x04034b50;
};

// central directory file header
struct CDFH
{
  CDFH( LFH *lfh )
  {
    zipVersion = ( 3 << 8 ) | 63;
    minZipVersion = lfh->minZipVersion;
    generalBitFlag = lfh->generalBitFlag;
    compressionMethod = lfh->compressionMethod;
    lastModFileTime = lfh->lastModFileTime;
    lastModFileDate = lfh->lastModFileDate;
    ZCRC32 = lfh->ZCRC32;
    compressedSize = lfh->compressedSize;
    uncompressedSize = lfh->uncompressedSize;
    filenameLength = lfh->filenameLength;
    extraLength = lfh->extraLength;
    commentLength = 0;
    diskNb = 0;
    internAttr = 0;
    externAttr = 0;
    // todo: different offset when appending
    // todo: deal with this for ZIP64
    offset = 0;
    filename = lfh->filename;
    extra = lfh->extra;
    comment = "";
    cdfhSize = cdfhBaseSize + filenameLength + extraLength + commentLength;
  }
  
  uint16_t zipVersion;
  uint16_t minZipVersion;
  uint16_t generalBitFlag;
  uint16_t compressionMethod;
  uint16_t lastModFileTime;
  uint16_t lastModFileDate;
  uint32_t ZCRC32;
  uint32_t compressedSize;
  uint32_t uncompressedSize;
  uint16_t filenameLength;
  uint16_t extraLength;
  uint16_t commentLength;
  uint16_t diskNb;
  uint16_t internAttr;
  uint32_t externAttr;
  uint32_t offset;
  std::string filename;
  std::string extra;
  std::string comment;
  uint32_t cdfhSize;
 
  static const uint32_t cdfhBaseSize = 46;
  static const uint32_t cdfhSign = 0x02014b50;
};

// end of central directory record
struct EOCD
{
  EOCD(LFH *lfh, CDFH *cdfh )
  {
    nbDisk = 0;
    nbDiskCd = 0;
    // todo: different when appending
    // todo: deal with this for ZIP64
    nbCdRecD = 1;
    nbCdRec = 1;
    cdSize = cdfh->cdfhSize;
    cdOffset = lfh->lfhSize + lfh->compressedSize;
    commentLength = 0;
    comment = "";
    eocdSize = eocdBaseSize + commentLength;
  }

  uint16_t nbDisk;
  uint16_t nbDiskCd;
  uint16_t nbCdRecD;
  uint16_t nbCdRec;
  uint32_t cdSize;
  uint32_t cdOffset;
  uint16_t commentLength;
  std::string comment;
  uint32_t eocdSize;

  static const uint16_t eocdBaseSize = 22;
  static const uint32_t eocdSign = 0x06054b50;
  // todo: store max size??
};

class ZipArchive
{
  public:

    ZipArchive( std::string inputFilename, std::string archiveFilename, uint32_t crc )
    {
      this->inputFilename = inputFilename;
      this->archiveFilename = archiveFilename;
      this->crc = crc;
    }
    
    void openArchive()
    {
      // file permissions: 644
      archiveFd = open( archiveFilename.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
      if ( archiveFd == -1 )
      {
        // todo: proper error handling
        std::cout << "Could not open " << archiveFilename << "\n";  
      }
    
      inputFd = open( inputFilename.c_str(), O_RDONLY );
      if ( inputFd == -1 )
      {
        // todo: proper error handling
        std::cout << "Could not open " << inputFilename << "\n";  
      }
    }
    
    void constructHeaders()
    {
      struct stat fileInfo;
      if ( fstat( inputFd, &fileInfo ) == -1 )
      {
        // todo: proper error handling
        std::cout << "Could not stat " << inputFilename << "\n";
      }
      
      lfh = new LFH( &fileInfo, inputFilename, crc );
      cdfh = new CDFH( lfh );
      eocd = new EOCD( lfh, cdfh );
    }

    void writeArchive()
    {
      writeLfh();
      writeFileData();
      writeCdfh();
      writeEocd();
    }

    void writeLfh()
    {
      uint32_t size = lfh->lfhSize;
      char buffer[size];
      std::memcpy( buffer, &lfh->lfhSign, 4 );
      std::memcpy( buffer + 4, &lfh->minZipVersion, 2 );
      std::memcpy( buffer + 6, &lfh->generalBitFlag, 2 );
      std::memcpy( buffer + 8, &lfh->compressionMethod, 2 );
      std::memcpy( buffer + 10, &lfh->lastModFileTime, 2 );
      std::memcpy( buffer + 12, &lfh->lastModFileDate, 2 );
      std::memcpy( buffer + 14, &lfh->ZCRC32, 4 );
      std::memcpy( buffer + 18, &lfh->compressedSize, 4 );
      std::memcpy( buffer + 22, &lfh->uncompressedSize, 4 );
      std::memcpy( buffer + 26, &lfh->filenameLength, 2 );
      std::memcpy( buffer + 28, &lfh->extraLength, 2 );
      std::memcpy( buffer + 30, lfh->filename.c_str(), lfh->filenameLength );
      std::memcpy( buffer + 30 + lfh->filenameLength, lfh->extra.c_str(), lfh->extraLength );
      
      // todo: error handling 
      write( archiveFd, buffer, size );
    }

    void writeCdfh()
    {
      uint32_t size = cdfh->cdfhSize;
      char buffer[size];
      std::memcpy( buffer, &cdfh->cdfhSign, 4 );
      std::memcpy( buffer + 4, &cdfh->zipVersion, 2 );
      std::memcpy( buffer + 6, &cdfh->minZipVersion, 2 );
      std::memcpy( buffer + 8, &cdfh->generalBitFlag, 2 );
      std::memcpy( buffer + 10, &cdfh->compressionMethod, 2 );
      std::memcpy( buffer + 12, &cdfh->lastModFileTime, 2 );
      std::memcpy( buffer + 14, &cdfh->lastModFileDate, 2 );
      std::memcpy( buffer + 16, &cdfh->ZCRC32, 4 );
      std::memcpy( buffer + 20, &cdfh->compressedSize, 4 );
      std::memcpy( buffer + 24, &cdfh->uncompressedSize, 4 );
      std::memcpy( buffer + 28, &cdfh->filenameLength, 2 );
      std::memcpy( buffer + 30, &cdfh->extraLength, 2 );
      std::memcpy( buffer + 32, &cdfh->commentLength, 2 );
      std::memcpy( buffer + 34, &cdfh->diskNb, 2 );
      std::memcpy( buffer + 36, &cdfh->internAttr, 2 );
      std::memcpy( buffer + 38, &cdfh->externAttr, 4 );
      std::memcpy( buffer + 42, &cdfh->offset, 4 );
      std::memcpy( buffer + 46, cdfh->filename.c_str(), cdfh->filenameLength );
      std::memcpy( buffer + 46 + cdfh->filenameLength, cdfh->extra.c_str(), cdfh->extraLength );
      std::memcpy( buffer + 46 + cdfh->filenameLength + cdfh->extraLength, cdfh->comment.c_str(), cdfh->commentLength );
      
      // todo: error handling 
      write( archiveFd, buffer, size );
    }

    void writeEocd()
    {
      uint32_t size = eocd->eocdSize;
      char buffer[size];
      std::memcpy( buffer, &eocd->eocdSign, 4 ); 
      std::memcpy( buffer + 4, &eocd->nbDisk, 2 );
      std::memcpy( buffer + 6, &eocd->nbDiskCd, 2 ); 
      std::memcpy( buffer + 8, &eocd->nbCdRecD, 2 ); 
      std::memcpy( buffer + 10, &eocd->nbCdRec, 2 ); 
      std::memcpy( buffer + 12, &eocd->cdSize, 4 ); 
      std::memcpy( buffer + 16, &eocd->cdOffset, 4 ); 
      std::memcpy( buffer + 20, &eocd->commentLength, 2 ); 
      std::memcpy( buffer + 22, eocd->comment.c_str(), eocd->commentLength ); 
      
      // todo: error handling
      write( archiveFd, buffer, size );
    }
    
    // only for testing purposes
    void writeFileData()
    {
      int bytes_read;
      int size = 1024;
      char buffer[size];
      do
      {
        // todo: error handling for read and write
        bytes_read = read( inputFd, buffer, size );
        write( archiveFd, buffer, bytes_read );
      } 
      while( bytes_read != 0 ); 
    }

    void closeArchive()
    {
      // todo: error handling
      close( inputFd );
      close ( archiveFd );
    }
 
  private:
    int inputFd;
    int archiveFd;
    std::string inputFilename;
    std::string archiveFilename;
    LFH *lfh;
    CDFH *cdfh;
    EOCD *eocd;
    uint32_t crc;
};

// run as ./ZipArchive <input filename> <output filename>
int main( int argc, char **argv )
{
  std::string inputFilename = "file.txt";
  std::string archiveFilename = "archive.zip"; 
  // this crc matches exactly my test file.txt
  uint32_t crc = 0x797b4b0e;
  if (argc >= 3)
  {
    inputFilename = argv[1];
    archiveFilename = argv[2];
  }
  else 
  {
    std::cout << "No args given, using defaults.\n";
  }
  std::cout << "Input file: " << inputFilename << "\n";
  std::cout << "Output file: " << archiveFilename << "\n";
  std::cout << "crc: " << std::hex << crc << "\n"; 

  ZipArchive *archive = new ZipArchive( inputFilename, archiveFilename, crc );
  archive->openArchive();
  archive->constructHeaders();
  archive->writeArchive();
  archive->closeArchive();
}

 
