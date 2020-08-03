#include "XrdCl/XrdClFile.hh"
#include "XrdCl/XrdClFileSystem.hh"
#include "XrdCl/XrdClURL.hh"
#include "XrdCl/XrdClMessageUtils.hh"

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
#include <vector>
#include <memory>

namespace XrdCl 
{
  
  const uint16_t ovrflw16 = 0xffff;
  const uint32_t ovrflw32 = 0xffffffff;
  const uint64_t ovrflw64 = 0xffffffffffffffff;

  // ZIP64 extended information extra field
  struct ZipExtra
  {
    ZipExtra( uint64_t fileSize )
    {
      offset = 0;
      nbDisk = 0;
      if ( fileSize >= ovrflw32 )
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
      if ( offset >= ovrflw32 )
      {
        this->offset = offset;
        dataSize += 8;
        totalSize = dataSize + 4;
      }
      else
        this->offset = 0;
    }

    void Write( File &archive, uint64_t writeOffset )
    {
      if ( totalSize > 0 )
      {
        std::unique_ptr<char[]> buffer { new char[totalSize] };
        std::memcpy( buffer.get(), &headerID, 2 );
        std::memcpy( buffer.get() + 2, &dataSize, 2 );
        if ( uncompressedSize > 0)
        {
          std::memcpy( buffer.get() + 4, &uncompressedSize, 8 );
          std::memcpy( buffer.get() + 12, &compressedSize, 8 );
          if ( offset > 0 )
            std::memcpy( buffer.get() + 20, &offset, 8 );
        }
        else if ( offset > 0 )
          std::memcpy( buffer.get() + 4, &offset, 8 );
        
        XRootDStatus st =	archive.Write( writeOffset, totalSize, buffer.get() );
        if( !st.IsOK() ) throw "Write ZIP extra Failed."; // todo error handling
      }
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
      if ( fileSize >= ovrflw32 ) 
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
      
      ToMsdosDateTime( &time );

      lfhSize = lfhBaseSize + filenameLength + extraLength;
    }
    
    void ToMsdosDateTime( time_t *originalTime )
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

    void Write( File &archive, uint64_t writeOffset )
    {
      uint16_t size = lfhSize - extraLength;
      std::unique_ptr<char[]> buffer { new char[size] };
      std::memcpy( buffer.get(), &lfhSign, 4 );
      std::memcpy( buffer.get() + 4, &minZipVersion, 2 );
      std::memcpy( buffer.get() + 6, &generalBitFlag, 2 );
      std::memcpy( buffer.get() + 8, &compressionMethod, 2 );
      std::memcpy( buffer.get() + 10, &lastModFileTime, 2 );
      std::memcpy( buffer.get() + 12, &lastModFileDate, 2 );
      std::memcpy( buffer.get() + 14, &ZCRC32, 4 );
      std::memcpy( buffer.get() + 18, &compressedSize, 4 );
      std::memcpy( buffer.get() + 22, &uncompressedSize, 4 );
      std::memcpy( buffer.get() + 26, &filenameLength, 2 );
      std::memcpy( buffer.get() + 28, &extraLength, 2 );
      std::memcpy( buffer.get() + 30, filename.c_str(), filenameLength );
      
      XRootDStatus st =	archive.Write( writeOffset, size, buffer.get() );
      if( !st.IsOK() ) throw "Write LFH Failed."; // todo error handling

      writeOffset += size;
      
      if ( extraLength > 0 )
        extra->Write( archive, writeOffset );
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
    uint16_t lfhSize;
    
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
      if ( lfhOffset >= ovrflw32 ) 
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

    void Write( File &archive, uint64_t writeOffset )
    {
      uint16_t size = cdfhSize - extraLength - commentLength;
      std::unique_ptr<char[]> buffer { new char[size] };
      std::memcpy( buffer.get(), &cdfhSign, 4 );
      std::memcpy( buffer.get() + 4, &zipVersion, 2 );
      std::memcpy( buffer.get() + 6, &minZipVersion, 2 );
      std::memcpy( buffer.get() + 8, &generalBitFlag, 2 );
      std::memcpy( buffer.get() + 10, &compressionMethod, 2 );
      std::memcpy( buffer.get() + 12, &lastModFileTime, 2 );
      std::memcpy( buffer.get() + 14, &lastModFileDate, 2 );
      std::memcpy( buffer.get() + 16, &ZCRC32, 4 );
      std::memcpy( buffer.get() + 20, &compressedSize, 4 );
      std::memcpy( buffer.get() + 24, &uncompressedSize, 4 );
      std::memcpy( buffer.get() + 28, &filenameLength, 2 );
      std::memcpy( buffer.get() + 30, &extraLength, 2 );
      std::memcpy( buffer.get() + 32, &commentLength, 2 );
      std::memcpy( buffer.get() + 34, &nbDisk, 2 );
      std::memcpy( buffer.get() + 36, &internAttr, 2 );
      std::memcpy( buffer.get() + 38, &externAttr, 4 );
      std::memcpy( buffer.get() + 42, &offset, 4 );
      std::memcpy( buffer.get() + 46, filename.c_str(), filenameLength );

      XRootDStatus st =	archive.Write( writeOffset, size, buffer.get() );
      if( !st.IsOK() ) throw "Write CDFH Failed."; // todo error handling
      writeOffset += size;
      
      if ( extraLength > 0 )
      {
        extra->Write( archive, writeOffset );
        writeOffset += extraLength;
      }

      if ( commentLength > 0 )
      {
        st =	archive.Write( writeOffset, commentLength, comment.c_str() );
        if( !st.IsOK() ) throw "Write CDFH Failed."; // todo error handling
      }
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
    uint16_t cdfhSize;

    static const uint16_t cdfhBaseSize = 46;
    static const uint32_t cdfhSign = 0x02014b50;
  };

  // end of central directory record
  struct EOCD
  {
    // constructor used when reading from existing ZIP archive
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
      if ( lfh->compressedSize == ovrflw32 || lfh->lfhSize + lfh->compressedSize >= ovrflw32 )
      {
        cdOffset = ovrflw32;
        cdSize = ovrflw32;
        useZip64 = true;
      }
      else
      {
        cdOffset = lfh->lfhSize + lfh->compressedSize;
        cdSize = cdfh->cdfhSize;
      }
      commentLength = 0;
      comment = "";
      eocdSize = eocdBaseSize + commentLength;
    }

    void Write( File &archive, uint64_t writeOffset )
    {
      std::unique_ptr<char[]> buffer { new char[eocdSize] };
      std::memcpy( buffer.get(), &eocdSign, 4 ); 
      std::memcpy( buffer.get() + 4, &nbDisk, 2 );
      std::memcpy( buffer.get() + 6, &nbDiskCd, 2 ); 
      std::memcpy( buffer.get() + 8, &nbCdRecD, 2 ); 
      std::memcpy( buffer.get() + 10, &nbCdRec, 2 ); 
      std::memcpy( buffer.get() + 12, &cdSize, 4 ); 
      std::memcpy( buffer.get() + 16, &cdOffset, 4 ); 
      std::memcpy( buffer.get() + 20, &commentLength, 2 ); 
      
      if ( commentLength > 0 )
        std::memcpy( buffer.get() + 22, comment.c_str(), commentLength ); 

      XRootDStatus st =	archive.Write( writeOffset, eocdSize, buffer.get() );
      if( !st.IsOK() ) throw "Write EOCD Failed."; // todo error handling
    }

    uint16_t nbDisk;
    uint16_t nbDiskCd;
    uint16_t nbCdRecD;
    uint16_t nbCdRec;
    uint32_t cdSize;
    uint32_t cdOffset;
    uint16_t commentLength;
    std::string comment;
    uint16_t eocdSize;
    bool useZip64;

    static const uint16_t eocdBaseSize = 22;
    static const uint32_t eocdSign = 0x06054b50;
    static const uint16_t maxCommentLength = 65535;
  };

  // ZIP64 end of central directory record
  struct ZIP64_EOCD
  {
    // constructor used when reading from existing ZIP archive
    ZIP64_EOCD( const char* buffer )
    {
      zip64EocdSize = *reinterpret_cast<const uint64_t*>( buffer + 4 );
      zipVersion    = *reinterpret_cast<const uint16_t*>( buffer + 12 );
      minZipVersion = *reinterpret_cast<const uint16_t*>( buffer + 14 );
      nbDisk        = *reinterpret_cast<const uint32_t*>( buffer + 16 );
      nbDiskCd      = *reinterpret_cast<const uint32_t*>( buffer + 20 );
      nbCdRecD      = *reinterpret_cast<const uint64_t*>( buffer + 24 );
      nbCdRec       = *reinterpret_cast<const uint64_t*>( buffer + 32 );
      cdSize        = *reinterpret_cast<const uint64_t*>( buffer + 40 );
      cdOffset      = *reinterpret_cast<const uint64_t*>( buffer + 48 );

      extensibleData = "";
      extensibleDataLength = 0;
      zip64EocdTotalSize = zip64EocdBaseSize + extensibleDataLength;
    }

    ZIP64_EOCD( EOCD *eocd, 
                LFH *lfh, 
                CDFH *cdfh, 
                uint16_t prevNbCdRecD = 0, 
                uint16_t prevNbCdRec = 0,
                uint32_t prevCdSize = 0, 
                uint32_t prevCdOffset = 0 )
    {
      zipVersion = ( 3 << 8 ) | 63;
      minZipVersion = 45;
      nbDisk = eocd->nbDisk;
      nbDiskCd = eocd->nbDiskCd;
      if ( eocd->nbCdRecD == ovrflw16 )
        nbCdRecD = prevNbCdRecD + 1;
      else
        nbCdRecD = eocd->nbCdRecD;
      if ( eocd->nbCdRec == ovrflw16 )
        nbCdRec = prevNbCdRec + 1;
      else
        nbCdRec = eocd->nbCdRec;
      if ( eocd->cdSize == ovrflw32 )
        cdSize = prevCdSize + cdfh->cdfhSize;
      else
        cdSize = eocd->cdSize;
      if ( eocd->cdOffset == ovrflw32 )
      {
        if ( lfh->compressedSize == ovrflw32 )
          cdOffset = prevCdOffset + lfh->lfhSize + lfh->extra->compressedSize;
        else
          cdOffset = prevCdOffset + lfh->lfhSize + lfh->compressedSize;
      }
      else
        cdOffset = eocd->cdOffset;
      extensibleData = "";
      extensibleDataLength = 0;
      zip64EocdSize = zip64EocdBaseSize + extensibleDataLength - 12;
      zip64EocdTotalSize = zip64EocdBaseSize + extensibleDataLength;
    }

    void Write( File &archive, uint64_t writeOffset )
    {
      std::unique_ptr<char[]> buffer { new char[zip64EocdTotalSize] };
      std::memcpy( buffer.get(), &zip64EocdSign, 4 );
      std::memcpy( buffer.get() + 4, &zip64EocdSize, 8 );
      std::memcpy( buffer.get() + 12, &zipVersion, 2 );
      std::memcpy( buffer.get() + 14, &minZipVersion, 2 );
      std::memcpy( buffer.get() + 16, &nbDisk, 4 );
      std::memcpy( buffer.get() + 20, &nbDiskCd, 4 );
      std::memcpy( buffer.get() + 24, &nbCdRecD, 8 );
      std::memcpy( buffer.get() + 32, &nbCdRec, 8 );
      std::memcpy( buffer.get() + 40, &cdSize, 8 );
      std::memcpy( buffer.get() + 48, &cdOffset, 8 );

      if ( extensibleDataLength > 0 )
        std::memcpy( buffer.get() + 56, extensibleData.c_str(), extensibleDataLength );

      XRootDStatus st =	archive.Write( writeOffset, zip64EocdTotalSize, buffer.get() );
      if( !st.IsOK() ) throw "Write ZIP64 EOCD Failed."; // todo error handling
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
    uint64_t extensibleDataLength;
    uint64_t zip64EocdTotalSize;

    static const uint16_t zip64EocdBaseSize = 56;
    static const uint32_t zip64EocdSign = 0x06064b50;
  };

  // ZIP64 end of central directory locator
  struct ZIP64_EOCDL
  {
    // constructor used when reading from existing ZIP archive
    ZIP64_EOCDL( const char *buffer )
    {
      nbDiskZip64Eocd = *reinterpret_cast<const uint32_t*>( buffer + 4 );
      zip64EocdOffset = *reinterpret_cast<const uint64_t*>( buffer + 8 );
      totalNbDisks    = *reinterpret_cast<const uint32_t*>( buffer + 16 );
    }

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

    void Write( File &archive, uint64_t writeOffset )
    {
      std::unique_ptr<char[]> buffer { new char[zip64EocdlSize] };
      std::memcpy( buffer.get(), &zip64EocdlSign, 4 );
      std::memcpy( buffer.get() + 4, &nbDiskZip64Eocd, 4 );
      std::memcpy( buffer.get() + 8, &zip64EocdOffset, 8 );
      std::memcpy( buffer.get() + 16, &totalNbDisks, 4 );

      XRootDStatus st =	archive.Write( writeOffset, zip64EocdlSize, buffer.get() );
      if( !st.IsOK() ) throw "Write ZIP64 EOCDL Failed."; // todo error handling
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

      ZipArchive( File &archive, std::string archiveUrl ) : archive( archive ), 
                                                            archiveUrl( archiveUrl ),
                                                            archiveSize( 0 ),
                                                            existingCdSize( 0 ), 
                                                            writeOffset( 0 ), 
                                                            isOpen( false ), 
                                                            createNew( false )
      { 

      }
      
      // open archive file for reading and writing and with file permissions 644
      void Open()
      {
        // stat to check if file exists already
        URL url( archiveUrl );
        FileSystem fs( url ) ;
        StatInfo *response = 0;
        XRootDStatus st = fs.Stat( url.GetPath(), response );

        if( st.IsOK() && response )
        {
          // file exists, append to existing ZIP archive
          st = archive.Open( archiveUrl, OpenFlags::Update, Access::UR | Access::UW | Access::GR | Access::OR );

          if ( st.IsOK() )
          {
            std::cout << "Appending to existing zip archive...\n";
            isOpen = true;
            archiveSize = response->GetSize();
            delete response;

            // read EOCD into buffer
            uint32_t size = EOCD::maxCommentLength + EOCD::eocdBaseSize + ZIP64_EOCDL::zip64EocdlSize;
            if ( size > archiveSize ) size = archiveSize;
            uint64_t offset = archiveSize - size;
            buffer.reset( new char[size] );          
            uint32_t bytesRead = 0;
            
            st = archive.Read( offset, size, buffer.get(), bytesRead );
            if( !st.IsOK() ) throw "Read failed."; // todo error handling
            
            // find and store existing EOCD, ZIP64EOCD, ZIP64EOCDL and central directory records
            ReadCentralDirectory( size );
          }
        }
        else
        {
          // file doesn't already exist, create ZIP archive from scratch
          st = archive.Open( archiveUrl, OpenFlags::New | OpenFlags::Update, Access::UR | Access::UW | Access::GR | Access::OR );

          if ( st.IsOK() )
          {            
            std::cout << "Creating new zip archive...\n";
            createNew = true;
            isOpen = true;
          }
          else 
          {
            // todo: proper error handling
            std::cout << st.ToStr();
            std::cout << "Could not open " << archiveUrl << "\n";  
          }
        }
      }
      
      void Append( std::string filename, uint32_t crc, off_t fileSize, time_t fileModTime, mode_t fileMode )
      {
        LFH *lfh = new LFH( filename, crc, fileSize, fileModTime );

        CDFH *cdfh;

        if ( !createNew )
        {
          // must be appending to existing archive
          if ( eocd->useZip64 )
          {
            cdfh = new CDFH( lfh, fileMode, zip64Eocd->cdOffset );
            // update EOCD
            if ( eocd->nbCdRecD + 1 >= ovrflw16 )
              eocd->nbCdRecD = ovrflw16;
            else
              eocd->nbCdRecD += 1;
            if ( eocd->nbCdRec + 1 >= ovrflw16 )
              eocd->nbCdRec = ovrflw16;
            else
              eocd->nbCdRec += 1;
            // update ZIP64 EOCD
            zip64Eocd->nbCdRecD += 1;
            zip64Eocd->nbCdRec += 1;
            zip64Eocd->cdSize += cdfh->cdfhSize;
            if ( lfh->compressedSize == ovrflw32 )
              zip64Eocd->cdOffset += lfh->lfhSize + lfh->extra->compressedSize;
            else
              zip64Eocd->cdOffset += lfh->lfhSize + lfh->compressedSize;
            // update ZIP64 EOCDL
            zip64Eocdl->zip64EocdOffset = zip64Eocd->cdOffset + zip64Eocd->cdSize;
          }
          else
          {
            cdfh = new CDFH( lfh, fileMode, eocd->cdOffset );

            // udpate EOCD
            if ( eocd->cdSize + cdfh->cdfhSize >= ovrflw32 
                  || lfh->compressedSize == ovrflw32 
                  || eocd->cdOffset + lfh->lfhSize + lfh->compressedSize >= ovrflw32
                  || eocd->nbCdRecD + 1 >= ovrflw16
                  || eocd->nbCdRec + 1 >= ovrflw16 )
            {
              // overflown
              eocd->useZip64 = true;
              uint16_t prevNbCdRecD = eocd->nbCdRecD;
              uint16_t prevNbCdRec = eocd->nbCdRec;
              uint32_t prevCdSize = eocd->cdSize;
              uint32_t prevCdOffset = eocd->cdOffset;
              if ( eocd->nbCdRecD + 1 >= ovrflw16 )
                eocd->nbCdRecD = ovrflw16;
              else 
                eocd->nbCdRecD += 1;
              if ( eocd->nbCdRec + 1)
                eocd->nbCdRec = ovrflw16;
              else
                eocd->nbCdRec += 1;
              eocd->cdSize = ovrflw32;
              eocd->cdOffset = ovrflw32;
              zip64Eocd = new ZIP64_EOCD( eocd, lfh, cdfh, prevNbCdRecD, prevNbCdRec, prevCdSize, prevCdOffset );
              zip64Eocdl = new ZIP64_EOCDL( eocd, zip64Eocd );
            }
            else
            {
              eocd->nbCdRecD += 1;
              eocd->nbCdRec += 1;
              eocd->cdSize += cdfh->cdfhSize;
              eocd->cdOffset += lfh->lfhSize + lfh->compressedSize;
            }
          }
        }
        else
        {
          // must be creating new archive
          cdfh = new CDFH( lfh, fileMode, 0 );
          eocd = new EOCD( lfh, cdfh );
          if ( eocd->useZip64 )
          {
            zip64Eocd = new ZIP64_EOCD( eocd, lfh, cdfh );
            zip64Eocdl = new ZIP64_EOCDL( eocd, zip64Eocd );
          }
          createNew = false;
        }

        cdRecords.push_back( cdfh );
        
        // write local file header to the archive
        writeOffset = ( cdfh->offset == ovrflw32 ) ? cdfh->extra->offset : cdfh->offset;
        lfh->Write( archive, writeOffset );
        writeOffset += lfh->lfhSize;
      }

      // taken from ZipArchiveReader.cc (modified variable names)
      char* LookForEocd( uint64_t size )
      {
        for( ssize_t offset = size - EOCD::eocdBaseSize; offset >= 0; --offset )
        {
          uint32_t *signature = reinterpret_cast<uint32_t*>( buffer.get() + offset );
          if( *signature == EOCD::eocdSign ) return buffer.get() + offset;
        }
        return 0;
      }

      // taken from ZipArchiveReader.cc (modified ReadCdfh())
      void ReadCentralDirectory( uint64_t bytesRead )
      {
        char *eocdBlock = LookForEocd( bytesRead );
        if( !eocdBlock )
          std::cout << "Could not find the EOCD signature.\n";
        eocd = new EOCD( eocdBlock ) ;

        // Let's see if it is ZIP64 (if yes, the EOCD will be preceded with ZIP64 EOCD locator)
        char *zip64EocdlBlock = eocdBlock - ZIP64_EOCDL::zip64EocdlSize;
        // make sure there is enough data to assume there's a ZIP64 EOCD locator
        if( zip64EocdlBlock > buffer.get() )
        {
          uint32_t *signature = reinterpret_cast<uint32_t*>( zip64EocdlBlock );
          if( *signature == ZIP64_EOCDL::zip64EocdlSign )
          {
            zip64Eocdl = new ZIP64_EOCDL( zip64EocdlBlock );
            // the offset at which we did the read
            uint64_t buffOffset = archiveSize - bytesRead;
            if( buffOffset > zip64Eocdl->zip64EocdOffset )
            {
              // we need to read more data
              uint32_t size = archiveSize - zip64Eocdl->zip64EocdOffset;
              buffer.reset( new char[size] );
              
              uint32_t bytes = 0;
              XRootDStatus st = archive.Read( zip64Eocdl->zip64EocdOffset, size, buffer.get(), bytes );
              if( !st.IsOK() ) throw "Read failed."; // todo error handling
            }

            char *zip64EocdBlock = buffer.get() + ( zip64Eocdl->zip64EocdOffset - buffOffset );
            signature = reinterpret_cast<uint32_t*>( zip64EocdBlock );
            if( *signature != ZIP64_EOCD::zip64EocdSign )
              std::cout << "Could not find the ZIP64 EOCD signature.\n";
            zip64Eocd = new ZIP64_EOCD( zip64EocdBlock );
            eocd->useZip64 = true;
          }
          /*
          else
            it is not ZIP64 so we have everything in EOCD
          */
        }

        uint64_t offset = eocd->useZip64 ? zip64Eocd->cdOffset : eocd->cdOffset;
        existingCdSize  = eocd->useZip64 ? zip64Eocd->cdSize   : eocd->cdSize;
        cdBuffer.reset( new char[existingCdSize] );
        
        uint32_t bytes = 0;
        XRootDStatus st = archive.Read( offset, existingCdSize, cdBuffer.get(), bytes );
        if( !st.IsOK() ) throw "Read failed."; // todo error handling
      }

      //taken from ZipArchiveReader.cc
      bool IsOpen() const
      {
        return isOpen;
      }

      void Finalize()
      {
        writeOffset = eocd->useZip64 ? zip64Eocd->cdOffset : eocd->cdOffset;
        // write central directory records to archive
        if ( existingCdSize > 0 )
        {
          XRootDStatus st =	archive.Write( writeOffset, existingCdSize, cdBuffer.get() );
          if( !st.IsOK() ) throw "Write existing CD Failed."; // todo error handling
          writeOffset += existingCdSize;
        }
        for ( uint16_t i=0; i<cdRecords.size(); i++)
        {
          cdRecords[i]->Write( archive, writeOffset );
          writeOffset += cdRecords[i]->cdfhSize;
        }
        // write EOCD to archive
        if ( eocd->useZip64 )
        {
          zip64Eocd->Write( archive, writeOffset );
          writeOffset += zip64Eocd->zip64EocdTotalSize;
          zip64Eocdl->Write( archive, writeOffset );
          writeOffset += ZIP64_EOCDL::zip64EocdlSize;
        }
        eocd->Write( archive, writeOffset );
      }

      void WriteFileData( char *buffer, uint32_t size, uint64_t fileOffset ) 
      {
        XRootDStatus st =	archive.Write( writeOffset + fileOffset, size, buffer );
        if( !st.IsOK() ) throw "Write file data Failed."; // todo error handling
      }

      void Close()
      {
        // todo do I need to check isOpen here
        if ( IsOpen() )
        {
          XRootDStatus st = archive.Close();
          if( st.IsOK() ) 
          {
            isOpen = false;
            buffer.reset();
          }
          else
            std::cout << "Failed to close archive.\n";
        } 
      }

    private:
      File                   &archive;
      std::string             archiveUrl;
      uint64_t                archiveSize;
      std::vector<CDFH*>      cdRecords;
      EOCD                   *eocd;
      ZIP64_EOCD             *zip64Eocd;
      ZIP64_EOCDL            *zip64Eocdl;
      std::unique_ptr<char[]> buffer;
      std::unique_ptr<char[]> cdBuffer;
      uint32_t                existingCdSize;
      uint64_t                writeOffset;
      bool                    isOpen;
      bool                    createNew;
  };
}

// for testing purposes - not in final API
int OpenInputFile( std::string inputFilename, struct stat &fileInfo )
{
  // open input file for reading then stat it
  int inputFd = open( inputFilename.c_str(), O_RDONLY );

  if ( inputFd == -1 ) 
    throw "Open input file failed.\n";
  else if ( fstat( inputFd, &fileInfo ) == -1 )
      throw "Stat input file failed.\n";

  return inputFd;
}

// run as ./ZipArchive <input filename> <output file url>
int main( int argc, char **argv )
{
  std::string inputFilename = "file.txt";
  std::string archiveUrl = "root://localhost//tmp/archive.zip"; 
  // uncomment crc when zipping file.txt
  //uint32_t crc = 0x797b4b0e;
  // uncomment crc when zipping 4GB.dat
  uint32_t crc = 0x756db3ac;
  if (argc >= 3)
  {
    inputFilename = argv[1];
    archiveUrl = argv[2];
  }
  else 
    std::cout << "No args given, using defaults.\n";

  std::cout << "Input file: " << inputFilename << "\n";
  std::cout << "Output file: " << archiveUrl << "\n";
  std::cout << "crc: " << std::hex << crc << "\n"; 

  struct stat fileInfo;
  int inputFd = OpenInputFile( inputFilename, fileInfo );

  XrdCl::File *file = new XrdCl::File();
  XrdCl::ZipArchive *archive = new XrdCl::ZipArchive( *file, archiveUrl );

  archive->Open();
  archive->Append( inputFilename, crc, fileInfo.st_size, fileInfo.st_mtime, fileInfo.st_mode );

  std::cout << "Writing file data...\n";
  uint32_t bytesRead = 0;
  uint64_t fileOffset = 0;
  uint32_t size = 10240;
  char buffer[size];

  lseek( inputFd, fileOffset, SEEK_SET );
  bytesRead = read( inputFd, buffer, size );
  if ( bytesRead == XrdCl::ovrflw32 ) std::cout << "Read failed.\n";
  while( bytesRead != 0 )
  {
    // todo: error handling
    archive->WriteFileData( buffer, bytesRead, fileOffset );
    fileOffset += bytesRead;
    lseek( inputFd, fileOffset, SEEK_SET );
    bytesRead = read( inputFd, buffer, size );
    if ( bytesRead == XrdCl::ovrflw32 ) std::cout << "Read failed.\n";
  } 
  std::cout << "Finished writing file data.\n"; 

  // todo: error handling
  close( inputFd );

  archive->Finalize();
  archive->Close();

  return 0;
}
