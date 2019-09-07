rmdir /q /s build64
md build64
cd build64
rem cmake -G "Visual Studio 16 2019" -A x64 -T "v141" ..
cmake -G "Visual Studio 16 2019" -A x64 ..

