"""Package every sample and template as a zip for a release, and write the release notes.

usage: python tools/package_samples.py <tag> [out dir, default dist]

Each project folder (samples/<Name>/, templates/<Name>/ -- any folder with a
project.schizo) becomes <out>/<Name>.zip with the folder itself at the root, so
unzipping gives a project the Hub can add. The asset names carry no version, so
https://github.com/<repo>/releases/latest/download/<Name>.zip always points at
the newest release.

Refuses to package a Git LFS pointer in place of a real file: a checkout made
without LFS would otherwise ship samples whose terrain is a 130-byte text stub.
"""
import os
import sys
import zipfile

LFS_POINTER = b"version https://git-lfs.github.com/spec/"
SKIP_DIRS = {"saves", "cooked", "build", ".cache", "script_cache"}
SKIP_FILES = {"editor.ini", "editor_layout.version"}


def projects(root):
    for kind in ("samples", "templates"):
        base = os.path.join(root, kind)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            d = os.path.join(base, name)
            if os.path.isfile(os.path.join(d, "project.schizo")):
                yield kind, name, d


def manifest_value(d, key):
    with open(os.path.join(d, "project.schizo"), encoding="utf-8") as f:
        for line in f:
            k, _, v = line.partition("=")
            if k.strip() == key:
                return v.strip()
    return ""


def package(d, name, out):
    zpath = os.path.join(out, name + ".zip")
    files = 0
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for here, dirs, names in os.walk(d):
            dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
            for n in sorted(names):
                if n in SKIP_FILES:
                    continue
                p = os.path.join(here, n)
                with open(p, "rb") as f:
                    head = f.read(len(LFS_POINTER))
                if head == LFS_POINTER:
                    sys.exit(f"error: {p} is a Git LFS pointer, not the file -- check out with LFS (git lfs pull)")
                z.write(p, os.path.join(name, os.path.relpath(p, d)))
                files += 1
    return zpath, files


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "dev"
    out = sys.argv[2] if len(sys.argv) > 2 else "dist"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.environ.get("GITHUB_REPOSITORY", "orgolis/WorldShaper-Samples")
    os.makedirs(out, exist_ok=True)
    rows = []
    for kind, name, d in projects(root):
        zpath, files = package(d, name, out)
        engine = manifest_value(d, "engine_version") or "?"
        print(f"{zpath}: {files} files, {os.path.getsize(zpath) // 1024} KB, engine {engine}")
        url = f"https://github.com/{repo}/releases/download/{tag}/{name}.zip"
        rows.append(f"| **{name}** ({kind[:-1]}) | [{name}.zip]({url}) | {engine} or newer |")
    if not rows:
        sys.exit("error: no samples or templates to package")
    notes = [
        "Ready-to-open GameWorldshaper projects.",
        "",
        "| Project | Download | Needs engine |",
        "|---|---|---|",
        *rows,
        "",
        "**To open one:** install the engine version it needs from the Hub, unzip the download, then in the Hub choose",
        "**Projects > Add Existing Project...** and pick the unzipped folder.",
        "",
        "Each project has a README inside. The newest download is always at",
        f"`https://github.com/{repo}/releases/latest/download/<Name>.zip`.",
    ]
    with open(os.path.join(out, "NOTES.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(notes) + "\n")


if __name__ == "__main__":
    main()
