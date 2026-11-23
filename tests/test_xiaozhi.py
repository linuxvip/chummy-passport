"""Compile production Xiaozhi sources with deterministic fault-injection mocks."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="passport-xiaozhi-") as temporary:
    for name in ("app", "audio", "protocol"):
        executable = Path(temporary) / name
        command = [os.environ.get("CXX", "c++"), "-std=c++20", "-g", "-O1",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                   "-Wno-unused-variable", "-Wno-misleading-indentation",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-Itests/xiaozhi_mocks", "-Imain", "-Icomponents/bsp/include",
                   f"tests/test_xiaozhi_{name}.cc",
                   "-o", str(executable)]
        if name == "protocol":
            cjson_obj = Path(temporary) / "cjson.o"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-g", "-O1",
                            "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                            "-fsanitize=address,undefined", "-c",
                            "tests/vendor/cjson/cJSON.c", "-o", str(cjson_obj)],
                           cwd=ROOT, check=True)
            command.append(str(cjson_obj))
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
print("Xiaozhi host regression: PASS (device tests not run)")
