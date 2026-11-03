"""Run production-panel contracts and parse every generated layout as JSON."""
import json
import subprocess
import sys
from pathlib import Path
subprocess.run([sys.argv[1]], check=True)
def walk(node):
    yield node
    for child in node.get('children', []):
        yield from walk(child)
for minimum in (1, 4, 24):
    tree = json.loads(Path(f'hud-layout-{minimum}.json').read_text())
    nodes = list(walk(tree))
    names = [n['name'] for n in nodes]
    assert len(names) == len(set(names))
    assert len(nodes) < 250
    assert not any(n['type'] == 'SettingsSliderWidget' for n in nodes)
    for count in range(minimum, 25):
        assert f'SliderMarker{count}' in names
        assert f'SliderTooltipValue{count}' in names
    assert 'SliderMarker25' not in names
    assert all(f'ActualDigit{pos}{digit}' in names for pos in range(5) for digit in range(10))
    assert not any(k.endswith('Message') for n in nodes for k in n.get('fields', {})), 'HUD layout must not invoke native message parsing'
    button = json.loads(Path(f'hud-button-{minimum}.json').read_text())
    assert not any(k.endswith('Message') for n in walk(button) for k in n.get('fields', {})), 'HUD diamond must not invoke native message parsing'
    assert button['name'].startswith('ruffneckk-playerx-scaling-tweaks/')
    print(f'Valid HUD JSON: minimum={minimum}, cap=24, widgets={len(nodes)}')
