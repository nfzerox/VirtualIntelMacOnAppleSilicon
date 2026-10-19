#!/usr/bin/env python3
"""Check native install planning without modifying any live guest."""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile

source=Path(__file__).resolve().parents[1]
tool=sys.argv[1]
for guest,release,build,filename in [('mojave','10.14.6','18G9999','install-targets-mojave.txt'),('catalina','10.15.7','19H9999','install-targets.txt')]:
    with tempfile.TemporaryDirectory(prefix='vim-install-plan-') as temporary:
        base=Path(temporary).resolve();root=base/'root';payload=base/'payload'
        root.mkdir();payload.mkdir()
        version=root/'System/Library/CoreServices/SystemVersion.plist'
        version.parent.mkdir(parents=True)
        version.write_bytes(plistlib.dumps(dict(ProductVersion=release,ProductBuildVersion=build)))
        system_cache=root/'System/Library/PrelinkedKernels/prelinkedkernel'
        system_cache.parent.mkdir(parents=True);system_cache.write_bytes(b'original boot cache fixture')
        targets=(source/'support'/filename).read_text().splitlines()
        for target in targets:
            path=payload/target[1:];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture replacement')
            path.chmod(0o666)
        receipt=dict(FormatVersion=1,Guest=guest,ProductVersion=release,ProductBuildVersion=build,Prepared=True,Installed=False,PayloadDirectory=str(payload))
        (payload/'.VirtualIntelMacPreparation.plist').write_bytes(plistlib.dumps(receipt))
        command=[tool,'--plan',str(root),str(payload),str(base/'journal.plist')]
        if '--reject-unsigned' in sys.argv[2:]:
            refused=subprocess.run(command,capture_output=True)
            assert refused.returncode and not (base/'journal.plist').exists()
            assert b'Invalid payload signature' in refused.stderr
            print(f'PASS {guest}: production planner rejects unsigned payload before journal creation')
            continue
        result=json.loads(subprocess.check_output(command))
        assert result['Planned'] and not result['Installed'] and not result['Ready']
        assert result['TargetCount']==len(targets)
        assert all((payload/p[1:]).stat().st_mode&0o777==0o644 for p in targets)
        journal=plistlib.loads((base/'journal.plist').read_bytes())
        snapshot=journal['Metadata']['BootCaches']
        saved=Path(journal['Metadata']['BootCacheDirectory'])/'cache-0'
        assert saved.read_bytes()==system_cache.read_bytes()
        assert len(snapshot['Caches'])==(1 if guest=='mojave' else 2)
        if guest=='catalina': assert not snapshot['Caches'][1]['Present']
        assert list(root.rglob('*')) and not any((root/p[1:]).exists() for p in targets)
        if '--exercise-success' in sys.argv[2:]:
            for relative in ['System/Library/Extensions','Library/Extensions']:
                (root/relative).mkdir(parents=True,exist_ok=True)
            applied=json.loads(subprocess.check_output([tool,'--apply',str(base/'journal.plist')]))
            assert applied['FilesPublished'] and applied['RebootRequired'] and not applied['Ready']
            outcome=plistlib.loads((base/'journal.plist.result.plist').read_bytes())
            assert outcome['Phase']=='awaiting-reboot' and outcome['Cache']['CacheRebuilt'] and not outcome['Ready']
            assert outcome['TransactionID']==journal['ID']
            assert system_cache.read_bytes()==b'fresh generated cache'
            subprocess.check_output([tool,'--rollback',str(base/'journal.plist')])
            assert not any((root/p[1:]).exists() for p in targets)
            assert system_cache.read_bytes()==b'original boot cache fixture'
            assert plistlib.loads((base/'journal.plist.result.plist').read_bytes())['Phase']=='rolled-back'
            print(f'PASS {guest}: successful cache build persists awaiting-reboot, explicit rollback restores originals')
        if '--exercise-rollback' in sys.argv[2:]:
            outside=base/'outside-lock';outside.write_bytes(b'leave untouched')
            lock=root/'install.lock';lock.symlink_to(outside)
            refused=subprocess.run([tool,'--apply',str(base/'journal.plist')],capture_output=True)
            assert refused.returncode and b'installation lock' in refused.stderr,refused.stderr
            assert outside.read_bytes()==b'leave untouched'
            assert plistlib.loads((base/'journal.plist').read_bytes())==journal
            lock.unlink()
            original_saved=saved.read_bytes()
            saved.write_bytes(b'corrupted recovery cache')
            refused=subprocess.run([tool,'--apply',str(base/'journal.plist')],
                env=dict(os.environ,VIM_TEST_CACHE_BUILDER='/usr/bin/false'),capture_output=True)
            assert refused.returncode and b'integrity verification' in refused.stderr,refused.stderr
            assert plistlib.loads((base/'journal.plist').read_bytes())==journal
            assert not any((root/p[1:]).exists() for p in targets)
            assert system_cache.read_bytes()==original_saved
            saved.write_bytes(original_saved)
            print(f'PASS {guest}: damaged recovery cache rejected before publication')
            failed=subprocess.run([tool,'--apply',str(base/'journal.plist')],
                env=dict(os.environ,VIM_TEST_CACHE_BUILDER='/usr/bin/false'),capture_output=True)
            assert failed.returncode and b'original files and caches restored' in failed.stderr,failed.stderr
            assert not any((root/p[1:]).exists() for p in targets)
            assert system_cache.read_bytes()==b'original boot cache fixture'
            assert plistlib.loads((base/'journal.plist').read_bytes())['Phase']=='rolled-back'
            outcome=plistlib.loads((base/'journal.plist.result.plist').read_bytes())
            assert outcome['Phase']=='rolled-back' and not outcome['Installed'] and not outcome['Ready']
            assert outcome['TransactionID']==journal['ID'] and outcome['ProductBuildVersion']==build
            assert outcome['BootSessionUUID'] and outcome['Failure']
            print(f'PASS {guest}: combined apply automatically rolled back after cache-builder failure')
        version.write_bytes(plistlib.dumps(dict(ProductVersion=release,ProductBuildVersion=build+'changed')))
        refused=subprocess.run(command[:-1]+[str(base/'wrong-build.plist')],capture_output=True)
        assert refused.returncode and not (base/'wrong-build.plist').exists()
        print(f'PASS {guest}: fixed-target plan, no publication, changed-build refusal')
