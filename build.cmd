setlocal
call %~dp0..\vc_setup.cmd
set BUILD=build
title Configuring PCRE
%CMAKE% -S . -B %BUILD%
title Building PCRE Debug
msbuild /m %BUILD%\PCRE2.sln -p:Configuration=Debug
title Building PCRE Release
msbuild /m %BUILD%\PCRE2.sln -p:Configuration=Release
title Done building PCRE
endlocal
