# android_properties
## summary
This tool is get/set system properties.Support all android versions in theory.
I just tested on android M, N and O.

Set operation need root first, and can set all the properties(include prefixed with 'ro').
## usage
    usage: system_properties [-h] [-a] [-l log_level] prop_name prop_value
          -h:                  display this help message
          -a:                  dump all system properties
          -l log_level:        console = 1 logcat = 2  consle + logcat = 3(default)
          -s                   print security context(selabel)
    
    example: system_properties ro.debuggable 1
## compile
In the root directory, use ndk-build.bat(Windows) or ndk-build.sh(Linux)


## timeline

    2017.10.15  first commit
    2017.12.14  support for android O
    2018.08.03  1. support for android P
                2. add security context(selabel) to print

