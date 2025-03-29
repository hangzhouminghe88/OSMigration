

@echo off
setlocal ENABLEEXTENSIONS

:: 参数设置
set TARGET_IP=10.10.10.24
set PORT=12345
set VOLUME=C:

:: 尝试使用 X, Y, Z 中未被占用的盘符
set SNAP_DRIVE=
for %%L in (Z Y X) do (
    fsutil fsinfo drivetype %%L: >nul 2>nul
    if errorlevel 1 (
        set SNAP_DRIVE=%%L:
        goto :found
    )
)

echo [!] 错误：找不到可用的快照挂载盘符（X: Y: Z: 都被占用了）。
pause
exit /b 1

:found
echo [*] 选定挂载盘符：%SNAP_DRIVE%

:: 检查 diskshadow 是否存在
where diskshadow >nul 2>nul
if errorlevel 1 (
    echo [!] 错误：找不到 diskshadow 工具。
    pause
    exit /b 1
)

:: 生成临时 diskshadow 脚本
set TMP_SCRIPT=%TEMP%\netdd_shadow_%RANDOM%.dsh
(
echo SET CONTEXT PERSISTENT NOWRITERS
echo BEGIN BACKUP
echo ADD VOLUME %VOLUME% ALIAS shadowVol
echo CREATE
echo EXPOSE %%shadowVol%% %SNAP_DRIVE%
echo END BACKUP
) > "%TMP_SCRIPT%"

echo [*] 正在使用 VSS 创建快照并挂载到 %SNAP_DRIVE% ...
diskshadow /s "%TMP_SCRIPT%" >nul

:: 去掉冒号，转换为设备路径 \\.\X
set SNAP_LETTER=%SNAP_DRIVE::=%

if exist %SNAP_DRIVE%\ (
    echo [✓] 快照成功挂载为 %SNAP_DRIVE%
    echo [*] 启动 netdd_send.exe 传输快照内容...

    netdd_send.exe %TARGET_IP% %PORT% \\.\%SNAP_LETTER%

    echo [*] 正在清理快照...
    diskshadow /s "%TMP_SCRIPT%" >nul
) else (
    echo [!] 错误：快照挂载失败（可能是权限不足、卷错误或 VSS 不支持）。
)

:: 清理临时文件
del "%TMP_SCRIPT%" >nul
endlocal
pause
