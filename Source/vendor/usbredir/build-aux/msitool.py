#!/usr/bin/python3

import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 8:
    print("syntax: %s BUILD-DIR PREFIX WIXL-ARCH MSI-FILE WXS-FILE " \
          "WIXL-HEAT-PATH WIXL-PATH" % sys.argv[0], file=sys.stderr)
    sys.exit(1)

builddir = sys.argv[1]
prefix = sys.argv[2]
arch = sys.argv[3]
msifile = sys.argv[4]
wxs = sys.argv[5]
wixl_heat = sys.argv[6]
wixl = sys.argv[7]

def build_msi():
    manufacturer = "Usbredir project"
    if "DESTDIR" not in os.environ:
        print("$DESTDIR environment variable missing. "
              "Please run 'ninja install' before attempting to "
              "build the MSI binary, and set DESTDIR to point "
              "to the installation virtual root.", file=sys.stderr)
        sys.exit(1)

    if "MANUFACTURER" not in os.environ:
        os.environ["MANUFACTURER"] = manufacturer

    vroot = os.environ["DESTDIR"]

    manifest = []
    for root, subFolder, files in os.walk(vroot):
        for item in files:
            path = str(os.path.join(root,item))
            manifest.append(path)

    wxsfiles = subprocess.run(
        [
            wixl_heat,
            "-p", vroot + prefix + "/",
            "--component-group", "CG.usbredirect",
            "--var", "var.DESTDIR",
            "--directory-ref", "INSTALLDIR",
        ],
        input="\n".join(manifest),
        encoding="utf8",
        check=True,
        capture_output=True)

    wxsfilelist = os.path.join(builddir, "data", "usbredirect-files.wxs")
    with open(wxsfilelist, "w") as fh:
        print(wxsfiles.stdout, file=fh)

    wixlenv = os.environ
    wixlenv["MANUFACTURER"] = manufacturer

    subprocess.run(
        [
            wixl,
            "-D", "SourceDir=" + prefix,
            "-D", "DESTDIR=" + vroot + prefix,
            "--arch", arch,
            "-o", msifile,
            wxs, wxsfilelist,
        ],
        check=True,
        env=wixlenv)

build_msi()
