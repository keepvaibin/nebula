"""Writes the Android app's translated text from localization/NebulaText.tsv.

    python tools/android_text.py            writes platform/android/app/src/main/res/values*/nebula_text.xml
    python tools/android_text.py --check    exits 1 when a file does not match the table, a text has no row, or
                                            Updates.kt's release key is not installer/release-public-key.xml's

TEXT names each string resource and the English cell of its row (rows are found by their English text, as the Windows
app's NebulaText.Of finds them; a third cell names the row by id where rows share the English text). An empty cell takes its sister column's (EuFrench for UsFrench and back), then
English. {0}..{3} become %1$s..%4$s. A language whose every string equals English gets no file.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, 'localization', 'NebulaText.tsv')
RES = os.path.join(ROOT, 'platform', 'android', 'app', 'src', 'main', 'res')
UPDATES_KT = os.path.join(ROOT, 'platform', 'android', 'app', 'src', 'main', 'kotlin', 'dev', 'nebula', 'galaxy', 'Updates.kt')
KEY_XML = os.path.join(ROOT, 'installer', 'release-public-key.xml')
NAME = 'nebula_text.xml'

TEXT = [
    ('nebula_update_available', 'Nebula {0} is available'),
    ('nebula_update_size', 'Download: {0}'),
    ('nebula_update_update', 'Update'),
    ('nebula_update_not_now', 'Not now'),
    ('nebula_update_skip', 'Skip this version'),
    ('nebula_update_downloading', 'Downloading Nebula {0}'),
    ('nebula_update_failed', "Couldn't download the update."),
    ('nebula_update_rejected', "The update didn't pass its check and was deleted."),
    ('nebula_update_no_room', 'Not enough free space for the update.'),
    ('nebula_update_declined', 'The update was not installed. Nebula offers it again at the next start.'),
    ('nebula_update_refused', 'Android refused the update: it does not match the installed Nebula.'),
    ('nebula_update_blocked', 'Android blocked the update.'),
    ('nebula_update_install_failed', 'The update could not be installed.'),
    ('nebula_update_allow', 'Allow Nebula to install apps, then press Update again.'),
    ('nebula_update_open_settings', 'Open settings'),
    ('nebula_update_cancel', 'Cancel'),
    ('nebula_updates', 'Updates'),
    ('nebula_updates_at_start', 'Check for updates at start'),
    # Settings: the screen's own texts.
    # Welcome, setup and home screens.
    # Result dialogs and About.
    # LauncherModel's results and the game's exit (app.getString).
    # Mii import (MiiImport.kt), the setup notification (ImportService.kt), the log share (LauncherActivity.kt).
]


# The table's columns as Android's resource qualifiers (UsEnglish is the default, values/).
FOLDERS = [
    ('UsEnglish', 'values'),
    ('EuEnglish', 'values-en-rGB'),
    ('UsFrench', 'values-fr-rCA'),
    ('EuFrench', 'values-fr'),
    ('UsSpanish', 'values-b+es+419'),
    ('EuSpanish', 'values-es'),
    ('EuGerman', 'values-de'),
    ('EuItalian', 'values-it'),
    ('EuDutch', 'values-nl'),
    ('UsPortuguese', 'values-pt'),
    ('JpJapanese', 'values-ja'),
    ('KrKorean', 'values-ko'),
    ('CnSimpChinese', 'values-zh'),
    ('TwTradChinese', 'values-b+zh+Hant'),
]


def sister(column):
    if column.startswith('Us'):
        return 'Eu' + column[2:]
    if column.startswith('Eu'):
        return 'Us' + column[2:]
    return None


def unescape(cell):
    return cell.replace('\\n', '\n').replace('\\\\', '\\')


def rows():
    # Either line ending: the table is CRLF in a checkout that converts and LF in one that does not; a cell's own line
    # breaks are written \n, never raw.
    with open(TABLE, encoding='utf-8', newline='') as f:
        lines = f.read().replace('\r\n', '\n').split('\n')
    header = lines[0].split('\t')
    found = {}
    for line in lines[1:]:
        if not line.strip():
            continue
        cells = line.split('\t')
        cells += [''] * (len(header) - len(cells))
        row = dict(zip(header, cells))
        found.setdefault(cells[1], row)
        found['id:' + cells[0]] = row
    return found


def lookup(entry):
    """The table key of a TEXT entry: its row id when it names one, else its English text."""
    return 'id:' + entry[2] if len(entry) > 2 else entry[1]


def android(text):
    """A string resource's text: XML and Android's own escapes, {n} as positional %s."""
    out = text.replace('\\', '\\\\').replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    out = out.replace("'", "\\'").replace('"', '\\"').replace('\n', '\\n')
    if out[:1] in ('@', '?'):
        out = '\\' + out
    out = out.replace('%', '%%') if re.search(r'\{\d\}', text) else out
    return re.sub(r'\{(\d)\}', lambda m: '%%%d$s' % (int(m.group(1)) + 1), out)


def generate():
    table = rows()
    errors = []
    files = {}
    english = {}
    for entry in TEXT:
        if lookup(entry) not in table or table[lookup(entry)]['UsEnglish'] != entry[1]:
            errors.append('no NebulaText row for %r (%s)' % (entry[1], entry[0]))
    if errors:
        return files, errors
    for column, folder in FOLDERS:
        values = []
        for entry in TEXT:
            name, text = entry[0], entry[1]
            row = table[lookup(entry)]
            cell = row.get(column) or row.get(sister(column) or '', '') or row['UsEnglish']
            values.append((name, unescape(cell), re.search(r'\{\d\}', text) is None and '%' in cell))
        if column == 'UsEnglish':
            english = {name: cell for name, cell, _ in values}
        elif all(english[name] == cell for name, cell, _ in values):
            continue
        out = ['<?xml version="1.0" encoding="utf-8"?>',
               '<!-- Generated by tools/android_text.py from localization/NebulaText.tsv: edit the table and run the script. -->',
               '<resources>']
        for name, cell, literal in values:
            attribute = ' formatted="false"' if literal else ''
            out.append('    <string name="%s"%s>%s</string>' % (name, attribute, android(cell)))
        out.append('</resources>')
        files[os.path.join(RES, folder, NAME)] = '\n'.join(out) + '\n'
    return files, errors


def key_errors():
    xml = open(KEY_XML, encoding='utf-8').read()
    modulus = re.search(r'<Modulus>([^<]+)</Modulus>', xml).group(1)
    exponent = re.search(r'<Exponent>([^<]+)</Exponent>', xml).group(1)
    if not os.path.exists(UPDATES_KT):
        return ['Updates.kt is missing']
    kotlin = open(UPDATES_KT, encoding='utf-8').read()
    errors = []
    if 'RELEASE_MODULUS = "%s"' % modulus not in kotlin:
        errors.append("Updates.kt RELEASE_MODULUS is not release-public-key.xml's modulus")
    if 'RELEASE_EXPONENT = "%s"' % exponent not in kotlin:
        errors.append("Updates.kt RELEASE_EXPONENT is not release-public-key.xml's exponent")
    return errors


def main():
    check = sys.argv[1:] == ['--check']
    if sys.argv[1:] not in ([], ['--check']):
        print(__doc__)
        return 2
    files, errors = generate()
    errors += key_errors()
    for path, text in sorted(files.items()):
        # A checkout may turn the line ends into CRLF: the text is what is compared.
        current = open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n') if os.path.exists(path) else None
        if current == text:
            continue
        if check:
            errors.append('%s is out of date: run python tools/android_text.py' % os.path.relpath(path, ROOT))
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8', newline='') as f:
                f.write(text)
    for folder in os.listdir(RES):
        stale = os.path.join(RES, folder, NAME)
        if os.path.exists(stale) and stale not in files:
            if check:
                errors.append('%s is no longer generated: run python tools/android_text.py' % os.path.relpath(stale, ROOT))
            else:
                os.remove(stale)
    for error in errors:
        print(error)
    if errors:
        print('android_text.py: %d problems' % len(errors))
        return 1
    print('android_text.py: %d files match the table' % len(files) if check else 'android_text.py: %d files written' % len(files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
