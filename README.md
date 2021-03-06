# ZipArchive

This code was developed for ZIP support in the XRootD Client, as part of a CERN summer project. 

The XRootD Client source code can be found here: https://github.com/xrootd/xrootd/tree/master/src/XrdCl

The ZIP64 file specification can be found here: https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT

*ZipArchive.cc* is the important file, the other files were mainly for my use during development and testing. It provides an API which allows you to append local files to an existing remote ZIP archive (N.B. an XRootD Server must be running), or to create a new remote ZIP archive from local files.

*experiments/LocalZipArchive.cc* contains a version of the code which works separately from XRootD, so can be used to append local files to a local ZIP archive.

Please read the *Project Report* for more details of my project, and check out the *Project Presentation* which I presented to the rest of the IT-ST-PDS section in the final team meeting I attended.

## Assumptions

The following assumptions were made when developing the ZipArchive class.

- No encryption 
- No compression 
- No digital signatures 
- No data descriptors 
- No file comments 
- No ZIP file comments 
- No content in ZIP64 EOCD extensible data sector 
- Disk number is always 0, total number of disks is always 1 
- File permissions: 644 
- Set last mod file time to local time 
- Correct CRC value will be provided by the user of the API 
- Version made by: UNIX, v6.3 of the ZIP specification 
- Version needed to extract: v1.0, or for large files needing ZIP64 format, v4.5 
- EOCD no. of records must be updated as well even if we are using a ZIP64 EOCD
- EOCD cdSize and cdOffset: if one overflows and is set to -1, must set BOTH to -1 and store in ZIP64 EOCD
- Don't need to use (eg write out to zip archive) the nbDisk field in the extra field, since it is always 0
- Use default timeout of 0 on the open/stat/read/write/close calls
