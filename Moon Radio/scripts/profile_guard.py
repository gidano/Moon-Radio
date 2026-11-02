Import("env")

import hashlib
import os
import shutil


project_dir = os.path.realpath(env.subst("$PROJECT_DIR"))
build_dir = os.path.realpath(env.subst("$BUILD_DIR"))
options_path = os.path.join(project_dir, "options.h")
signature_path = os.path.join(build_dir, ".options-signature")

with open(options_path, "rb") as options_file:
    signature = hashlib.sha256(options_file.read()).hexdigest()

if os.path.isfile(signature_path):
    with open(signature_path, "r", encoding="ascii") as signature_file:
        previous_signature = signature_file.read().strip()

    if previous_signature != signature:
        expected_build_root = os.path.realpath(os.path.join(project_dir, ".pio", "build"))
        if os.path.commonpath((expected_build_root, build_dir)) != expected_build_root:
            raise RuntimeError("Refusing to clear build directory outside .pio/build")
        print("Active options.h changed; clearing stale PlatformIO build artifacts")
        shutil.rmtree(build_dir)

os.makedirs(build_dir, exist_ok=True)
with open(signature_path, "w", encoding="ascii", newline="\n") as signature_file:
    signature_file.write(signature + "\n")
