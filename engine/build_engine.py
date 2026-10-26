"""Packages the engine into a folder with a single program in it
(dist/transform6-engine/transform6-engine[.exe]) so nobody needs Python.

    python build_engine.py            # build
    python build_engine.py --models DIR   # also copy the splitter model in
"""
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
models = None
if "--models" in args:
    models = args[args.index("--models") + 1]

cmd = [
    sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir",
    "--console", "--name", "transform6-engine",
    "--distpath", os.path.join(here, "dist"),
    "--workpath", os.path.join(here, "build"),
    "--specpath", os.path.join(here, "build"),
    "--collect-data", "basic_pitch",
    "--collect-submodules", "basic_pitch",
    "--collect-all", "demucs",
    "--collect-data", "librosa",
    "--collect-binaries", "onnxruntime",
    "--collect-data", "pretty_midi",
    "--hidden-import", "sklearn.utils._typedefs",
    "--hidden-import", "soundfile",
    "--exclude-module", "tensorflow",
    "--exclude-module", "matplotlib",
    "--exclude-module", "tkinter",
    "--exclude-module", "IPython",
    os.path.join(here, "transform6_engine.py"),
]
print(" ".join(cmd))
subprocess.check_call(cmd)

out = os.path.join(here, "dist", "transform6-engine")
if models:
    dst = os.path.join(out, "models")
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(models):
        shutil.copy2(os.path.join(models, f), dst)
    print("copied models into", dst)
print("built:", out)
