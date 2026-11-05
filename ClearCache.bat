@echo off
chcp 65001 > nul
rem 빌드 결과와 실행 시 자동 생성되는 캐시를 지운다. 다음 빌드·실행 때 다시 만들어진다.
rem 남기는 것: Intermediate\ProjectFiles(솔루션이 참조하는 Shaders 프로젝트), Intermediate\vcpkg_installed(다시 받는 데 오래 걸림)
rem vcpkg 설치 결과까지 지우려면: ClearCache.bat all  (다음 SlnGenerator.bat 실행 때 다시 설치된다)
rem 주의: if/for의 괄호 블록 안에서 echo에 괄호를 쓰면 블록이 일찍 닫힌다. 블록 안에는 echo를 두지 않는다.
cd /d "%~dp0"

echo [1/3] 빌드 결과 (Build)
if exist "Build" rmdir /s /q "Build"

echo [2/3] 중간 파일 (Intermediate: 셰이더 컴파일 캐시 등)
if exist "Intermediate" for /d %%D in ("Intermediate\*") do if /i not "%%~nxD"=="ProjectFiles" if /i not "%%~nxD"=="vcpkg_installed" rmdir /s /q "%%D"
if exist "Intermediate" del /q "Intermediate\*" > nul 2>&1
if /i not "%~1"=="all" goto SkipVcpkg
echo       vcpkg 설치 결과 (Intermediate\vcpkg_installed)
if exist "Intermediate\vcpkg_installed" rmdir /s /q "Intermediate\vcpkg_installed"
:SkipVcpkg

echo [3/3] OBJ Bake 캐시 (*.obj.bin)
del /s /q "Assets\*.obj.bin" > nul 2>&1
del /s /q "Resources\*.obj.bin" > nul 2>&1

echo 완료
if /i "%~1"=="nopause" goto :eof
if /i "%~2"=="nopause" goto :eof
PAUSE
