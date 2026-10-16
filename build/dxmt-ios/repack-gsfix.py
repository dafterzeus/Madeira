#!/usr/bin/env python3
"""Replace CI-built entries, preserving every fexalloc addition/PE override."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plain', required=True, type=Path, help='original plain CI IPA used to make fexalloc')
parser.add_argument('--fexalloc', required=True, type=Path)
parser.add_argument('--rebuilt', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
assert len({p.resolve() for p in (args.plain, args.fexalloc, args.rebuilt, args.output)}) == 4


def hashes(z):
    names = z.namelist()
    assert len(names) == len(set(names)), 'duplicate ZIP entries'
    return {n: hashlib.sha256(z.read(n)).hexdigest() for n in names}


with zipfile.ZipFile(args.plain) as plain, zipfile.ZipFile(args.fexalloc) as old, zipfile.ZipFile(args.rebuilt) as new:
    base_hash, old_hash, new_hash = hashes(plain), hashes(old), hashes(new)
    extras = set(old_hash) - set(base_hash)
    overrides = {n for n in base_hash if old_hash[n] != base_hash[n]}
    assert not set(base_hash) - set(old_hash), 'fexalloc removed baseline entries'
    app = 'Payload/Madeira.app/'
    assert overrides == {app + 'aarch64-windows/xtajit.dll', app + 'arm64ec-windows/xtajit64.dll'}
    fixed_extras = {
        app + 'arm64ec-windows/dockhost.exe', app + 'arm64ec-windows/dock-notices.txt',
        *(app + 'aarch64-windows/' + n for n in ('plugplay.exe', 'svchost.exe', 'winedevice.exe')),
    }
    assert fixed_extras <= extras
    assert all(n in fixed_extras or n.startswith(app + 'i386-windows/') for n in extras)
    assert len(extras) == 733, f'unexpected fexalloc additions: {len(extras)}'
    assert set(new_hash) == set(base_hash), 'rebuilt CI entry list differs from the original plain CI build'
    preserve = extras | overrides
    # The CI app must actually contain this patch, not just a fresh signature.
    executable = new.read(app + 'Madeira')
    assert b'dxmt: GS dcl_input_siv semantic=%u register=%u mask=%#x' in executable
    assert b'Unexpected/unhandled geometry shader siv' not in executable
    changed = {n for n in new_hash if n not in preserve and new_hash[n] != old_hash[n]}
    assert app + 'Madeira' in changed
    # Start from the requested fexalloc copy and rewrite only changed members.
    shutil.copy2(args.fexalloc, args.output)
    temporary = args.output.with_suffix('.tmp.ipa')
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for info in old.infolist():
            source = new if info.filename in changed else old
            out.writestr(source.getinfo(info.filename), source.read(info.filename))
    temporary.replace(args.output)
    with zipfile.ZipFile(args.output) as out:
        assert out.testzip() is None
        final_hash = hashes(out)
    assert set(final_hash) == set(old_hash)
    assert all(final_hash[n] == old_hash[n] for n in preserve)
    assert {n for n in old_hash if final_hash[n] != old_hash[n]} == changed
    assert all(final_hash[n] == new_hash[n] for n in new_hash if n not in preserve)
    report = {
        'plain': str(args.plain), 'fexalloc': str(args.fexalloc), 'rebuilt': str(args.rebuilt),
        'output': str(args.output), 'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
        'preserved_extra_entries': sorted(extras), 'preserved_overrides': sorted(overrides),
        'changed_entries': {n: {'fexalloc': old_hash[n], 'gsfix': final_hash[n]} for n in sorted(changed)},
        'added_entries': [], 'removed_entries': [],
        'entry_sha256': final_hash,
    }
    report_path = args.output.with_suffix('.manifest.json')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('entry_sha256', 'preserved_extra_entries')}, indent=2))
    print(f'Preserved {len(extras)} extra entries and both xtajit overrides; manifest: {report_path}')
