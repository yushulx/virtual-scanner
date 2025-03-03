# Windows Virtual Scanner
The project is ported from [twain-samples](https://github.com/twain/twain-samples).

## Pre-requisites
- Visual Studio 2017 or above
- Qt 5.12.11 msvc2017

## Environment Setup
- Add system variable `QTDIR`: msvc2017 (x86) or msvc2017_64 (x64)
    ![Qt system variable](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/qt-system-variable.jpg) 
- If you have multiple Qt Kit directories in system path, move the target kit directory ahead of others. Otherwise, when running `windeployqt.exe` to deploy a Qt project, it will fail to copy the correct dll files.
    ![Qt kit path](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/qt-kit-path-windeployqt.jpg)

## How to Build
1. Start Visual Studio as administrator.
2. Open `TWAIN-Samples\Twain_DS_sample01\visual_studio\TWAINDS_VS2017.sln`.
3. Build the project. The relevant files will be copied to `C:\Windows\twain_32\sample2\`.

## How to Test
- Open the virtual scanner in [Dynamic Web TWAIN online demo](https://demo.dynamsoft.com/web-twain/).
    
    ![Dynamic Web TWAIN online demo](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/dynamic-webtwain-online-demo.jpg)
- Open the scanner in [twacker](https://github.com/twain/twain-toolkit/tree/master/releases).
    
    ![twacker](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/twacker.jpg)


## How to Debug
1. Run twacker.
2. In Visual Studio, set a breakpoint in `DS_Entry()` and then go to `Debug > Attach to Process` to find the twacker process.
    
    ![attach TWAIN DS to twacker](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/debug-twain-ds.jpg)

3. Click the `Attach` button and then select the virtual scanner to trigger debugging. 
    
    ![Debug TWAIN virtual scanner](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/visual-studio-debug-twain-ds.jpg)


## How to Load Custom Image Set
1. Create a `source.json` file which contains the folder of custom image set. For example:
    
    ```json
    {
        "folder": "C:/Users/admin/Pictures/barcode"
    }
    ```
    
    Copy the JSON file to `C:\Windows\twain_32\sample2`.

2. In the folder of custom image set, create a `info.json` file which contains the index of current selected file and max image count for ADF:
    
    ```json
    {
        "index": 0,
        "maxcount": 10
    }
    ```

3. Use twacker to scan the custom images one by one. The index will be auto-updated if your folder have **Write permission**.

    ![Windows virtual scanner with custom image set](https://www.dynamsoft.com/blog/wp-content/uploads/2021/11/windows-virtual-scanner-custom-images.jpg)

## Blog
[How to Build Windows Virtual Scanner and Feed Custom Images](https://www.dynamsoft.com/codepool/windows-virtual-scanner-custom-images.html)
