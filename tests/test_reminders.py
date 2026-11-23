"""Production reminder service and MCP with deterministic storage failure injection."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="passport-reminders-") as tmp:
    target = str(Path(tmp) / "reminders")
    cjson = Path(tmp) / "cjson.o"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-deprecated-declarations", "-g", "-fsanitize=address,undefined",
                    "-c", "tests/vendor/cjson/cJSON.c", "-o", str(cjson)],
                   cwd=root, check=True)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-Wno-misleading-indentation", "-g", "-fsanitize=address,undefined",
                    "-Itests/reminder_mocks", "-Itests/vendor/cjson",
                    "-Imain", "-Icomponents/bsp/include", "tests/test_reminders.cc",
                    str(cjson), "-o", target], cwd=root, check=True)
    subprocess.run([target], cwd=root, check=True)
