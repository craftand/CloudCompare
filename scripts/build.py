import os
import sys
import subprocess
import argparse
from pathlib import Path
from get_qt_version import get_qt_version

def main():
    parser = argparse.ArgumentParser(description="Configure, build, and package CloudCompare")
    parser.add_argument("--drive", default="D:", help="Target drive for build/Qt")
    parser.add_argument("--vs-path", default="", help="Custom VS build tools path")
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug", "RelWithDebInfo"], help="CMake build type")
    args = parser.parse_args()

    qt_version = get_qt_version()
    drive = args.drive
    if not drive.endswith(os.sep) and not drive.endswith("/"):
        drive = drive + os.sep

    qt_prefix = Path(drive) / "Qt" / qt_version / "msvc2022_64"
    build_dir = Path(drive) / "CloudCompareBuild"
    install_dir = Path(drive) / "CloudCompareInstall"
    vs_path = Path(args.vs_path) if args.vs_path else (Path(drive) / "VS2022BuildTools")
    build_type = args.build_type

    print(f"Discovered Qt Version: {qt_version}")
    print(f"Qt Prefix: {qt_prefix}")
    print(f"Build Directory: {build_dir}")
    print(f"Install Directory: {install_dir}")

    # Locate vcvars64.bat
    vcvars_candidates = [
        Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"),
        Path("C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvars64.bat"),
        Path("C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvars64.bat"),
        Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"),
        vs_path / "VC/Auxiliary/Build/vcvars64.bat"
    ]

    vcvars_bat = None
    for candidate in vcvars_candidates:
        if candidate.exists():
            vcvars_bat = candidate
            break

    cmake_cmd = (
        f'cmake -B "{build_dir}" -G Ninja '
        f'-DCMAKE_BUILD_TYPE={build_type} '
        f'-DCMAKE_PREFIX_PATH="{qt_prefix}" '
        f'-DCMAKE_INSTALL_PREFIX="{install_dir}" '
        f'-DPLUGIN_EXAMPLE_STANDARD=ON '
        f'-DPLUGIN_STANDARD_QJSONRPC=ON . && '
        f'cmake --build "{build_dir}" --parallel && '
        f'cmake --install "{build_dir}"'
    )

    if vcvars_bat:
        print(f"Using MSVC environment from: {vcvars_bat}")
        full_cmd = f'cmd.exe /c ""{vcvars_bat}" && {cmake_cmd}"'
    else:
        print("Warning: vcvars64.bat not found. Running cmake directly...")
        full_cmd = f'cmd.exe /c "{cmake_cmd}"'

    res = subprocess.run(full_cmd, shell=True)
    sys.exit(res.returncode)

if __name__ == '__main__':
    main()
