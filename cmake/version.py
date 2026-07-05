from pathlib import Path


def safe_version(version):
    """Extract the plain X.Y.Z prefix of a version string, if present."""
    import re
    m = re.match(r"(\d+\.\d+\.\d+)", version)
    return m.group(1) if m else None


def git_run(args, default=None, cwd=None):
    from subprocess import run
    if not Path(cwd).joinpath('.git').is_dir():
        return default
    res = run(args, cwd=cwd, capture_output=True, text=True)
    return default if res.returncode else res.stdout.strip()


def pkg_info_version(root: Path):
    """An sdist has no git metadata but carries its resolved version in PKG-INFO."""
    file = Path(root).joinpath('PKG-INFO')
    if not file.is_file():
        return None
    for line in file.read_text().splitlines():
        if line.startswith('Version:'):
            return line.split(':', maxsplit=1)[1].strip()
    return None


def get_version(root: Path, override=None):
    """Resolve the build version.

    An explicit override (scikit-build-core's setuptools_scm result, passed
    through check.cmake) beats `git describe`, which beats an sdist's PKG-INFO.
    """
    candidates = (
        override,
        # --tags: releases made via `gh release create` are lightweight tags
        git_run(['git', 'describe', '--tags', '--long'], cwd=root),
        pkg_info_version(root),
    )
    for candidate in candidates:
        if candidate:
            full = candidate.lstrip('v')
            if safe := safe_version(full):
                return full, safe
    return '0.0.0', '0.0.0'


def git_info(root: Path):
    sha = git_run(['git', 'rev-parse', 'HEAD'], cwd=root, default='0')
    branch = git_run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], cwd=root, default='UNKNOWN')
    return sha, branch


def version_header(root: Path, filename: Path, override=None):
    from textwrap import dedent
    from datetime import datetime
    import platform

    sha, branch= git_info(root)
    full_v, safe_v = get_version(root, override=override)
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
    parser.add_argument('--version', default=None,
                        help='externally resolved version, overrides git metadata')
    args = parser.parse_args()
    version_header(Path(args.repository), Path(args.directory).joinpath('version.hpp'),
                   override=args.version)


if __name__ == '__main__':
    main()
