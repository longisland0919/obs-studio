set DEPS=dependencies2019.0
set CMakeGenerator=Visual Studio 16 2019

set BuildConfig=Debug

set ObsDepthPath="%CD%\..\obs-build-dependencies\dependencies2019\win64"
set ObsQtPath="%CD%\..\obs-build-dependencies\qt\5.15.2\msvc2019_64"


cmake -H. ^
         -B%CD%\build ^
         -G"%CmakeGenerator%" ^
         -A x64 ^
         -DCMAKE_SYSTEM_VERSION=10.0 ^
         -DDepsPath=%ObsDepthPath% ^
         -DQTDIR=%ObsQtPath% ^
         -DBUILD_BROWSER=false

cmake --build %CD%\build
