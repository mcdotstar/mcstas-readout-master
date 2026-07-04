from pathlib import Path


def intable(x):
    try:
        int(x)
    except:
        return False
    return True


def semver_groups(s):
    import re
    r = re.compile(r"^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$")
    if m := r.match(s):
        d = m.groupdict()
        return d
    return None


def git_run(args, default=None, cwd=None):
    from subprocess import run
    from pathlib import Path
    if not Path(cwd).joinpath('.git').is_dir():
        return default
    res = run(args, cwd=cwd, capture_output=True, text=True)
    return default if res.returncode else res.stdout.strip()


def get_version(root: Path):
    file = Path(root).joinpath('VERSION')
    fallback = file.read_text()
    version = git_run(['git', 'describe', '--long'], cwd=root, default=fallback).split('v', maxsplit=1)[-1]
    d = semver_groups(version)
    if not d:
        # warn about a non-semantic-version value?
        d = semver_groups(fallback)
        version = fallback
    safe = f"{d['major']}.{d['minor']}.{d['patch']}"
    if safe != fallback:
        file.write_text(safe)
    return version, f"{d['major']}.{d['minor']}.{d['patch']}"


def git_info(root: Path):
    sha = git_run(['git', 'rev-parse', 'HEAD'], cwd=root, default='0')
    branch = git_run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], cwd=root, default='UNKNOWN')
    return sha, branch


def version_header(root: Path, filename: Path):
    from textwrap import dedent
    from datetime import datetime
    import platform

    sha, branch= git_info(root)
    full_v, safe_v = get_version(root)
    date_str = datetime.now().date().isoformat()
    hostname = platform.node()

    text = dedent(fr"""
    #pragma once
    //! \file
    namespace libreadout::version{{
        //! `project` git repository revision information at build time
        auto constexpr git_revision = "{sha}";
        //! `project` git repository branch at build time
        auto constexpr git_branch = "{branch}";
        //! build date in YYYY-MM-DD format
        auto constexpr build_date = "{date_str}";
        //! `project` version
        auto constexpr version_number = "{safe_v}";
        //! hostname of the build machine
        auto constexpr build_hostname = "{hostname}";
        //! version with metadata included
        auto constexpr meta_version = "{full_v}";
    }}
    """)

    if not (parent := filename.parent).is_dir():
        parent.mkdir(parents=True)

    # Do not write the file if it already exists and is the same, to avoid unnecessary rebuilds
    if not filename.is_file() or filename.read_text() != text:
        with filename.open('w') as file:
            file.write(text)

    # Plus write to stdout so CMake can capture and use the version
    print(safe_v)


def main():
    from argparse import ArgumentParser
    parser = ArgumentParser()
    parser.add_argument('repository')
    parser.add_argument('directory')
    args = parser.parse_args()
    version_header(Path(args.repository), Path(args.directory).joinpath('version.hpp'))


if __name__ == '__main__':
    main()