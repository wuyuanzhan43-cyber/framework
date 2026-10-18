// Checks that every relative Markdown link points at a file that exists.
// Wired as the CTest test `docs_links` and run by CI. Links are resolved
// relative to the Markdown file that contains them, matching GitHub rendering.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const skipDirs = new Set(['.git', '.serena', 'build', 'third_party', 'Objects', 'Listings']);
const files = [];

function collect(directory) {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
        if (entry.isDirectory()) {
            if (!skipDirs.has(entry.name)) collect(path.join(directory, entry.name));
        } else if (entry.name.toLowerCase().endsWith('.md')) {
            files.push(path.join(directory, entry.name));
        }
    }
}

collect(root);

const problems = [];
const linkPattern = /\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g;

for (const file of files) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(linkPattern)) {
        let target = match[1];
        if (/^(?:https?:|mailto:|#)/i.test(target)) continue;
        target = target.split('#')[0];
        if (target === '') continue;
        try {
            target = decodeURIComponent(target);
        } catch {
            // Keep the raw text; the existence check will report it.
        }
        const resolved = path.resolve(path.dirname(file), target);
        if (!fs.existsSync(resolved)) {
            problems.push(`${path.relative(root, file)}: missing ${match[1]}`);
        }
    }
}

if (problems.length > 0) {
    console.error('FAIL Markdown links:');
    for (const problem of problems) console.error('  - ' + problem);
    process.exit(1);
}

console.log(`PASS Markdown links: ${files.length} files checked`);
