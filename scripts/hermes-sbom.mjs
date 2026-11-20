#!/usr/bin/env node
// SPDX 2.3 SBOM for the native Hermes client packages (Consiglio-Hermes-*).
//
//   node scripts/hermes-sbom.mjs --dir release-artifacts --inputs sbom-inputs \
//        --out release-artifacts/Consiglio-Hermes-<version>.spdx.json
//
// --dir     folder holding the Consiglio-Hermes-<version>-* packages
// --inputs  folder of per-platform JSON notes written by packaging/package.sh
//           ({ platform, arch, qtVersion, qtSource, files: [...] })
//
// The packages bundle (or, for the .deb, depend on) Qt 5 under the LGPL; each
// package file is listed with its SHA-256 so the SBOM pins exactly what shipped.
import { createHash, randomUUID } from 'node:crypto';
import { readFileSync, readdirSync, writeFileSync, existsSync } from 'node:fs';
import { join, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((pairs, value, index, all) => {
    if (value.startsWith('--')) pairs.push([value.slice(2), all[index + 1]]);
    return pairs;
  }, []),
);
const root = fileURLToPath(new URL('..', import.meta.url));
const cmake = readFileSync(join(root, 'src-hermes', 'CMakeLists.txt'), 'utf8');
const version = cmake.match(/project\(ConsiglioHermes VERSION ([0-9.]+)/)?.[1];
if (!version) throw new Error('ConsiglioHermes version not found in src-hermes/CMakeLists.txt');
const dir = args.dir ?? 'release-artifacts';
const out = args.out ?? join(dir, `Consiglio-Hermes-${version}.spdx.json`);

const packages = readdirSync(dir)
  .filter((name) => name.startsWith(`Consiglio-Hermes-${version}-`))
  .sort();
if (packages.length === 0) throw new Error(`no Consiglio-Hermes-${version}-* packages in ${dir}`);

const notes = [];
if (args.inputs && existsSync(args.inputs)) {
  for (const name of readdirSync(args.inputs).filter((n) => n.endsWith('.json')).sort()) {
    notes.push(JSON.parse(readFileSync(join(args.inputs, name), 'utf8')));
  }
}

const sha256 = (file) => createHash('sha256').update(readFileSync(file)).digest('hex');
const spdxId = (text) => `SPDXRef-${text.replace(/[^A-Za-z0-9.-]/g, '-')}`;
const repo = 'https://github.com/rickenator/Consiglio';

const client = {
  SPDXID: 'SPDXRef-Package-ConsiglioHermes',
  name: 'ConsiglioHermes',
  versionInfo: version,
  supplier: 'Organization: Consiglio contributors',
  downloadLocation: `git+${repo}.git@v${version}#src-hermes`,
  homepage: repo,
  licenseConcluded: 'Apache-2.0',
  licenseDeclared: 'Apache-2.0',
  copyrightText: 'NOASSERTION',
  filesAnalyzed: false,
  primaryPackagePurpose: 'APPLICATION',
  externalRefs: [
    { referenceCategory: 'PACKAGE-MANAGER', referenceType: 'purl', referenceLocator: `pkg:github/rickenator/Consiglio@v${version}#src-hermes` },
  ],
};

const qtVersions = new Map();
for (const note of notes) {
  const key = note.qtVersion || 'NOASSERTION';
  if (!qtVersions.has(key)) qtVersions.set(key, []);
  qtVersions.get(key).push(`${note.platform}-${note.arch} (${note.qtSource ?? 'unknown source'})`);
}
if (qtVersions.size === 0) qtVersions.set('NOASSERTION', ['all platforms']);
const qtPackages = [...qtVersions.entries()].map(([qtVersion, where]) => ({
  SPDXID: spdxId(`Package-Qt-${qtVersion}`),
  name: 'Qt',
  versionInfo: qtVersion,
  supplier: 'Organization: The Qt Company',
  downloadLocation: 'https://download.qt.io/',
  homepage: 'https://www.qt.io/',
  licenseConcluded: 'LGPL-3.0-only',
  licenseDeclared: 'LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only',
  copyrightText: 'NOASSERTION',
  filesAnalyzed: false,
  primaryPackagePurpose: 'LIBRARY',
  comment: `Qt modules Core, Gui, Widgets, Svg (+ platform plugins). Used by: ${where.join(', ')}.`,
  externalRefs: [
    { referenceCategory: 'PACKAGE-MANAGER', referenceType: 'purl', referenceLocator: `pkg:generic/qt@${qtVersion}` },
  ],
}));

const files = packages.map((name) => ({
  SPDXID: spdxId(`File-${name}`),
  fileName: `./${basename(name)}`,
  checksums: [{ algorithm: 'SHA256', checksumValue: sha256(join(dir, name)) }],
  licenseConcluded: 'NOASSERTION',
  copyrightText: 'NOASSERTION',
}));

const relationships = [
  { spdxElementId: 'SPDXRef-DOCUMENT', relationshipType: 'DESCRIBES', relatedSpdxElement: client.SPDXID },
  ...qtPackages.map((qt) => ({ spdxElementId: client.SPDXID, relationshipType: 'DEPENDS_ON', relatedSpdxElement: qt.SPDXID })),
  ...files.map((file) => ({ spdxElementId: client.SPDXID, relationshipType: 'GENERATES', relatedSpdxElement: file.SPDXID })),
];

const document = {
  spdxVersion: 'SPDX-2.3',
  dataLicense: 'CC0-1.0',
  SPDXID: 'SPDXRef-DOCUMENT',
  name: `ConsiglioHermes-${version}`,
  documentNamespace: `${repo}/releases/v${version}/hermes/${randomUUID()}`,
  creationInfo: {
    created: new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
    creators: ['Tool: consiglio-hermes-sbom'],
  },
  packages: [client, ...qtPackages],
  files,
  relationships,
};

writeFileSync(out, `${JSON.stringify(document, null, 2)}\n`);
console.log(`wrote ${out} (${packages.length} packages)`);
