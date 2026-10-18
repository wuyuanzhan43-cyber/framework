// Cross-checks the hand-maintained build entry points against
// projects/stm32f407/manifest.json.
//
// Why this exists (CODE_REVIEW.md M-1): the same source set is written down in the
// CMake target definitions, in manifest.json, and in the uVision .uvprojx. CMake now
// derives its list from the manifest, but the .uvprojx is still a manual copy that
// only a human ever compares. Nothing else in the build would notice a divergence --
// the compiler simply would not see the file.
//
// Usage: node tools/check-sources.mjs      (also wired as the CTest test
//                                           `source_manifest_sync`)
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const manifestPath = 'projects/stm32f407/manifest.json';
const manifest = JSON.parse(fs.readFileSync(path.join(root, manifestPath), 'utf8'));

const normalize = value => path.posix.normalize(value.replace(/\\/g, '/'));

// Everything the ARM Keil target is expected to compile or assemble.
const keilExpected = [
    ...Object.values(manifest.coreLibraries).flat(),
    ...manifest.firmwareSources,
    ...manifest.vendorSources,
    manifest.ac5Port,
    manifest.keilStartup,
];

const problems = [];

// Third-party trees are fetched by CMake and intentionally absent from a clean
// checkout. Host CI runs this test before fetching them, so existence checks
// cover project-owned files only; the vendor list is still compared against the
// uVision project below.
const projectOwned = entry => !entry.startsWith('third_party/');
let vendorSkipped = 0;

// 1. Every project-owned manifest entry must exist on disk. A typo here
//    currently surfaces only when someone happens to run the matching build.
for (const entry of keilExpected) {
    if (!projectOwned(entry)) {
        ++vendorSkipped;
        continue;
    }
    if (!fs.existsSync(path.join(root, entry))) {
        problems.push(`manifest entry does not exist: ${entry}`);
    }
}

// Files listed in the manifest drive the GCC chain too; check those as well.
for (const entry of [manifest.gccPort, manifest.gccStartup, manifest.gccRuntime]) {
    if (!projectOwned(entry)) {
        ++vendorSkipped;
        continue;
    }
    if (!fs.existsSync(path.join(root, entry))) {
        problems.push(`manifest entry does not exist: ${entry}`);
    }
}

// 2. The uVision project must list exactly the Keil source set. Its <FilePath>
//    entries are relative to the directory that holds the .uvprojx.
const projectDir = path.posix.dirname(manifest.keilProject);
const uvprojx = fs.readFileSync(path.join(root, manifest.keilProject), 'utf8');
const listedFiles = new Set(
    [...uvprojx.matchAll(/<FilePath>([^<]+)<\/FilePath>/g)]
        .map(match => normalize(path.posix.join(projectDir, match[1].replace(/\\/g, '/'))))
);
const expectedFiles = new Set(keilExpected.map(normalize));

const missingFromProject = [...expectedFiles].filter(file => !listedFiles.has(file)).sort();
const notInManifest = [...listedFiles].filter(file => !expectedFiles.has(file)).sort();

if (missingFromProject.length > 0) {
    problems.push(`in ${manifestPath} but not in ${manifest.keilProject}:\n    `
        + missingFromProject.join('\n    '));
}
if (notInManifest.length > 0) {
    problems.push(`in ${manifest.keilProject} but not in ${manifestPath}:\n    `
        + notInManifest.join('\n    '));
}

if (problems.length > 0) {
    console.error('FAIL source manifest consistency:');
    for (const problem of problems) console.error('  - ' + problem);
    process.exit(1);
}

console.log(`PASS source manifest: ${listedFiles.size} files agree between ${manifestPath}`
    + ` and ${manifest.keilProject}`
    + (vendorSkipped > 0 ? ` (${vendorSkipped} vendor paths not fetched; existence unchecked)` : ''));
