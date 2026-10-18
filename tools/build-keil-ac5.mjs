// Headless AC5 build. Reads the shared manifest; preserves existing outputs.
// Usage: node tools/build-keil-ac5.mjs [--arm-dir PATH] [--release]
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const armIndex = args.indexOf('--arm-dir');
if (armIndex >= 0 && !args[armIndex + 1]) throw new Error('--arm-dir requires a path');
const armDir = path.resolve(armIndex >= 0 ? args[armIndex + 1] :
    (process.env.FW_ARMCC_BIN || 'D:/keil5/stm32/ARM/ARMCC/bin'));
const release = args.includes('--release');
const outputRelative = release ? 'build/keil-ac5-release' : 'build/keil-ac5';
const output = path.join(root, outputRelative);
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'projects/stm32f407/manifest.json'), 'utf8'));
const sources = [...Object.values(manifest.coreLibraries).flat(), ...manifest.firmwareSources,
    ...manifest.vendorSources, manifest.ac5Port];
const includes = [...manifest.includes, path.posix.dirname(manifest.ac5Port)];
const binaries = Object.fromEntries(['armcc', 'armasm', 'armlink', 'fromelf'].map(name => {
    const exe = path.join(armDir, name + '.exe');
    if (!fs.existsSync(exe)) throw new Error('Missing AC5 tool: ' + exe);
    return [name, exe];
}));
for (const file of [...sources, manifest.keilStartup, manifest.keilScatter]) {
    if (!fs.existsSync(path.join(root, file))) throw new Error('Missing input: ' + file + '; configure f407-debug first.');
}
if (new Set(sources).size !== sources.length) throw new Error('Duplicate source in manifest');
// A fresh dependency tree needs the tracked startup patch, applied by CMake.
const startup = fs.readFileSync(path.join(root, manifest.keilStartup), 'utf8');
if (!/Stack_Size\s+EQU\s+0x00001000/.test(startup)) {
    throw new Error('Expected 4 KB startup stack patch; run cmake --preset f407-debug first.');
}
fs.mkdirSync(output, {recursive: true});
let log = '';
function run(name, parameters, cwd = root) {
    const result = spawnSync(binaries[name], parameters, {cwd, encoding:'utf8', windowsHide:true});
    const text = (result.stdout || '') + (result.stderr || '');
    log += text;
    if (text.trim()) process.stdout.write(text);
    if (result.error || result.status !== 0) {
        fs.writeFileSync(path.join(output, 'build.log'), log, 'utf8');
        throw new Error(name + ' failed: ' + (result.error || result.status));
    }
}
run('armcc', ['--vsn']);
const flags = ['-c', '--cpu', 'Cortex-M4.fp', '--c99', '-g', release ? '-O2' : '-O0',
    '--apcs=interwork', '--split_sections',
    ...manifest.defines.map(value => '-D' + value), ...includes.map(value => '-I' + value)];
const objects = [];
for (let i = 0; i < sources.length; ++i) {
    const object = String(i).padStart(2, '0') + '_' + path.basename(sources[i], '.c') + '.o';
    console.log('[' + (i + 1) + '/' + sources.length + '] ' + sources[i]);
    run('armcc', [...flags, '-o', path.posix.join(outputRelative, object), sources[i]]);
    objects.push(object);
}
run('armasm', ['--cpu', 'Cortex-M4.fp', '--apcs=interwork', '-g', '-o',
    path.posix.join(outputRelative, 'startup_stm32f407xx.o'), manifest.keilStartup]);
objects.push('startup_stm32f407xx.o');
// Source, object and output paths passed to legacy tools stay relative, including
// scatter file. This avoids corruption of Chinese absolute path arguments.
run('armlink', ['--cpu', 'Cortex-M4.fp', '--scatter', path.relative(output, path.join(root, manifest.keilScatter)),
    '--list', 'fw_f407.map', '--map', '--info', 'sizes,totals', '--entry', 'Reset_Handler',
    '--strict', '--libpath', path.resolve(armDir, '../lib'), '-o', 'fw_f407.axf', ...objects], output);
run('fromelf', ['--i32combined', '--output', 'fw_f407.hex', 'fw_f407.axf'], output);
run('fromelf', ['--bin', '--output', 'fw_f407.bin', 'fw_f407.axf'], output);
for (const file of ['fw_f407.axf', 'fw_f407.hex', 'fw_f407.bin', 'fw_f407.map']) {
    const stat = fs.statSync(path.join(output, file));
    if (stat.size === 0) throw new Error('Empty output: ' + file);
    console.log(file + ': ' + stat.size + ' bytes');
}
fs.writeFileSync(path.join(output, 'build.log'), log, 'utf8');
console.log('AC5 build verified: ' + outputRelative);
