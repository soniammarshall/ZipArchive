#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include <ctime>
#include <cstring>
#include <errno.h>

const uint32_t ovrflw32 = 0xffffffff;

// ZIP64 extended information extra field
struct ZipExtra
{
  ZipExtra( uint64_t fileSize )
  {
    offset = 0;
    nbDisk = 0;
    if ( fileSize > ovrflw32 )
    {
      dataSize = 16;
      uncompressedSize = fileSize;
      compressedSize = fileSize;
      totalSize = dataSize + 4;
    }
    else 
    {
      dataSize = 0;
      uncompressedSize = 0;
      compressedSize = 0;
      totalSize = 0;
    }
  }

  ZipExtra( ZipExtra *extra, uint64_t offset )
  {
    nbDisk = 0;
    uncompressedSize = extra->uncompressedSize;
    compressedSize = extra->compressedSize;
    dataSize = extra->dataSize;
    totalSize = extra->totalSize;
    if ( offset > ovrflw32 )
    {
      this->offset = offset;
      dataSize = 24;
      totalSize = dataSize + 4;
    }
    else
      this->offset = 0;
  }
 
  void concatenateFields( char *buffer )
  {
    std::memcpy( buffer, &headerID, 2 );
    std::memcpy( buffer + 2, &dataSize, 2 );
    std::memcpy( buffer + 4, &uncompressedSize, 8 );
    std::memcpy( buffer + 12, &compressedSize, 8 );
    if ( totalSize >= 28 )
      std::memcpy( buffer + 20, &offset, 8 );
    if ( totalSize >= 32 )
      std::memcpy( buffer + 28, &nbDisk, 4 );
  }
 
  static const uint16_t headerID = 0x0001;
  uint16_t dataSize;
  uint64_t uncompressedSize;
  uint64_t compressedSize;
  uint64_t offset;
  uint32_t nbDisk;
  uint16_t totalSize;
};

// local file header
struct LFH
{
  LFH( std::string filename, uint32_t crc, off_t fileSize, time_t time ) 
  {
    generalBitFlag = 0;
    compressionMethod = 0;
    ZCRC32 = crc;
    if ( fileSize > ovrflw32 ) 
    {
      compressedSize = ovrflw32;
      uncompressedSize = ovrflw32;
    }
    else
    {
      compressedSize = fileSize;
      uncompressedSize = fileSize;
    }
    extra = new ZipExtra( fileSize );
    extraLength = extra->totalSize;    
    if ( extraLength == 0 )
      minZipVersion = 10;
    else
      minZipVersion = 45;
    this->filename = filename;
    filenameLength = this->filename.length();
    
    MsdosDateTime( &time );

    lfhSize = lfhBaseSize + filenameLength + extraLength;
  }
  
  void MsdosDateTime( time_t *originalTime )
  {
    // convert from Epoch time to local time
    struct tm *t = localtime( originalTime );
    // convert to MS-DOS time format
    uint16_t hour = t->tm_hour;
    uint16_t min = t->tm_min;
    uint16_t sec = t->tm_sec / 2;
    uint16_t year = t->tm_year - 80;
    uint16_t month = t->tm_mon + 1;
    uint16_t day = t->tm_mday;  
    lastModFileTime = ( hour << 11 ) | ( min << 5 ) | sec ;
    lastModFileDate =  ( year << 9 ) | ( month << 5 ) | day ;
  }

  void Write( int archiveFd )
  {
    char buffer[lfhSize];
    std::memcpy( buffer, &lfhSign, 4 );
    std::memcpy( buffer + 4, &minZipVersion, 2 );
    std::memcpy( buffer + 6, &generalBitFlag, 2 );
    std::memcpy( buffer + 8, &compressionMethod, 2 );
    std::memcpy( buffer + 10, &lastModFileTime, 2 );
    std::memcpy( buffer + 12, &lastModFileDate, 2 );
    std::memcpy( buffer + 14, &ZCRC32, 4 );
    std::memcpy( buffer + 18, &compressedSize, 4 );
    std::memcpy( buffer + 22, &uncompressedSize, 4 );
    std::memcpy( buffer + 26, &filenameLength, 2 );
    std::memcpy( buffer + 28, &extraLength, 2 );
    std::memcpy( buffer + 30, filename.c_str(), filenameLength );
    
    if ( extraLength > 0 )
    {
      char extraBuffer[extraLength];
      extra->concatenateFields( extraBuffer );
      std::memcpy( buffer + 30 + filenameLength, extraBuffer, extraLength );
    }

    // todo: error handling 
    write( archiveFd, buffer, lfhSize );
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
  ZipExtra *extra;
  uint32_t lfhSize;
  
  static const uint16_t lfhBaseSize = 30;
  static const uint32_t lfhSign = 0x04034b50;
};

// central directory file header
struct CDFH
{
  CDFH( LFH *lfh, mode_t mode, uint64_t lfhOffset )
  {
    zipVersion = ( 3 << 8 ) | 63;
    generalBitFlag = lfh->generalBitFlag;
    compressionMethod = lfh->compressionMethod;
    lastModFileTime = lfh->lastModFileTime;
    lastModFileDate = lfh->lastModFileDate;
    ZCRC32 = lfh->ZCRC32;
    compressedSize = lfh->compressedSize;
    uncompressedSize = lfh->uncompressedSize;
    filenameLength = lfh->filenameLength;
    commentLength = 0;
    nbDisk = 0;
    internAttr = 0;
    externAttr = mode << 16;
    if ( lfhOffset > ovrflw32 ) 
      offset = ovrflw32;
    else
      offset = lfhOffset;     
    extra = new ZipExtra( lfh->extra, lfhOffset );
    extraLength = extra->totalSize;
    if ( extraLength == 0 )
      minZipVersion = 10;
    else
      minZipVersion = 45;
    filename = lfh->filename;
    comment = "";
    cdfhSize = cdfhBaseSize + filenameLength + extraLength + commentLength;
  }

  void Write( int archiveFd )
  {
    char buffer[cdfhSize];
    std::memcpy( buffer, &cdfhSign, 4 );
    std::memcpy( buffer + 4, &zipVersion, 2 );
    std::memcpy( buffer + 6, &minZipVersion, 2 );
    std::memcpy( buffer + 8, &generalBitFlag, 2 );
    std::memcpy( buffer + 10, &compressionMethod, 2 );
    std::memcpy( buffer + 12, &lastModFileTime, 2 );
    std::memcpy( buffer + 14, &lastModFileDate, 2 );
    std::memcpy( buffer + 16, &ZCRC32, 4 );
    std::memcpy( buffer + 20, &compressedSize, 4 );
    std::memcpy( buffer + 24, &uncompressedSize, 4 );
    std::memcpy( buffer + 28, &filenameLength, 2 );
    std::memcpy( buffer + 30, &extraLength, 2 );
    std::memcpy( buffer + 32, &commentLength, 2 );
    std::memcpy( buffer + 34, &nbDisk, 2 );
    std::memcpy( buffer + 36, &internAttr, 2 );
    std::memcpy( buffer + 38, &externAttr, 4 );
    std::memcpy( buffer + 42, &offset, 4 );
    std::memcpy( buffer + 46, filename.c_str(), filenameLength );
    
    if ( extraLength > 0 )
    {
      char extraBuffer[extraLength];
      extra->concatenateFields( extraBuffer );
      std::memcpy( buffer + 46 + filenameLength, extraBuffer, extraLength );
    }

    if ( commentLength > 0 )
      std::memcpy( buffer + 46 + filenameLength + extraLength, comment.c_str(), commentLength );

    // todo: error handling 
    write( archiveFd, buffer, cdfhSize );
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
  uint16_t nbDisk;
  uint16_t internAttr;
  uint32_t externAttr;
  uint32_t offset;
  std::string filename;
  ZipExtra *extra;
  std::string comment;
  uint32_t cdfhSize;
 
  static const uint16_t cdfhBaseSize = 46;
  static const uint32_t cdfhSign = 0x02014b50;
};

// end of central directory record
struct EOCD
{
  // constructor used when appending to existing ZIP archive
  EOCD( const char *buffer )
  {
    nbDisk        = *reinterpret_cast<const uint16_t*>( buffer + 4 );
    nbDiskCd      = *reinterpret_cast<const uint16_t*>( buffer + 6 );
    nbCdRecD      = *reinterpret_cast<const uint16_t*>( buffer + 8 );
    nbCdRec       = *reinterpret_cast<const uint16_t*>( buffer + 10 );
    cdSize        = *reinterpret_cast<const uint32_t*>( buffer + 12 );
    cdOffset      = *reinterpret_cast<const uint32_t*>( buffer + 16 );
    commentLength = *reinterpret_cast<const uint16_t*>( buffer + 20 );
    comment       = std::string( buffer + 22, commentLength );

    eocdSize = eocdBaseSize + commentLength;
    // todo: change this for ZIP64
    useZip64= false;
  }

  // constructor used when creating new ZIP archive
  EOCD(LFH *lfh, CDFH *cdfh )
  {
    useZip64 = false;
    nbDisk = 0;
    nbDiskCd = 0;
    nbCdRecD = 1;
    nbCdRec = 1;
    cdSize = cdfh->cdfhSize;
    if ( lfh->compressedSize == ovrflw32 || lfh->lfhSize + lfh->compressedSize > ovrflw32 )
    {
      cdOffset = ovrflw32;
      useZip64 = true;
    }
    else
      cdOffset = lfh->lfhSize + lfh->compressedSize;
    commentLength = 0;
    comment = "";
    eocdSize = eocdBaseSize + commentLength;
  }

  void Write( int archiveFd )
  {
    char buffer[eocdSize];
    std::memcpy( buffer, &eocdSign, 4 ); 
    std::memcpy( buffer + 4, &nbDisk, 2 );
    std::memcpy( buffer + 6, &nbDiskCd, 2 ); 
    std::memcpy( buffer + 8, &nbCdRecD, 2 ); 
    std::memcpy( buffer + 10, &nbCdRec, 2 ); 
    std::memcpy( buffer + 12, &cdSize, 4 ); 
    std::memcpy( buffer + 16, &cdOffset, 4 ); 
    std::memcpy( buffer + 20, &commentLength, 2 ); 
    
    if ( commentLength > 0 )
      std::memcpy( buffer + 22, comment.c_str(), commentLength ); 

    // todo: error handling
    write( archiveFd, buffer, eocdSize );
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
  bool useZip64;

  static const uint16_t eocdBaseSize = 22;
  static const uint32_t eocdSign = 0x06054b50;
  static const uint16_t maxCommentLength = 65535;
};

// ZIP64 end of central directory record
struct ZIP64_EOCD
{
  ZIP64_EOCD( EOCD *eocd, LFH *lfh )
  {
    zipVersion = ( 3 << 8 ) | 63;
    minZipVersion = 45;
    nbDisk = eocd->nbDisk;
    nbDiskCd = eocd->nbDiskCd;
    // todo: change for when appending a file
    nbCdRecD = eocd->nbCdRecD;
    nbCdRec = eocd->nbCdRec;
    cdSize = eocd->cdSize;
    if ( eocd->cdOffset == ovrflw32 )
    {
      if ( lfh->compressedSize == ovrflw32 )
        cdOffset = lfh->lfhSize + lfh->extra->compressedSize;
      else
        cdOffset = lfh->lfhSize + lfh->compressedSize;
    }
    else
      cdOffset = eocd->cdOffset;
    extensibleData = "";
    extensibleDataLength = 0;
    zip64EocdSize = zip64EocdBaseSize + extensibleDataLength - 12;
    zip64EocdTotalSize = zip64EocdBaseSize + extensibleDataLength;
  }

  void Write( int archiveFd )
  {
    char buffer[zip64EocdTotalSize];
    std::memcpy( buffer, &zip64EocdSign, 4 );
    std::memcpy( buffer + 4, &zip64EocdSize, 8 );
    std::memcpy( buffer + 12, &zipVersion, 2 );
    std::memcpy( buffer + 14, &minZipVersion, 2 );
    std::memcpy( buffer + 16, &nbDisk, 4 );
    std::memcpy( buffer + 20, &nbDiskCd, 4 );
    std::memcpy( buffer + 24, &nbCdRecD, 8 );
    std::memcpy( buffer + 32, &nbCdRec, 8 );
    std::memcpy( buffer + 40, &cdSize, 8 );
    std::memcpy( buffer + 48, &cdOffset, 8 );

    if ( extensibleDataLength > 0 )
      std::memcpy( buffer + 56, extensibleData.c_str(), extensibleDataLength );

    // todo: error handling 
    write( archiveFd, buffer, zip64EocdTotalSize );
  }

  uint64_t zip64EocdSize;
  uint16_t zipVersion;
  uint16_t minZipVersion;
  uint32_t nbDisk;
  uint32_t nbDiskCd;
  uint64_t nbCdRecD;
  uint64_t nbCdRec;
  uint64_t cdSize;
  uint64_t cdOffset;
  std::string extensibleData;
  uint16_t extensibleDataLength;
  uint64_t zip64EocdTotalSize;

  static const uint16_t zip64EocdBaseSize = 56;
  static const uint32_t zip64EocdSign = 0x06064b50;
};

// ZIP64 end of central directory locator
struct ZIP64_EOCDL
{
  ZIP64_EOCDL( EOCD *eocd, ZIP64_EOCD *zip64Eocd )
  {
    nbDiskZip64Eocd = 0;
    totalNbDisks = 1;

    if ( eocd->cdOffset == ovrflw32 )
      zip64EocdOffset = zip64Eocd->cdOffset;
    else 
      zip64EocdOffset = eocd->cdOffset;
    
    if ( eocd->cdSize == ovrflw32 )
      zip64EocdOffset += zip64Eocd->cdSize;
    else
      zip64EocdOffset += eocd->cdSize;
  }

  void Write( int archiveFd )
  {
    char buffer[zip64EocdlSize];
    std::memcpy( buffer, &zip64EocdlSign, 4 );
    std::memcpy( buffer + 4, &nbDiskZip64Eocd, 4 );
    std::memcpy( buffer + 8, &zip64EocdOffset, 8 );
    std::memcpy( buffer + 16, &totalNbDisks, 4 );

    // todo: error handling 
    write( archiveFd, buffer, zip64EocdlSize );
  }
  
  uint32_t nbDiskZip64Eocd;
  uint64_t zip64EocdOffset;
  uint32_t totalNbDisks;

  static const uint16_t zip64EocdlSize = 20;
  static const uint32_t zip64EocdlSign = 0x07064b50;
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
    
    void Open()
    {
      // open archive file for reading and writing and with file permissions 644
      archiveFd = open( archiveFilename.c_str(), O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );

      if ( archiveFd == -1 )
      {
        if ( errno == ENOENT ) 
        {
          // file doesn't exist, so must be creating ZIP archive from scratch
          // todo: error handling
          archiveFd = open( archiveFilename.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
          std::cout << "Creating new zip archive...\n";
        }
        else
        {
          // todo: proper error handling
          std::cout << "Could not open " << archiveFilename << "\n";  
        }
      }
      else
      {
        // file exists, so we must be appending to existing ZIP archive
        std::cout << "Appending to existing zip archive...\n";   
        // read and store existing EOCD and central directory records
        struct stat zipInfo;
        if ( fstat( archiveFd, &zipInfo ) == -1 )
        {
          // todo: proper error handling
          std::cout << "Could not stat " << archiveFilename << "\n";
        }
        uint64_t size = EOCD::maxCommentLength + EOCD::eocdBaseSize;
        if ( size > zipInfo.st_size )
          size = zipInfo.st_size;
        char eocdBuffer[size];
        // todo: error handling
        lseek( archiveFd, -size, SEEK_END );
        read( archiveFd, eocdBuffer, size );
        // find EOCD in ZIP archive
        char *eocdBlock = LookForEocd( size, eocdBuffer );
        // todo: proper error handling 
        if ( !eocdBlock )
          std::cout << "Could not find the EOCD signature.\n";
        eocd = new EOCD( eocdBlock ) ;

        // read and store existing central directory
        // todo: adapt for ZIP64
        uint64_t existingCdSize = eocd->cdSize;
        uint64_t offset = eocd->cdOffset;
        char buffer[existingCdSize];
        // todo: error handling
        lseek( archiveFd, offset, SEEK_SET );
        read( archiveFd, buffer, existingCdSize );
        existingCd = std::string( buffer, existingCdSize );
      }
    }
    
    void Append()
    {    
      // open input file for reading
      inputFd = open( inputFilename.c_str(), O_RDONLY );
      if ( inputFd == -1 )
      {
        // todo: proper error handling
        std::cout << "Could not open " << inputFilename << "\n";  
      }

      struct stat fileInfo;
      if ( fstat( inputFd, &fileInfo ) == -1 )
      {
        // todo: proper error handling
        std::cout << "Could not stat " << inputFilename << "\n";
      }
      
      LFH *lfh = new LFH( inputFilename, crc, fileInfo.st_size, fileInfo.st_mtime );

      // todo: ZIP64 argument may need to be offset from ZIP64 EOCD
      if ( eocd )
      {
        // must be appending to existing archive
        cdfh = new CDFH( lfh, fileInfo.st_mode, eocd->cdOffset );
        // udpate the EOCD
        eocd->nbCdRecD += 1;
        eocd->nbCdRec += 1;
        // todo: deal with this for ZIP64
        eocd->cdSize += cdfh->cdfhSize;
        eocd->cdOffset += lfh->lfhSize + lfh->compressedSize;
      }
      else
      {
        // must be creating new archive
        cdfh = new CDFH( lfh, fileInfo.st_mode, 0 );
        eocd = new EOCD( lfh, cdfh );
      }

      if ( eocd->useZip64 )
      {
        zip64Eocd = new ZIP64_EOCD( eocd, lfh );
        zip64Eocdl = new ZIP64_EOCDL( eocd, zip64Eocd );
      }

      // write local file header to the archive
      // todo: error handling
      lseek( archiveFd, cdfh->offset, SEEK_SET );
      lfh->Write( archiveFd );
    }

    // taken from ZipArchiveReader.cc and modified
    char* LookForEocd( uint64_t size, char *buffer )
    {
      for( ssize_t offset = size - EOCD::eocdBaseSize; offset >= 0; --offset )
      {
        uint32_t *signature = reinterpret_cast<uint32_t*>( buffer + offset );
        if( *signature == EOCD::eocdSign ) 
          return buffer + offset;
      }
      return 0;
    }

    void Finalize()
    {
      // write central directory records to archive
      if ( existingCd.length() > 0 )
        WriteExistingCd();
      cdfh->Write( archiveFd );
      // write EOCD to archive
      if ( eocd->useZip64 )
      {
        zip64Eocd->Write( archiveFd );
        zip64Eocdl->Write( archiveFd );
      }
      eocd->Write( archiveFd );
    }

    void WriteExistingCd()
    {
      // todo: error handling
      write( archiveFd, existingCd.c_str(), existingCd.length() );
    }
    
    // for testing purposes - not in final API
    void WriteFileData()
    {
      std::cout << "Writing file data...\n";
      int bytes_read;
      int size = 10240;
      char buffer[size];
      uint64_t total_bytes = 0;
      do
      {
        // todo: error handling for read and write
        bytes_read = read( inputFd, buffer, size );
        write( archiveFd, buffer, bytes_read );
        total_bytes += bytes_read;
      } 
      while( bytes_read != 0 );
      std::cout << "Finished writing file data.\n"; 
      std::cout << "File data written: " << std::hex << total_bytes << "\n";

      // todo: error handling
      close( inputFd );
    }

    void Close()
    {
      // close the archive
      // todo: error handling
      close ( archiveFd );
    }

  private:
    int inputFd;
    int archiveFd;
    std::string inputFilename;
    std::string archiveFilename;
    CDFH *cdfh;
    EOCD *eocd;
    ZIP64_EOCD *zip64Eocd;
    ZIP64_EOCDL *zip64Eocdl;
    uint32_t crc;
    std::string existingCd;

};

// run as ./ZipArchive <input filename> <output filename>
int main( int argc, char **argv )
{
  std::string inputFilename = "file.txt";
  std::string archiveFilename = "archive.zip"; 
  // uncomment crc when zipping file.txt
  uint32_t crc = 0x797b4b0e;
  // uncomment crc when zipping 4GB.dat
  //uint32_t crc = 0x756db3ac;
  if (argc >= 3)
  {
    inputFilename = argv[1];
    archiveFilename = argv[2];
  }
  else 
    std::cout << "No args given, using defaults.\n";

  std::cout << "Input file: " << inputFilename << "\n";
  std::cout << "Output file: " << archiveFilename << "\n";
  std::cout << "crc: " << std::hex << crc << "\n"; 

  ZipArchive *archive = new ZipArchive( inputFilename, archiveFilename, crc );
  archive->Open();
  archive->Append();
  archive->WriteFileData();
  archive->Finalize();
  archive->Close();
}

 
