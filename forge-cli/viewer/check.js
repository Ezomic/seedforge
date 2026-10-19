'use strict';
// Smoke test, no dependencies: node forge-cli/viewer/check.js [output-folder]
// Reads a forge-cli output folder (default: the committed fixture), decodes every PNG with zlib and
// checks that the viewer's JS hash equals the C++ hash recorded in manifest.json.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const assert = require('assert');
const core = require('./core.js');

function decodePng(buf) {
    assert.deepStrictEqual([...buf.subarray(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10], 'png signature');
    let pos = 8;
    let ihdr = null;
    const idat = [];
    while (pos < buf.length) {
        const len = buf.readUInt32BE(pos);
        const type = buf.toString('latin1', pos + 4, pos + 8);
        const data = buf.subarray(pos + 8, pos + 8 + len);
        if (type === 'IHDR') ihdr = { w: data.readUInt32BE(0), h: data.readUInt32BE(4), depth: data[8], color: data[9] };
        if (type === 'IDAT') idat.push(data);
        pos += 12 + len;
    }
    assert(ihdr && ihdr.depth === 8, 'ihdr');
    const channels = ihdr.color === 0 ? 1 : ihdr.color === 2 ? 3 : 0;
    assert(channels, 'color type ' + ihdr.color);
    const raw = zlib.inflateSync(Buffer.concat(idat));
    const stride = ihdr.w * channels;
    const pixels = Buffer.alloc(stride * ihdr.h);
    for (let y = 0; y < ihdr.h; y++) {
        assert.strictEqual(raw[y * (stride + 1)], 0, 'filter type 0 expected');
        raw.copy(pixels, y * stride, y * (stride + 1) + 1, (y + 1) * (stride + 1));
    }
    return { w: ihdr.w, h: ihdr.h, channels, pixels };
}

function checkFolder(dir) {
    const m = core.parseManifest(fs.readFileSync(path.join(dir, 'manifest.json'), 'utf8'));
    let n = 0;
    for (const run of m.runs) {
        for (const l of run.layers) {
            const png = decodePng(fs.readFileSync(path.join(dir, l.file)));
            assert.strictEqual(png.w, l.width, l.file + ' width');
            assert.strictEqual(png.h, l.height, l.file + ' height');
            assert.strictEqual(png.channels, l.channels, l.file + ' channels');
            assert.strictEqual(core.layerHash(l.name, png.w, png.h, png.channels, png.pixels), l.hash, l.file + ' hash');
            n++;
        }
    }
    assert(n > 0, 'no layers checked');
    return n;
}

function checkParser() {
    const ok = '{"size":2,"phases":[],"runs":[{"seed":1,"layers":[{"name":"a","phase":"p","file":"seed_1/a.png","width":2,"height":2,"channels":1,"hash":"00"}]}]}';
    assert.strictEqual(core.parseManifest(ok).runs[0].layers[0].name, 'a');
    const bad = [
        'not json', '[]', '{}', '{"runs":[{"seed":"x"}]}',
        ok.replace('seed_1/a.png', '../a.png'), ok.replace('seed_1/a.png', '/etc/a.png'), ok.replace('seed_1/a.png', 'a\\\\b.png'),
        ok.replace('"width":2', '"width":0'), ok.replace('"width":2', '"width":99999'), ok.replace('"channels":1', '"channels":2'),
        ok.replace('"hash":"00"', '"hash":5'),
    ];
    for (const b of bad) assert.throws(() => core.parseManifest(b), b);
    assert.strictEqual(core.sampleText(new Uint8Array([1, 2, 3, 4]), 2, 2, 1, 1, 1), '4');
    assert.strictEqual(core.sampleText(new Uint8Array(12), 2, 2, 3, 0, 0), '0, 0, 0');
    assert.strictEqual(core.sampleText(new Uint8Array(4), 2, 2, 1, 2, 0), '');
    assert.strictEqual(core.sampleText(new Uint8Array(4), 2, 2, 1, -1, 0), '');
    const a = core.layerHash('x', 1, 1, 1, new Uint8Array([0]));
    assert.notStrictEqual(a, core.layerHash('x', 1, 1, 1, new Uint8Array([1])), 'hash must depend on pixels');
}

function checkOverlay() {
    const ok = '{"size":8,"sites":[{"id":"settlement_0000","kind":"settlement","x":1,"y":2,"items":[],"cast":[{"id":"smith#0","archetype":"smith"}]},' +
        '{"id":"poi_0000","kind":"dungeon","x":7,"y":0,"items":["sword"],"cast":[],"dungeon":{"archetype":"crypt","seed":"18446744073709551615"}}]}';
    const parsed = core.parseOverlay(ok);
    assert.strictEqual(parsed.sites.length, 2);
    assert.strictEqual(parsed.sites[1].dungeon.seed, '18446744073709551615');
    assert.strictEqual(parsed.sites[0].cast[0].id, 'smith#0');
    const bad = [
        'nope', '[]', '{}', '{"size":8}', ok.replace('"size":8', '"size":0'), ok.replace('"kind":"settlement"', '"kind":"castle"'),
        ok.replace('"x":7', '"x":8'), ok.replace('"y":2', '"y":-1'), ok.replace('"x":1', '"x":1.5'), ok.replace('"sword"', '5'),
        ok.replace('"archetype":"smith"', '"archetype":3'), ok.replace('"seed":"18446744073709551615"', '"seed":5'),
    ];
    for (const b of bad) assert.throws(() => core.parseOverlay(b), b);
    const big = JSON.stringify({ size: 8, sites: Array.from({ length: 9000 }, (_, i) => ({ id: 's' + i, kind: 'ruin', x: 0, y: 0, items: [], cast: [] })) });
    assert.throws(() => core.parseOverlay(big), 'too many sites');
    assert.strictEqual(core.nearestSite(parsed.sites, 1.5, 2.5, 0.5).id, 'settlement_0000');
    assert.strictEqual(core.nearestSite(parsed.sites, 4, 4, 1), null);
    assert.strictEqual(core.nearestSite([], 0, 0, 5), null);
    const m = '{"runs":[{"seed":1,"overlay":"seed_1/overlay.json","layers":[]}]}';
    assert.strictEqual(core.parseManifest(m).runs[0].overlay, 'seed_1/overlay.json');
    assert.throws(() => core.parseManifest(m.replace('seed_1/overlay.json', '../overlay.json')));
    assert.strictEqual(core.parseManifest('{"runs":[{"seed":1,"layers":[]}]}').runs[0].overlay, '');
}

// The overlay must agree with the folder's own layers: every site lies on the grid and a "pois" layer pixel.
function checkFolderOverlays(dir) {
    const m = core.parseManifest(fs.readFileSync(path.join(dir, 'manifest.json'), 'utf8'));
    let n = 0;
    for (const run of m.runs) {
        if (!run.overlay) continue;
        const overlay = core.parseOverlay(fs.readFileSync(path.join(dir, run.overlay), 'utf8'));
        const pois = run.layers.find((l) => l.name === 'pois');
        assert(pois && pois.channels === 1, 'overlay without a one-channel pois layer');
        assert.strictEqual(overlay.size, pois.width, 'overlay size');
        const png = decodePng(fs.readFileSync(path.join(dir, pois.file)));
        for (const s of overlay.sites.filter((s) => s.id.startsWith('poi_'))) assert(png.pixels[s.y * png.w + s.x] > 0, s.id + ' not on a pois pixel');
        n += overlay.sites.length;
    }
    return n;
}

checkParser();
checkOverlay();
const dir = process.argv[2] || path.join(__dirname, 'fixture');
const sites = checkFolderOverlays(dir);
if (!process.argv[2]) assert(sites > 0, 'the committed fixture has no overlay');
console.log('viewer check ok: parser, ' + checkFolder(dir) + ' layer hashes match, ' + sites + ' overlay sites on the grid in ' + dir);
