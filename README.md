# android_properties
## summary
This tool is get/set system properties.Support all android versions in theory. But I just tested on android M and N. 

Set operation need root first, and can set all the properties(include prefixed with 'ro').
## usage
    usage: system_properties [-h] [-a] [-l log_level] prop_name prop_value
          -h:                  display this help message
          -a:                  dump all system properties
          -l log_level:        console = 1 logcat = 2  consle + logcat = 3(default)
    
    example: system_properties ro.debuggable 1
## compile
In the root directory, use ndk-build.bat(Windows) or ndk-build.sh(Linux)



