"""Compile actual product paths and compare HEAD baseline vs working tree.

No devices, broker or release packaging. Fixed artifacts live under build/host.
The meal fixture executes its real command dispatcher and response queue;
business collaborators are host stubs and OTA/control calls assert unreachable.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent.parent if ROOT.name == 'platform' else ROOT.parent


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True,
                          **kwargs).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--baseline-manifest', type=Path,
                        help='Saved summary.json pins the original product/platform revisions')
    for product in ('meal', 'sweeper', 'patrol'):
        parser.add_argument('--' + product + '-root', type=Path)
    args = parser.parse_args()
    cc = shutil.which(args.cc) or args.cc
    products = dict(zip(('meal', 'sweeper', 'patrol'),
                       ('Meal_Delivery_Vehicle', 'Sweeper_VCU', 'Patrol_Vehicle_Chassis')))
    build = ROOT / 'build/host/internal-v2'
    build.mkdir(parents=True, exist_ok=True)
    records = []
    pinned = {r['product']: r for r in json.loads(args.baseline_manifest.read_text(encoding='utf8'))} if args.baseline_manifest else {}
    shared = ['components/cloud/src/abox_cloud_internal_v2.c',
              'components/cloud/src/abox_cloud_response.c']

    def baseline(repo, name, target, revision):
        target.write_bytes(run(['git', '-C', repo, 'show', revision + ':' + name]))
        return target

    def compile_run(label, includes, sources, defines=()):
        exe = build / (label + '.exe')
        command = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-Wno-unused-parameter', '-D__USE_MINGW_ANSI_STDIO=1']
        command += ['-I' + str(p) for p in includes] + list(defines) + list(sources) + ['-o', exe]
        run(command)
        return run([exe])

    for product, folder in products.items():
        repo = getattr(args, product + '_root') or WORKSPACE / folder
        platform = repo / 'platform'
        product_revision = pinned.get(product, {}).get('baseline_revision') or run(['git', '-C', repo, 'rev-parse', 'HEAD']).decode().strip()
        platform_revision = pinned.get(product, {}).get('platform_revision') or run(['git', '-C', platform, 'rev-parse', 'HEAD']).decode().strip()
        includes = [p / 'include' for p in (platform / 'components').iterdir() if p.is_dir()]
        includes += [platform / 'components/cjson']
        old = [baseline(platform, name, build / (product + '-before-' + Path(name).name), platform_revision)
               for name in shared]
        new = [platform / name for name in shared]
        support = [platform / 'components/cjson/cJSON.c',
                   platform / 'components/ec800/src/abox_tls_secret_record.c']
        before_wire = compile_run(product + '-wire-before', includes,
            [ROOT / 'tests/internal_v2_wire_vectors.c', old[1]])
        after_wire = compile_run(product + '-wire-after', includes,
            [ROOT / 'tests/internal_v2_wire_vectors.c', new[1]])
        assert before_wire == after_wire, product + ' response bytes changed'
        if product == 'meal':
            includes += [repo / 'app/tests/feature_stubs', repo / 'app/Core/User/cloud/private']
            includes += [p / 'include' for p in (repo / 'app/Core/User').iterdir() if p.is_dir()]
            support += [platform / 'components/cloud/src/abox_cloud_queue.c',
                        platform / 'components/cloud/src/abox_cloud_dispatch.c',
                        platform / 'components/mqtt/src/abox_mqtt_v4.c',
                        repo / 'app/Core/User/common/src/sha256.c',
                        repo / 'app/Core/User/cloud/src/meal_mqtt_profile.c']
            defines = []
            for macro, name in [('MEAL_COMMAND_SOURCE', 'mqtt_app_cmd.c'),
                                ('MEAL_RESPONSE_SOURCE', 'cloud_responses.c')]:
                path = baseline(repo, 'app/Core/User/cloud/src/' + name, build / ('meal-before-' + name), product_revision)
                defines.append('-D' + macro + '="' + path.as_posix() + '"')
            fixture = repo / 'app/tests/internal_envelope_response_test.c'
            before = compile_run('meal-before', includes, [fixture, *old, *support], defines)
            after = compile_run('meal-after', includes, [fixture, *new, *support])
            assert before == after, 'meal dispatcher/queue payload bytes changed'
        else:
            includes += [p / 'include' for p in (repo / 'app/Core/User').iterdir() if p.is_dir()]
            sources = [repo / 'tests/cloud_protocol_test.c', repo / 'app/Core/User/cloud/src/cloud_protocol.c']
            before = compile_run(product + '-before', includes, [*sources, old[0], *support])
            after = compile_run(product + '-after', includes, [*sources, new[0], *support])
            assert before == after, product + ' parser regression changed'
        records.append({'product': product, 'baseline_revision': product_revision,
                        'platform_revision': platform_revision,
                        'product_regression': 'passed', 'wire_vectors': 359,
                        'before_after_equal': True,
                        'product_output_sha256': hashlib.sha256(after).hexdigest(),
                        'response_output_sha256': hashlib.sha256(after_wire).hexdigest()})
        print(product + ': product regression and response byte comparison PASS')
    (build / 'summary.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf8', newline='\n')


if __name__ == '__main__':
    main()
