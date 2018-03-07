# dirsync
OS and System Programming, lab #3

Write a program that will sync two directories, e.g. Dir1 and Dir2. The user specifies names of Dir1 and Dir2. As a result of program, all files which exist in Dir1, but does not in Dir2, should be copied along with source permissions to Dir2. Copying procedures should run in separate process for each file. Each process outputs its pid, a full path to the file to copy and total number of bytes written. The number of simultaneously running processes should not exceed N, which is specified by user. 
