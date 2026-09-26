"""Run CPU checks of the reused combiner using actual engine/SDK constants."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
helper = ROOT / "engine_port/VitaFixedFunction.h"
engine = (ROOT / "CryCommon/IShader.h").read_text()
sdk = (ROOT / "engine_port/vita_kit/vitagl/include/vitaGL.h").read_text()
types = "\n".join(re.search(r"enum " + name + r"\s*\{.*?\};", engine, re.S).group()
                  for name in ("EColorOp", "EColorArg"))
constants = set(re.findall(r"\bGL_\w+", helper.read_text()))
for name in sorted(constants):
    match = re.search(r"^#define\s+" + name + r"\s+([^\r\n]+)", sdk, re.M)
    if not match:
        raise RuntimeError(f"Pinned vitaGL lacks {name}")
    types += f"\n#define {name} {match[1]}"

compiler = shutil.which("g++")
if not compiler:
    raise RuntimeError("Host g++ is required for fixed-function checks")
with tempfile.TemporaryDirectory(prefix="farcry-combiner-") as temp:
    temp = Path(temp)
    (temp / "FixedFunctionTestTypes.h").write_text(types)
    binary = temp / "combiner.exe"
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-static",
                    "-I" + str(temp), "-I" + str(helper.parent),
                    str(Path(__file__).with_name("test_fixed_function.cpp")),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
