(function (root) {
    'use strict';

    const FNV_OFFSET = 14695981039346656037n;
    const FNV_PRIME = 1099511628211n;
    const MASK = 0xffffffffffffffffn;
    const MAX_LAYERS = 4096;
    const MAX_DIM = 16384;

    // Mirrors forgecli::layerHash: FNV-1a over name length (u32 LE), name bytes, width, height,
    // channels (u32 LE each) and the raw pixel bytes.
    function layerHash(name, width, height, channels, pixels) {
        let h = FNV_OFFSET;
        const mix = (b) => {
            h = ((h ^ BigInt(b)) * FNV_PRIME) & MASK;
        };
        const mix32 = (v) => {
            for (let shift = 0; shift < 32; shift += 8) mix((v >>> shift) & 0xff);
        };
        const nameBytes = new TextEncoder().encode(name);
        mix32(nameBytes.length);
        for (const b of nameBytes) mix(b);
        mix32(width);
        mix32(height);
        mix32(channels);
        for (let i = 0; i < pixels.length; i++) mix(pixels[i]);
        return h.toString(16).padStart(16, '0');
    }

    function isDim(v) {
        return Number.isInteger(v) && v > 0 && v <= MAX_DIM;
    }

    // Validates manifest.json text and returns { size, phases, runs } or throws an Error.
    // Layer files are checked to stay inside the output folder.
    function parseManifest(text) {
        let m;
        try {
            m = JSON.parse(text);
        } catch (e) {
            throw new Error('manifest.json is not valid JSON');
        }
        if (!m || typeof m !== 'object' || !Array.isArray(m.runs)) throw new Error('manifest.json has no runs array');
        const runs = [];
        for (const r of m.runs) {
            if (!r || typeof r !== 'object' || !Number.isInteger(r.seed)) throw new Error('manifest run without an integer seed');
            const layers = [];
            const rawLayers = Array.isArray(r.layers) ? r.layers : [];
            if (rawLayers.length > MAX_LAYERS) throw new Error('manifest run has too many layers');
            for (const l of rawLayers) {
                if (!l || typeof l.name !== 'string' || typeof l.hash !== 'string') throw new Error('manifest layer without name or hash');
                if (!isDim(l.width) || !isDim(l.height)) throw new Error('manifest layer ' + l.name + ' has bad dimensions');
                if (l.channels !== 1 && l.channels !== 3) throw new Error('manifest layer ' + l.name + ' has bad channels');
                const file = typeof l.file === 'string' ? l.file : '';
                if (file.startsWith('/') || file.includes('\\') || file.split('/').includes('..')) throw new Error('manifest layer ' + l.name + ' has an unsafe file path');
                layers.push({ name: l.name, phase: String(l.phase || ''), file, width: l.width, height: l.height, channels: l.channels, hash: l.hash });
            }
            runs.push({ seed: r.seed, error: typeof r.error === 'string' ? r.error : '', layers });
        }
        return { size: Number.isInteger(m.size) ? m.size : 0, phases: Array.isArray(m.phases) ? m.phases.map(String) : [], runs };
    }

    // Raw sample text for the pixel at (x, y); pixels is tightly packed with `channels` bytes per pixel.
    function sampleText(pixels, width, height, channels, x, y) {
        if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || y < 0 || x >= width || y >= height) return '';
        const i = (y * width + x) * channels;
        if (channels === 1) return String(pixels[i]);
        return pixels[i] + ', ' + pixels[i + 1] + ', ' + pixels[i + 2];
    }

    const api = { layerHash, parseManifest, sampleText };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    else root.ViewerCore = api;
})(typeof self !== 'undefined' ? self : this);
