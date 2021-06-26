# android_properties
## summary
This tool is get/set system properties.Support all android versions in theory.
I just tested on android M, N, O, P, Q and R.

Set operation need root first, and can set all the properties(include prefixed with 'ro').
## usage
    usage: system_properties [-h] [-a] [-l log_level] [-s] [-f] prop_name prop_value
      -h:                  display this help message
      -a:                  dump all system properties
      -l log_level:        console = 1 logcat = 2  consle + logcat = 3(default)
      -s                   print security context(selabel)
      -f                   read property_contexts files to get security context
    
    example: system_properties ro.debuggable 1
## compile
In the root directory, use ndk-build.bat(Windows) or ndk-build.sh(Linux)


## timeline

    2017.10.15  first commit
    2017.12.14  support for android O
    2018.08.03  1. support for android P
                2. add security context(selabel) to print
    2019.09.25  support for android Q
    2020.07.13  1. support for android R
                2. support long property name and property value
    2021.06.26  use file(/dev/__properties__/property_info) to get security contexts

