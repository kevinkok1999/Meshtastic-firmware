"""PlatformIO post-script: capture actual flash inputs without touching hardware.

Loaded only by the isolated DEV artifact workflow after the regular project
post-scripts. No upload action and no changes to board flags or firmware source.
"""
import json
from pathlib import Path

Import('env')  # pylint: disable=undefined-variable

platform = env.PioPlatform()
board = env.BoardConfig()
build_dir = Path(env.subst('$BUILD_DIR')).resolve()
framework = Path(platform.get_package_dir('framework-arduinoespressif32')).resolve()
esptool = Path(platform.get_package_dir('tool-esptoolpy')).resolve() / 'esptool.py'
metadata = {
    'environment': env['PIOENV'],
    'board': env.subst('$BOARD'),
    'chip': board.get('build.mcu'),
    'flash_size': board.get('upload.flash_size'),
    'flash_mode': board.get('build.flash_mode'),
    'flash_frequency': board.get('build.f_flash'),
    'app_offset': env.subst('$ESP32_APP_OFFSET'),
    'app_path': str(build_dir / (env.subst('${PROGNAME}') + '.bin')),
    'framework': str(framework),
    'framework_version': json.loads((framework / 'package.json').read_text())['version'],
    'esptool': str(esptool),
    'esptool_package_version': json.loads((esptool.parent / 'package.json').read_text())['version'],
    'extra_images': [[env.subst(str(offset)), str(Path(env.subst(str(path))).resolve())]
                     for offset, path in env.get('FLASH_EXTRA_IMAGES', [])],
}
build_dir.mkdir(parents=True, exist_ok=True)
(build_dir / 'v30-pio-flash-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
