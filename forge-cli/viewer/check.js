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

checkParser();
const dir = process.argv[2] || path.join(__dirname, 'fixture');
console.log('viewer check ok: parser, ' + checkFolder(dir) + ' layer hashes match in ' + dir);
