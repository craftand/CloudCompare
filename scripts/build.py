import os
import sys
import platform
import subprocess
import argparse
from pathlib import Path
from get_qt_version import get_qt_version

def main():
    parser = argparse.ArgumentParser(description="Configure, build, and package CloudCompare")
    parser.add_argument("--drive", default="D:", help="Target drive for build/Qt on Windows")
    parser.add_argument("--vs-path", default="", help="Custom VS build tools path (Windows)")
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug", "RelWithDebInfo"], help="CMake build type")
    parser.add_argument("--skip-install", action="store_true", help="Skip cmake --install step")
    args = parser.parse_args()

    system = platform.system()
    qt_version = get_qt_version()
    build_type = args.build_type

    if system == "Windows":
        drive = args.drive
        if not drive.endswith(os.sep) and not drive.endswith("/"):
            drive = drive + os.sep

        qt_prefix = Path(drive) / "Qt" / qt_version / "msvc2022_64"
        build_dir = Path(drive) / "CloudCompareBuild"
        install_dir = Path(drive) / "CloudCompareInstall"
        vs_path = Path(args.vs_path) if args.vs_path else (Path(drive) / "VS2022BuildTools")
    else: # macOS / Linux
        home = Path.home()
        qt_prefix = home / "Qt" / qt_version / "macos"
        build_dir = Path("build")
        install_dir = Path("build/install")

    print(f"Platform: {system}")
    print(f"Discovered Qt Version: {qt_version}")
    print(f"Qt Prefix: {qt_prefix}")
    print(f"Build Directory: {build_dir}")
    print(f"Install Directory: {install_dir}")

    cmake_args = [
        f'-DCMAKE_BUILD_TYPE={build_type}',
        f'-DCMAKE_PREFIX_PATH="{qt_prefix}"',
        f'-DCMAKE_INSTALL_PREFIX="{install_dir}"',
        '-DBUILD_SHARED_LIBS=OFF',
        '-DPLUGIN_EXAMPLE_STANDARD=ON',
        '-DPLUGIN_STANDARD_QBEACONRPC=ON',
        '-DPLUGIN_STANDARD_3DMASC=OFF',
        '-DPLUGIN_STANDARD_QM3C2=ON',
        '-DPLUGIN_STANDARD_QANIMATION=ON',
        '-DPLUGIN_STANDARD_QBROOM=ON',
        '-DPLUGIN_STANDARD_QCSF=ON',
        '-DPLUGIN_STANDARD_QCANUPO=ON',
        '-DPLUGIN_STANDARD_QCLOUDLAYERS=ON',
        '-DPLUGIN_STANDARD_QCOMPASS=ON',
        '-DPLUGIN_STANDARD_QCOLORIMETRIC_SEGMENTER=ON',
        '-DPLUGIN_STANDARD_QFACETS=ON',
        '-DPLUGIN_STANDARD_QHPR=ON',
        '-DPLUGIN_STANDARD_QHOUGH_NORMALS=OFF',
        '-DPLUGIN_STANDARD_QMPLANE=ON',
        '-DPLUGIN_STANDARD_QPCL=OFF',
        '-DPLUGIN_STANDARD_QPCV=ON',
        '-DPLUGIN_STANDARD_QPOISSON_RECON=ON',
        '-DPLUGIN_STANDARD_QRANSAC_SD=ON',
        '-DPLUGIN_STANDARD_QSRA=ON',
        '-DPLUGIN_STANDARD_QVOXFALL=ON',
        '-DPLUGIN_IO_QADDITIONAL=ON',
        '-DPLUGIN_IO_QCORE=ON',
        '-DPLUGIN_IO_QPHOTOSCAN=OFF',
        '-DPLUGIN_GL_QEDL=ON',
        '-DPLUGIN_GL_QSSAO=ON',
        '-DBUILD_TESTING=ON',
    ]

    is_ci = os.environ.get("CI") == "true" or os.environ.get("GITHUB_ACTIONS") == "true"
    if is_ci:
        parallel_flag = "--parallel"
        print("CI environment detected: Using 100% CPU parallel cores.")
    else:
        cpu_count = os.cpu_count() or 4
        jobs = max(1, int(cpu_count * 0.80))
        parallel_flag = f"--parallel {jobs}"
        print(f"Local environment detected: Capping parallel build jobs at 80% ({jobs}/{cpu_count} CPU cores).")

    cmake_flags_str = " ".join(cmake_args)

    if args.skip_install:
        install_cmd = ""
    else:
        install_cmd = f' && cmake --install "{build_dir}"'

    cmake_cmd = (
        f'cmake -B "{build_dir}" -G Ninja {cmake_flags_str} . && '
        f'cmake --build "{build_dir}" {parallel_flag}'
        f'{install_cmd}'
    )

    if system == "Windows":
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

        if vcvars_bat:
            print(f"Using MSVC environment from: {vcvars_bat}")
            full_cmd = f'cmd.exe /c ""{vcvars_bat}" && {cmake_cmd}"'
        else:
            print("Warning: vcvars64.bat not found. Running cmake directly...")
            full_cmd = f'cmd.exe /c "{cmake_cmd}"'
    else:
        full_cmd = cmake_cmd

    res = subprocess.run(full_cmd, shell=True)
    sys.exit(res.returncode)

if __name__ == '__main__':
    main()

