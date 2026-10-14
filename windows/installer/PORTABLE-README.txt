Virtual Scanner - Windows TWAIN Data Source
==========================================

A virtual (software only) TWAIN scanner. It shows up in TWAIN applications
(twacker, Dynamic Web TWAIN, ...) like a real scanner, but instead of reading
from hardware it returns the picture files of a folder - one picture per scan,
cycling through the whole folder.

What was installed
------------------
C:\Windows\twain_64\sample2            the 64 bit scanner (TWAINDS_Sample64.ds,
                                       FreeImage.dll, the Qt runtime)
C:\Windows\twain_64\sample2\images     the 11 sample pictures
C:\Windows\twain_64\sample2\README.txt this file
C:\Windows\twain_32\sample2            the same for 32 bit applications, if it
                                       was selected during the installation

No configuration is needed: the scanner looks for pictures in the "images"
folder next to it (TWAINDS_Sample64.ds) and returns a different one on every
scan. A flatbed scan returns one picture, a scan from the document feeder
(ADF) returns one page per picture - so checking "Use ADF" in the online demo
gives you all 11 sample pictures as one multi page document.

Which picture is next is remembered per user in
C:\Users\<you>\AppData\Local\VirtualScanner\info.json, so the sample folder
itself stays untouched.

Portable use
------------
Instead of the installer you can copy this whole folder (including "images")
into C:\Windows\twain_64\sample2 - nothing else to configure.

Using your own pictures
-----------------------
1. Put your pictures (JPG or PNG) into the images folder:

     C:\Windows\twain_64\sample2\images

   Delete the sample pictures you do not want. They are sorted in natural
   order (2.png comes before 10.png).

2. Restart the TWAIN application (the scanner reads the folder when a scan
   starts).

Optional: controlling the order and the number of pages
-------------------------------------------------------
Create an info.json in the picture folder:

  C:\Windows\twain_64\sample2\images\info.json

     {"index": 0, "maxcount": 4}

  "index"     picture of the next scan (0 = the first file of the folder)
  "maxcount"  number of pages one ADF scan job returns

A folder that contains info.json is updated after every scan, which needs
write permission for that folder. Without info.json the position is kept in
C:\Users\<you>\AppData\Local\VirtualScanner\info.json instead.

Optional: using a folder somewhere else
---------------------------------------
Create a source.json next to the data source

  C:\Windows\twain_64\sample2\source.json

     {"folder": "C:/Users/<you>/Pictures/my-scans"}

to use pictures from that folder instead of the "images" folder. Delete
source.json to go back to the pictures next to the data source.

Requirements
------------
A TWAIN Data Source Manager (twaindsm.dll or twain_32.dll) must be present on
the machine - every scanner driver or Dynamic Web TWAIN installs one. Without
it TWAIN applications cannot enumerate data sources at all. See
https://twain.org/dsm/

More information
----------------
https://github.com/yushulx/virtual-scanner
