"""Writes the Android launcher's translated text from localization/NebulaText.tsv.

    python tools/android_text.py            writes platform/android/app/src/main/res/values*/nebula_text.xml
    python tools/android_text.py --check    exits 1 when a file does not match the table, a text has no row, or
                                            Updates.kt's release key is not installer/release-public-key.xml's

TEXT names each string resource and the English cell of its row (rows are found by their English text, as the Windows
launcher's NebulaText.Of finds them; a third cell names the row by id where rows share the English text). An empty cell takes its sister column's (EuFrench for UsFrench and back), then
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
    # Settings, Performance: the benchmark (perf/bench's contract) and the settings it recommends.
    ('nebula_performance', 'Performance'),
    ('nebula_benchmark', 'Benchmark'),
    ('nebula_benchmark_about', 'Runs real stages to measure this device, then recommends settings. It takes a few minutes. Your saves and settings are not changed.'),
    ('nebula_benchmark_recommended', 'Recommended settings'),
    ('nebula_benchmark_current', 'Current'),
    ('nebula_benchmark_recommended_value', 'Recommended'),
    ('nebula_benchmark_fits', 'These settings keep 60 frames per second on this device.'),
    ('nebula_benchmark_below', 'Even the lowest settings may not keep 60 frames per second on this device.'),
    ('nebula_benchmark_cpu', 'The processor limits the frame rate here: lower graphics settings will not raise it.'),
    ('nebula_benchmark_unfinished', 'The benchmark did not finish.'),
    ('nebula_apply', 'Apply'),
    ('nebula_settings_not_saved', 'Settings could not be saved: {0}'),
    ('nebula_setting_internal_resolution', 'Internal resolution'),
    ('nebula_setting_ambient_occlusion', 'Ambient occlusion'),
    ('nebula_setting_lighting', 'Lighting'),
    ('nebula_setting_aa', 'Anti-aliasing'),
    ('nebula_setting_aa_quality', 'Anti-aliasing quality'),
    ('nebula_setting_bloom', 'Bloom'),
    ('nebula_on', 'On'),
    ('nebula_off', 'Off'),
    ('nebula_match_display', 'Match display'),
    ('nebula_lighting_enhanced', 'Enhanced'),
    ('nebula_lighting_per_pixel', 'Per pixel'),
    ('nebula_aa_auto', 'Auto (SMAA unless supersampled)'),
    ('nebula_quality_low', 'Low'),
    ('nebula_quality_medium', 'Medium'),
    ('nebula_quality_high', 'High'),
    ('nebula_quality_ultra', 'Ultra'),
    # Settings: the screen's own texts.
    ('nebula_settings', 'Settings', 'S.SETTINGS'),
    ('nebula_storage', 'Storage'),
    ('nebula_set_up_again', 'Set up again'),
    ('nebula_set_up_again_detail', 'From another disc image'),
    ('nebula_remove_game', 'Remove the game'),
    ('nebula_remove_game_detail', '{0} · your saves ({1}) are kept'),
    ('nebula_saves', 'Saves'),
    ('nebula_export_saves', 'Export saves'),
    ('nebula_working', 'Working...'),
    ('nebula_export_saves_detail', 'Every save file and your Miis as one .zip, to keep or move to another device'),
    ('nebula_import_saves', 'Import saves'),
    ('nebula_import_saves_detail', 'A .zip from Export, a Nebula.sav, or a GameData.bin from a Wii, Dolphin or the Switch release'),
    ('nebula_miis', 'Miis'),
    ('nebula_import_miis', 'Import Miis'),
    ('nebula_import_miis_detail', 'From a Wii, DS, 3DS, Wii U or Switch, Mii Studio or Mii Creator, for the Mii icons'),
    ('nebula_mii_count', '{0} of 100 now'),
    ('nebula_controllers', 'Controllers'),
    ('nebula_motion_test', 'Motion test'),
    ('nebula_motion_test_detail', "Each controller's gyroscope and accelerometer as the game reads them, live"),
    ('nebula_mods', 'Mods'),
    ('nebula_import_mod', 'Import a mod or texture pack'),
    ('nebula_importing', 'Importing... {0}%'),
    ('nebula_import_mod_detail', "A .luma, .zip, .7z or .rar file. It's on from the next start"),
    ('nebula_help', 'Help'),
    ('nebula_record_diagnostics', 'Record diagnostics'),
    ('nebula_record_diagnostics_detail', 'The next run writes frame times and pointer delay to its log. Share the log after it.'),
    ('nebula_share_log', "Share the game's log"),
    ('nebula_share_log_detail', "The last run's, to report a problem"),
    ('nebula_replace_saves', 'Replace your saves?'),
    ('nebula_replace_saves_detail', 'The saves in the file take the place of the same saves here. The saves you have now are backed up first.'),
    ('nebula_choose_file', 'Choose a file'),
    ('nebula_cancel', 'Cancel'),
    ('nebula_remove_game_question', 'Remove the game?'),
    ('nebula_remove_game_question_detail', 'Your saves stay. To play again, set the game up from your disc image.'),
    ('nebula_remove', 'Remove'),
    ('nebula_keep', 'Keep', 'S.KEEP'),
    # Welcome, setup and home screens.
    ('nebula_drop_to_import', 'Drop to import'),
    ('nebula_drop_detail', 'A disc image, saves, Mii files or mods: each goes where it belongs.'),
    ('nebula_drop_detail_setup', 'Your disc image sets the game up.'),
    ('nebula_step_choose', 'Choose your disc image'),
    ('nebula_step_choose_detail', 'The copy you made of your own Wii disc: ISO, RVZ, WIA, WBFS, CISO or GCZ.'),
    ('nebula_step_setup', 'Nebula sets the game up'),
    ('nebula_step_setup_detail', 'It copies the game into its own storage (about 3.9 GB). The disc image is only read.'),
    ('nebula_play', 'Play'),
    ('nebula_step_play_detail', 'With the touch controls, or a controller.'),
    ('nebula_choose_disc', 'Choose disc image'),
    ('nebula_reading_disc', 'Reading the disc image…'),
    ('nebula_free_space', 'Free up some space on this device, then try again.'),
    ('nebula_set_up', 'Set up'),
    ('nebula_choose_another', 'Choose another'),
    ('nebula_disc_unusable', "This disc image can't be used"),
    ('nebula_disc_unusable_detail', 'It is not Super Mario Galaxy for North America, Europe or Japan.'),
    ('nebula_setting_up', 'Setting up Super Mario Galaxy'),
    ('nebula_setting_up_detail', 'You can leave Nebula meanwhile: setup carries on, with its progress in the notifications.'),
    ('nebula_ready_to_play', 'Ready to play'),
    ('nebula_ready_to_play_detail', 'Super Mario Galaxy is set up. You can delete the disc image if you no longer need it.'),
    ('nebula_done', 'Done'),
    ('nebula_setup_failed', "Setup didn't finish"),
    ('nebula_try_again', 'Try again'),
    ('nebula_setup_cancelled', 'Setup cancelled'),
    ('nebula_setup_cancelled_detail', 'Nothing was kept. You can start again whenever you like.'),
    ('nebula_back', 'Back', 'SettingsBack'),
    ('nebula_ready', 'Ready'),
    ('nebula_settings_detail', 'Touch controls, storage'),
    ('nebula_about', 'About Nebula'),
    ('nebula_touch_controls', 'Touch controls'),
    ('nebula_welcome_outdated', 'This update reads the game in a new form: set it up again from your disc image. Your saves, Miis and settings stay.'),
    ('nebula_welcome', 'Super Mario Galaxy, running natively on this device.'),
    ('nebula_needs_space', 'Needs {0} · {1} available'),
    ('nebula_version_licenses', 'Version {0}, licenses'),
    # Result dialogs and About.
    ('nebula_ok', 'OK'),
    ('nebula_share_the_log', 'Share the log'),
    ('nebula_mod_already', '{0} is already in your mods'),
    ('nebula_mod_ready', '{0} is ready'),
    ('nebula_mod_failed', 'That file could not be imported'),
    ('nebula_mod_already_detail', "Nothing was added. It is under Mods in the game's settings."),
    ('nebula_mod_ready_detail', "It's on from the next start. Mods in the game's settings turns it off or moves it."),
    ('nebula_about_title', 'About'),
    ('nebula_version', 'Version {0}'),
    ('nebula_about_text', "Nebula runs Super Mario Galaxy natively: the game's code is Petari's decompilation, compiled for this device, drawing with Vulkan. The game's files come from your own disc. Nebula is not affiliated with or endorsed by Nintendo."),
    ('nebula_licenses', 'Open source licenses'),
    # LauncherModel's results and the game's exit (app.getString).
    ('nebula_file_unreadable', 'That file could not be read'),
    ('nebula_files_failed', 'Those files could not be imported'),
    ('nebula_mods_ready', '{0} mods are ready'),
    ('nebula_mods_some_ready', '{0} of {1} mods are ready'),
    ('nebula_mods_turn_on', "Turn them on under Mods in the game's settings."),
    ('nebula_set_up_first', 'Set up the game first'),
    ('nebula_drop_disc_to_start', 'Drop your disc image here to start.'),
    ('nebula_miis_failed', 'The Miis could not be imported'),
    ('nebula_unknown_error', 'Unknown error.'),
    ('nebula_saves_failed', 'The saves could not be moved'),
    ('nebula_disc_unreadable', 'The disc image could not be read: {0}'),
    ('nebula_game_could_not_continue', 'The game could not continue'),
    ('nebula_game_crashed', 'The game stopped unexpectedly. The log of that run can help find out why.'),
    ('nebula_game_not_responding', 'The game stopped responding and was closed.'),
    ('nebula_game_low_memory', 'Android closed the game to free memory. Closing other apps before playing can help.'),
    ('nebula_game_closed', 'The game closed'),
    # Mii import (MiiImport.kt), the setup notification (ImportService.kt), the log share (LauncherActivity.kt).
    ('nebula_miis_unkept', 'The Miis could not be kept'),
    ('nebula_mii_folder_unwritable', "Nebula's Mii folder could not be written."),
    ('nebula_no_miis_file', 'No Miis in that file'),
    ('nebula_no_miis_files', 'No Miis in those files'),
    ('nebula_choose_mii_files', 'Choose Mii files from a Wii, DS, 3DS, Wii U or Switch, or from Mii Studio or Mii Creator.'),
    ('nebula_one_mii_found', '1 Mii found.'),
    ('nebula_miis_found', '{0} Miis found.'),
    ('nebula_one_mii_converted', "1 from a newer console takes the Wii's nearest parts and colours."),
    ('nebula_miis_converted', "{0} from newer consoles take the Wii's nearest parts and colours."),
    ('nebula_not_mii_data', 'Not Mii data: {0}.'),
    ('nebula_not_kept', 'Could not be kept: {0}.'),
    ('nebula_miis_join_next', 'They join your Miis when the game starts again.'),
    ('nebula_miis_join', 'They join your Miis when the game starts. Miis it already has are not added twice.'),
    ('nebula_miis_imported', 'Miis imported'),
    ('nebula_import_progress', '{0}% · {1} of {2}'),
    ('nebula_reading_disc_plain', 'Reading the disc image'),
    ('nebula_share_game_log', 'Share the game log'),
    ('nebula_log_subject', 'Nebula game log'),
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
    with open(TABLE, encoding='utf-8', newline='') as f:
        lines = f.read().split('\r\n')
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
