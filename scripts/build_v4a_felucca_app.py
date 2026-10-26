#!/usr/bin/env python3
"""Compile/link the REAL patched Felucca unity app, not only overlay stubs.

No SDK boot blobs are redistributed. The resulting app is NOT an installable
.fwsc package; use the pinned upstream packager with the required SDK to make it.
"""
import argparse
import importlib.util
import os
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("felucca", type=Path)
    args = ap.parse_args()
    root = args.felucca.resolve()
    # The benchmark is single-core on both platforms, even when hosted in
    # Felucca. Keep the original size-oriented production compile settings.
    os.environ["FELUCCA_CPU2"] = "0"
    os.environ["FELUCCA_CDC"] = "1"
    os.chdir(root)
    spec = importlib.util.spec_from_file_location("felucca_build", root/"tools/build.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.generate()
    image, syms, dis, rt = module.build_app()
    errors, notes = module.check(image, syms, dis, rt)
    (root/"build/felucca.symbols").write_text(syms)
    for note in notes: print(note)
    for error in errors: print("LINK-GATE ERROR:", error)
    if errors: raise SystemExit(1)
    print("V4A full Felucca app compile/link/layout gate passed")


if __name__ == "__main__": main()
