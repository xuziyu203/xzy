"""Prepare or run the frozen Efinity 2026.1 recipe. Local files only; no board access.

Default prepares a fresh snapshot and command plan. Add --execute to run synthesis,
interface, original pack/PNR sequence, bit generation, and explicit-SDC STA.
"""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery_common import contained, efinity_env, hashes, run_logged, sha256, write_json

NAME = 'ti60f225_oob'
NS = 'http://www.efinixinc.com/enf_proj'
ET.register_namespace('efx', NS)


def local(tag):
    return tag.rsplit('}', 1)[-1]


def tcl_path(path):
    value = Path(path).as_posix()
    if any(c in value for c in '{}\n\r'):
        raise ValueError('Unsupported Tcl path character: ' + value)
    return '{' + value + '}'


def collect_hardware(root, tree):
    files = {root / (NAME + '.xml')}
    for node in tree.iter():
        if local(node.tag) in ('design_file', 'sdc_file', 'peri_file', 'default_peri_file') and node.get('name'):
            files.add(contained(root, root / node.get('name')))
        if local(node.tag) == 'ip' and node.get('path'):
            files.add(contained(root, root / node.get('path')))
    for directory in [root / 'rtl', root / 'ip/riscv_soc', root / 'ip/axi_interconnect_3to1']:
        if not directory.is_dir():
            raise FileNotFoundError(directory)
        # Include source and initialization dependencies, never generated build/cache outputs.
        # The original recipes use generated top-level IP files. Other device
        # example trees are not inputs; referenced top_soc.v is already in XML.
        iterator = directory.rglob('*') if directory.name == 'rtl' else directory.iterdir()
        for p in iterator:
            if p.is_file() and p.suffix.lower() in ('.v', '.sv', '.vh', '.vhd', '.bin', '.mem', '.hex', '.json', '.xml'):
                if not any(part.startswith(('build_', '.')) or part in ('outflow', 'work_syn', 'work_pnr', '__pycache__') for part in p.relative_to(directory).parts):
                    files.add(contained(root, p))
    for p in root.iterdir():
        if p.is_file() and p.suffix.lower() in ('.vh', '.mem', '.hex', '.bin'):
            files.add(contained(root, p))
    if (root / 'package_settings.xml').is_file():
        files.add(root / 'package_settings.xml')
    for p in files:
        if not p.is_file():
            raise FileNotFoundError(p)
    return files


def stage_project(root, source, tree, profile):
    # Basic's live XML predates its frozen scope build options. Apply only the same
    # controls used by hw_05/build_hardware.py; game XML already carries its recipe.
    for section in list(tree.getroot()):
        tag = local(section.tag)
        if profile == 'basic' and tag == 'debugger':
            tree.getroot().remove(section)
            continue
        for node in section:
            if local(node.tag) == 'param':
                key = node.get('name')
                if profile == 'basic' and key == 'max_threads': node.set('value', '2')
                if profile == 'basic' and key == 'enable-mark-debug': node.set('value', '0')
                if tag == 'place_and_route' and key == 'seed': node.set('value', '5')
                if tag == 'place_and_route' and key == 'optimization_level': node.set('value', 'TIMING_1')
    models = [n.get('name') for n in tree.iter() if local(n.tag) == 'timing_model']
    if models != ['I3']:
        raise ValueError('Expected the frozen I3 timing model, got ' + repr(models))
    for node in tree.iter():
        if local(node.tag) in ('design_file', 'sdc_file', 'peri_file', 'default_peri_file') and node.get('name'):
            rel = contained(root, root / node.get('name')).relative_to(root)
            node.set('name', (source / rel).as_posix())
        elif local(node.tag) == 'ip' and node.get('path'):
            rel = contained(root, root / node.get('path')).relative_to(root)
            node.set('path', (source / rel).as_posix())
        elif local(node.tag) == 'param' and node.get('name') == 'include':
            rel = contained(root, root / node.get('value')).relative_to(root)
            node.set('value', (source / rel).as_posix())
    tree.write(source / (NAME + '.xml'), encoding='utf-8', xml_declaration=True)


def run_sta(build, source, project, efinity, env, profile):
    out, work = build / 'map', build / 'sta_work'
    shutil.copytree(build / 'pnr_work', work)
    shutil.copyfile(out / (NAME + '.troutingtraces'), work / (NAME + '.troutingtraces'))
    tree = ET.parse(project)
    for node in tree.iter():
        if local(node.tag) == 'param' and node.get('name') == 'work_dir':
            node.set('value', work.as_posix())
    query_xml = build / 'sta_query.xml'
    tree.write(query_xml, encoding='utf-8', xml_declaration=True)
    report_dir = build / 'sta_reports'; report_dir.mkdir()
    queries = {'negative_setup': '-setup -less_than_slack 0 -npaths 100000 -nworst 1',
               'negative_hold': '-hold -less_than_slack 0 -npaths 100000 -nworst 1'}
    for a, b in [('core_clk', 'core_clk'), ('hdmi_tx_slow_clk', 'hdmi_tx_slow_clk'),
                 ('core_clk', 'hdmi_tx_slow_clk'), ('hdmi_tx_slow_clk', 'core_clk'), ('tx_cal_clk', 'tx_cal_clk')]:
        for kind in ('setup', 'hold'):
            queries[kind + '_' + a + '__' + b] = '-' + kind + ' -from_clock ' + a + ' -to_clock ' + b + ' -npaths 64 -nworst 1'
    if profile == 'basic':
        queries.update(scope_inputs='-to [get_pins {u_scope_measure/*~FF|D}] -npaths 20',
                       scope_outputs='-from [get_pins {u_scope_measure/*~FF|CLK}] -npaths 20',
                       scope_pin='-to [get_ports {scope_blockcopy_frame}] -npaths 5')
    lines = ['reset_timing', 'read_sdc ' + tcl_path(source / (NAME + '.sdc')),
             'puts "EXPLICIT_SDC_LOADED MODEL=[get_timing_model]"',
             'report_clocks -file ' + tcl_path(report_dir / 'clocks.rpt'),
             'check_timing -verbose -file ' + tcl_path(report_dir / 'coverage.rpt')]
    for label, query in queries.items():
        lines.append('report_timing ' + query + ' -file ' + tcl_path(report_dir / (label + '.rpt')))
    lines += ['puts "STA_QUERIES_COMPLETE"', 'exit']
    (out / 'delivery_sta.tcl').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    result = run_logged([str(efinity / 'bin/efx_sta.exe'), '--prj', str(query_xml), '--tclsh', 'delivery_sta.tcl'], out, env, build / 'sta.log', timeout=1800)
    log = (build / 'sta.log').read_text(encoding='utf-8', errors='replace')
    paths = {}
    for label in queries:
        path = report_dir / (label + '.rpt')
        body = path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''
        slacks = [float(v) for v in re.findall(r'Slack\s*:\s*(-?[\d.]+) ns', body)]
        paths[label] = dict(reported_paths=len(slacks), worst_slack_ns=min(slacks) if slacks else None,
                            sha256=sha256(path) if path.exists() else None)
    completed = (result['returncode'] == 0 and 'MODEL=I3' in log and 'STA_QUERIES_COMPLETE' in log
                 and all(p['sha256'] for p in paths.values()) and not re.search(r'ERROR\s*:', log))
    negative = any(paths[k]['reported_paths'] for k in ('negative_setup', 'negative_hold'))
    result.update(timing_model='I3', queries_completed=completed, paths=paths,
                  overall='ERROR' if not completed else 'FAIL' if negative else 'PASS',
                  note='Successful tool exit or bit generation is not a timing pass. Retain all raw reports.')
    write_json(build / 'timing_summary.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--profile', choices=['basic', 'game'], required=True)
    parser.add_argument('--output', type=Path, required=True, help='New isolated build directory; never overwritten')
    parser.add_argument('--efinity-root', type=Path, default=os.environ.get('EFINITY_HOME'))
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    if args.efinity_root is None: parser.error('Set EFINITY_HOME or --efinity-root to Efinity 2026.1')
    root, build, efinity = args.project_root.resolve(), args.output.resolve(), args.efinity_root.resolve()
    if build.exists(): raise FileExistsError('Fresh FPGA output required: ' + str(build))
    source = build / 'source'
    tree = ET.parse(root / (NAME + '.xml'))
    files = collect_hardware(root, tree)
    before = hashes(root, files)
    for folder in [source, build / 'map', build / 'map_work', build / 'pnr_work']:
        folder.mkdir(parents=True, exist_ok=True)
    for original in files:
        target = source / original.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    stage_project(root, source, tree, args.profile)
    project = source / (NAME + '.xml')
    replacements = {'${EFINITY_ROOT}': efinity.as_posix(), '${BUILD}': build.as_posix(),
                    '${SOURCE}': source.as_posix(), '${PROJECT}': project.as_posix()}
    def expand(text):
        for old, new in replacements.items(): text = text.replace(old, new)
        if '${' in text: raise ValueError('Unresolved recipe variable: ' + text)
        return text
    recipes = json.loads((Path(__file__).with_name('hardware_recipes.json')).read_text(encoding='utf-8'))
    commands = [{k: [expand(x) for x in v] if isinstance(v, list) else expand(v) for k, v in row.items()}
                for row in recipes[args.profile]]
    write_json(build / 'commands.json', commands)
    write_json(build / 'source_sha256_before.json', before)
    staged = hashes(source, [source / key for key in before])
    write_json(build / 'snapshot_sha256_before.json', staged)
    state = dict(profile=args.profile, seed=5, timing_model='I3', optimization='TIMING_1',
                 device_operation=False, status='PLAN_ONLY', phases=[], timing='NOT_RUN',
                 project_xml_changes='Source paths moved into isolated snapshot; basic frozen build options applied',
                 recipe_sha256=sha256(Path(__file__).with_name('hardware_recipes.json')))
    write_json(build / 'build_record.json', state)
    if not args.execute:
        print(str(build / 'build_record.json')); return 0
    env = efinity_env(efinity)
    try:
        for row in commands:
            if not Path(row['args'][0]).is_file(): raise FileNotFoundError(row['args'][0])
            result = run_logged(row['args'], row['cwd'], env, build / (row['stage'] + '.log'), timeout=14400)
            state['phases'].append(dict(stage=row['stage'], **result))
            write_json(build / 'build_record.json', state)
            if result['returncode']: raise RuntimeError(row['stage'] + ' failed')
        timing = run_sta(build, source, project, efinity, env, args.profile)
        state['timing'] = timing['overall']
        bit = build / 'map' / (NAME + '.bit')
        state['bit'] = dict(path=str(bit), bytes=bit.stat().st_size, sha256=sha256(bit))
        state['status'] = 'BUILD_DONE'
    except Exception as exc:
        state.update(status='FAIL', error=str(exc))
    finally:
        after = hashes(root, collect_hardware(root, ET.parse(root / (NAME + '.xml'))))
        snapshot_after = hashes(source, [source / key for key in before])
        write_json(build / 'source_sha256_after.json', after)
        write_json(build / 'snapshot_sha256_after.json', snapshot_after)
        state['source_drift'] = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
        state['snapshot_drift'] = sorted(k for k in set(staged) | set(snapshot_after) if staged.get(k) != snapshot_after.get(k))
        if state['source_drift'] or state['snapshot_drift']: state['status'] = 'FAIL'
        write_json(build / 'build_record.json', state)
    print(str(build / 'build_record.json'))
    if state['status'] != 'BUILD_DONE': return 1
    return 0 if state['timing'] == 'PASS' else 2


if __name__ == '__main__':
    raise SystemExit(main())
