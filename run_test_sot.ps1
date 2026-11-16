$qtBin = "D:\Qt\6.12.0\mingw_64\bin"
$mingwBin = "D:\Qt\Tools\mingw1310_64\bin"
$releaseBin = "D:\PyProjects\RGB_PTZ_Integrated\build\release"
$ocvBin = "D:\PyProjects\RGB_PTZ_Integrated\opencv\install\x64\mingw\bin"
$ortBin = "D:\PyProjects\RGB_PTZ_Integrated\3rdparty\onnxruntime\bin"

$env:PATH = "$qtBin;$mingwBin;$releaseBin;$ocvBin;$ortBin;$env:PATH"

& "$releaseBin\test_sot_tracker.exe" @args

