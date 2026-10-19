VoodooHDA
========

Compilation.
If you are using systems up to Catalina then open project 
tranc/VoodooHDA.xcodeproj

For systems BigSur and up you have to download the project VoodooHDA

You also need to download MacKernelSDK 
git clone https://github.com/joevt/MacKernelSDK.git
(this is tested version)
and make a symlink into project
cd /path_to/VoodooHDA/tranc/
ln -s /path_to/MacKernelSDK MacKernelSDK

open project
tranc/VoodooHDA-BS.xcodeproj
and compile
