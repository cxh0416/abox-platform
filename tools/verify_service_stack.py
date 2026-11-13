"""Check declared owner call chains against compiler stack frames and a reserve."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--budget', type=Path, required=True)
args = parser.parse_args()
def read_budget(path, seen=None):
    path = path.resolve()
    seen = set() if seen is None else seen
    if path in seen:
        raise ValueError(f'Circular stack budget inheritance: {path}')
    seen.add(path)
    selected = json.loads(path.read_text(encoding='utf-8'))
    parent = selected.pop('extends', None)
    if not parent:
        return selected
    budget = read_budget(path.parent / parent, seen)
    budget.setdefault('chains', {}).update(selected.pop('chains', {}))
    budget.update(selected)
    return budget


budget = read_budget(args.budget)
frames = {}
for path in args.build.rglob('*.su'):
    for line in path.read_text(errors='replace').splitlines():
        fields = line.split('\t')
        if len(fields) < 3:
            continue
        source = fields[0].rsplit(':', 3)[0]
        if not Path(source).is_file():
            continue
        name = fields[0].rsplit(':', 1)[-1].split('.')[0]
        frames[name] = max(frames.get(name, 0), int(fields[1]))
for name, chain in budget['chains'].items():
    missing = [fn for fn in chain if fn not in frames]
    if missing:
        raise SystemExit('Missing stack evidence: ' + ', '.join(missing))
    used = sum(frames[fn] for fn in chain)
    total = used + budget['reserve_bytes']
    if total > budget['stack_bytes']:
        raise SystemExit(f'{name}: {used} + reserve exceeds {budget["stack_bytes"]}')
    print(f'PASS {name}: known frames {used} + reserve {budget["reserve_bytes"]} <= {budget["stack_bytes"]}')
