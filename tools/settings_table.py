"""Checks runtime/src/ui/settings_table.tsv, the one list of Nebula's settings, against the code that reads and offers them.

    python tools/settings_table.py --check    exits 1, listing each difference, when the table and the code disagree

What it compares:
  - the engine: every key render_config.cpp, runtime_settings.cpp and input_bindings.cpp read has a row, and every row of
    the files config and runtime names a key they read; a row's default equals its member's initialiser where that is a
    plain number or bool (render_config.h, runtime_settings.h);
  - the Windows launcher (installer/src/Launcher): the keys it writes are exactly the rows marked y in the windows column;
  - the Android launcher (platform/android/app): the keys of config.ini and runtime_settings.ini it writes are the rows
    marked y in the android column;
  - the game's menu (runtime/src/ui): every row marked y in the game column has its member changed there;
  - the text: every label of a row the Windows launcher offers has a row in localization/NebulaText.tsv with every
    language filled (a Us/Eu column may leave its sister's).
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, 'runtime', 'src', 'ui', 'settings_table.tsv')
COLUMNS = ['key', 'file', 'field', 'type', 'values', 'default', 'page', 'windows', 'android', 'game', 'macos', 'capability', 'label']
MARKS = {'y', 'n', 's', '-'}


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def load_table(errors):
    rows = []
    with open(TABLE, encoding='utf-8') as f:
        lines = [line.rstrip('\n') for line in f if line.strip() and not line.startswith('#')]
    if not lines or lines[0].split('\t') != COLUMNS:
        errors.append('settings_table.tsv: the header is not ' + '\\t'.join(COLUMNS))
        return rows
    seen = set()
    for number, line in enumerate(lines[1:], 2):
        cells = line.split('\t')
        if len(cells) != len(COLUMNS):
            errors.append('settings_table.tsv row %d: %d cells, not %d' % (number, len(cells), len(COLUMNS)))
            continue
        row = dict(zip(COLUMNS, cells))
        if (row['file'], row['key']) in seen:
            errors.append('settings_table.tsv: %s is listed twice' % row['key'])
        seen.add((row['file'], row['key']))
        for column in ('windows', 'android', 'game', 'macos'):
            if row[column] not in MARKS:
                errors.append('settings_table.tsv %s: %s is %r, not one of y n s -' % (row['key'], column, row[column]))
        if row['page'] != '-' and row['label'] == '-' and 'y' in (row['windows'], row['android']):
            errors.append('settings_table.tsv %s: offered by a launcher without a label' % row['key'])
        rows.append(row)
    return rows


def engine_keys():
    keys = {'config': set(), 'runtime': set()}
    render = strip_comments(read('runtime/src/render/render_config.cpp'))
    keys['config'] |= set(re.findall(r'strcmp\(key, "([a-z0-9_]+)"\)', render))
    parts = re.search(r'kRtPartKeys\[\] = \{([^}]*)\}', render)
    keys['config'] |= set(re.findall(r'"([a-z_]+)"', parts.group(1)))
    runtime = strip_comments(read('runtime/src/runtime_settings.cpp'))
    keys['runtime'] |= set(re.findall(r'strcmp\(key, "([a-z0-9_]+)"\)', runtime))
    keys['runtime'] |= set(re.findall(r'key_text == "([a-z_]+)"', runtime))
    bindings = strip_comments(read('runtime/src/input_bindings.cpp'))
    body = bindings[bindings.index('bool apply_controls_setting('):bindings.index('ControlsConfig sanitize_controls_config(')]
    keys['runtime'] |= {'tune.*.' + name for name in re.findall(r'name == "([a-z_]+)"', body)}
    keys['runtime'].add('bind.*.*')
    return keys


def member_defaults():
    """Plain number and bool initialisers of the settings structs: member -> set of values (both of an #if)."""
    defaults = {}
    for path in ('runtime/include/galaxy/render/render_config.h', 'runtime/include/galaxy/runtime_settings.h'):
        for member, value in re.findall(r'^\s*(?:unsigned|bool|float|int)\s+(\w+)\s*=\s*([^;]+);', read(path), re.M):
            value = value.strip()
            value = re.sub(r'^static_cast<float>\(kGameStepsPerSecond\)$', '60', value)
            value = {'true': '1', 'false': '0'}.get(value, value)
            value = re.sub(r'^(\d+(?:\.\d+)?)(?:u|f|\.0f)$', r'\1', value)
            value = re.sub(r'\.0$', '', value)
            if re.fullmatch(r'\d+(\.\d+)?', value):
                defaults.setdefault(member, set()).add(value)
    return defaults


def windows_keys():
    keys = set()
    for path in glob.glob(os.path.join(ROOT, 'installer', 'src', 'Launcher', '*.cs')):
        if os.path.basename(path) == 'ModsPage.cs':
            continue
        text = strip_comments(open(path, encoding='utf-8').read())
        keys |= set(re.findall(r'KeyValuePair<string, string>\("([a-z0-9_]+)"', text))
        keys |= {'tune.*.' + name for name in re.findall(r'KeyValuePair<string, string>\("tune\." \+ \w+ \+ "([a-z_]+)"', text)}
        if re.search(r'KeyValuePair<string, string>\("bind\." \+', text):
            keys.add('bind.*.*')
        for array in re.findall(r'string\[\] (?:Keys|\w+Keys) = \{([^}]*)\}', text):
            keys |= {key for key in re.findall(r'"([a-z0-9_]+)"', array) if '_' in key}
    return keys


def android_keys():
    keys = set()
    for path in glob.glob(os.path.join(ROOT, 'platform', 'android', 'app', 'src', 'main', '**', '*.kt'), recursive=True):
        text = strip_comments(open(path, encoding='utf-8').read())
        keys |= set(re.findall(r'values\["([a-z0-9_]+)"\]\s*=', text))
    return keys


def game_text():
    text = ''
    for path in glob.glob(os.path.join(ROOT, 'runtime', 'src', 'ui', '*.cpp')):
        if not path.endswith('_tests.cpp'):
            text += strip_comments(open(path, encoding='utf-8').read())
    return text


def text_rows():
    rows = {}
    with open(os.path.join(ROOT, 'localization', 'NebulaText.tsv'), encoding='utf-8') as f:
        header = f.readline().rstrip('\n').split('\t')
        for line in f:
            cells = line.rstrip('\n').split('\t')
            cells += [''] * (len(header) - len(cells))
            english = cells[header.index('UsEnglish')]
            rows.setdefault(english, dict(zip(header, cells)))
    return header, rows


def sister(column):
    if column.startswith('Us'):
        return 'Eu' + column[2:]
    if column.startswith('Eu'):
        return 'Us' + column[2:]
    return None


def check():
    errors = []
    rows = load_table(errors)
    if not rows:
        return errors
    by_file = {}
    for row in rows:
        by_file.setdefault(row['file'], {})[row['key']] = row

    engine = engine_keys()
    for file in ('config', 'runtime'):
        listed = set(by_file.get(file, {}))
        for key in sorted(engine[file] - listed):
            errors.append('engine reads %s key %s: no row' % (file, key))
        for key in sorted(listed - engine[file]):
            errors.append('row %s (%s): the engine reads no such key' % (key, file))

    defaults = member_defaults()
    for row in rows:
        if row['file'] in ('config', 'runtime') and row['field'] in defaults and re.fullmatch(r'[\d.;=a-z]+', row['default']):
            want = {part.split('=')[-1] for part in row['default'].split(';')}
            if want != defaults[row['field']]:
                errors.append('row %s: default %s, the engine\'s %s' % (row['key'], row['default'], '/'.join(sorted(defaults[row['field']]))))

    written = windows_keys()
    offered = {row['key'] for row in rows if row['windows'] == 'y'}
    engine_all = engine['config'] | engine['runtime']
    for key in sorted(written - engine_all):
        errors.append('Windows launcher writes %s: the engine reads no such key' % key)
    for key in sorted((written & engine_all) - offered):
        errors.append('Windows launcher writes %s: its row does not say windows y' % key)
    for key in sorted(offered - written):
        errors.append('row %s says windows y: the Windows launcher does not write it' % key)

    written = android_keys()
    offered = {row['key'] for row in rows if row['android'] == 'y' and row['file'] != 'android'}
    for key in sorted(written - offered):
        errors.append('Android launcher writes %s: its row does not say android y' % key)
    for key in sorted(offered - written):
        errors.append('row %s says android y: the Android launcher does not write it' % key)

    game = game_text()
    for row in rows:
        if row['game'] != 'y' or row['file'] == 'android' or row['field'] == '-':
            continue
        if not re.search(r'\b(?:draft|render|runtime|settings|tuning|audio)\.(?:audio\.)?%s\b' % re.escape(row['field']), game):
            errors.append('row %s says game y: runtime/src/ui never touches %s' % (row['key'], row['field']))

    header, texts = text_rows()
    languages = [column for column in header if column not in ('id', 'UsEnglish')]
    for row in rows:
        if row['windows'] != 'y' or row['label'] == '-':
            continue
        text = texts.get(row['label'])
        if text is None:
            errors.append('row %s: label %r has no row in NebulaText.tsv' % (row['key'], row['label']))
            continue
        for column in languages:
            if not text.get(column) and not text.get(sister(column) or '', ''):
                errors.append('row %s: label %r has no %s text' % (row['key'], row['label'], column))
    return errors


def main():
    if sys.argv[1:] != ['--check']:
        print(__doc__)
        return 2
    errors = check()
    for error in errors:
        print(error)
    if errors:
        print('settings_table.py: %d differences' % len(errors))
        return 1
    print('settings_table.py: the table matches the code')
    return 0


if __name__ == '__main__':
    sys.exit(main())
