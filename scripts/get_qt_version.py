import re
from pathlib import Path

def get_qt_version():
    conda_file = Path('.ci/conda-win.yml')
    if conda_file.exists():
        match = re.search(r'qt6-main==([0-9]+\.[0-9]+\.[0-9]+)', conda_file.read_text())
        if match:
            return match.group(1)
    return '6.8.0'

if __name__ == '__main__':
    print(get_qt_version())
