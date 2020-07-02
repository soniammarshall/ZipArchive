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
