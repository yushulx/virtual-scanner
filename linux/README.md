# Linux Virtual Scanner
This project implements a virtual scanner device for Linux using the SANE (Scanner Access Now Easy) framework. It allows you to simulate a scanner device and feed custom images into applications that support SANE.

https://github.com/user-attachments/assets/51f2f4c3-d123-41bb-a010-fcd72097fc18

## Overview
The Linux Virtual Scanner creates a virtual scanning device that applications can interact with through the SANE API. Instead of capturing images from physical hardware, this scanner loads images from files on disk and presents them as scanned documents.

## Pre-requisites
The following packages are required to build the project:
```bash
sudo apt install build-essential autoconf libtool libsane-dev sane sane-utils imagemagick libmagickcore-dev
```

## Custom Images
Convert images to the PNM format using ImageMagick:

```bash
convert test.jpg test.pnm
```

## Usage
1. Set the custom image path in `custom_scanner.c`:
    
    ```c
    #define CUSTOM_IMAGE_PATH "/home/xiao/Desktop/dwt/test.pnm"
    ```

2. Build the source code:
    
    ```bash
    gcc -shared -fPIC -o libsane-custom_scanner.so.1 custom_scanner.c -lsane -I/usr/include/sane
    ```

3. Copy the shared library to the SANE backend directory:
    
    ```bash
    sudo cp libsane-mock_sane.so.1 /usr/lib/x86_64-linux-gnu/sane/
    ```

4. Update the SANE configuration file `/etc/sane.d/dll.conf` to include the custom scanner backend:
    
    ```bash
    echo "custom_scanner" | sudo tee -a /etc/sane.d/dll.conf
    ```

5. Test the scanner using the `scanimage` command:
    
    ```bash
    scanimage -d custom_scanner > output.pnm
    ```

6. Test the scanner using [Dynamic Web TWAIN online demo](https://demo.dynamsoft.com/web-twain/).

    ![Dynamic Web TWAIN Linux Virtual Scanner](https://www.dynamsoft.com/codepool/img/2025/02/sane-virtual-scanner.png)

## Troubleshooting
Run `SANE_DEBUG_CUSTOM_SCANNER=255 scanimage -L` for detailed debugging information

## Reference
- [test.c](https://gitlab.com/sane-project/backends/-/blob/master/backend/test.c) - SANE test backend implementation
