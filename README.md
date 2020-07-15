# ZipArchive

**Assumptions made:**

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
- The EOCD no. of records will never overflow (max is 65535)
