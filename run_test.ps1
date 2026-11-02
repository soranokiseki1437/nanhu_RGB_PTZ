$qtBin = "D:\Qt\6.12.0\mingw_64\bin"
$mingwBin = "D:\Qt\Tools\mingw1310_64\bin"
$releaseBin = "D:\PyProjects\RGB_PTZ_Integrated\build\release"

$env:PATH = "$qtBin;$mingwBin;$releaseBin;$env:PATH"

$model = if ($args.Count -gt 0) { $args[0] } else { "models/yolov8n.onnx" }

& "$releaseBin\test_detector.exe" $model
