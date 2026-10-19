(function (root) {
    'use strict';

    const FNV_OFFSET = 14695981039346656037n;
    const FNV_PRIME = 1099511628211n;
    const MASK = 0xffffffffffffffffn;
    const MAX_LAYERS = 4096;
    const MAX_DIM = 16384;
    const MAX_SITES = 8192;
    const MAX_LIST = 4096;
    const KINDS = ['settlement', 'dungeon', 'ruin', 'landmark'];

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

    function safeFile(file) {
        return !(file.startsWith('/') || file.includes('\\') || file.split('/').includes('..'));
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
                if (!safeFile(file)) throw new Error('manifest layer ' + l.name + ' has an unsafe file path');
                layers.push({ name: l.name, phase: String(l.phase || ''), file, width: l.width, height: l.height, channels: l.channels, hash: l.hash });
            }
            const overlay = typeof r.overlay === 'string' ? r.overlay : '';
            if (!safeFile(overlay)) throw new Error('manifest run ' + r.seed + ' has an unsafe overlay path');
            runs.push({ seed: r.seed, error: typeof r.error === 'string' ? r.error : '', overlay, layers });
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

    function str(v, max) {
        return typeof v === 'string' && v.length <= max;
    }

    // Validates overlay.json (the content manifest placed on the map) and returns { size, sites } or throws.
    // Sites outside the grid, unknown kinds and oversized lists are rejected rather than clipped.
    function parseOverlay(text) {
        let o;
        try {
            o = JSON.parse(text);
        } catch (e) {
            throw new Error('overlay.json is not valid JSON');
        }
        if (!o || typeof o !== 'object' || !isDim(o.size) || !Array.isArray(o.sites)) throw new Error('overlay.json has no size or sites');
        if (o.sites.length > MAX_SITES) throw new Error('overlay.json has too many sites');
        const sites = [];
        for (const s of o.sites) {
            if (!s || typeof s !== 'object' || !str(s.id, 64) || !KINDS.includes(s.kind)) throw new Error('overlay site without id or known kind');
            if (!Number.isInteger(s.x) || !Number.isInteger(s.y) || s.x < 0 || s.y < 0 || s.x >= o.size || s.y >= o.size) throw new Error('overlay site ' + s.id + ' is outside the grid');
            const items = Array.isArray(s.items) ? s.items : [];
            const cast = Array.isArray(s.cast) ? s.cast : [];
            if (items.length > MAX_LIST || cast.length > MAX_LIST) throw new Error('overlay site ' + s.id + ' has too many entries');
            if (!items.every((i) => str(i, 64))) throw new Error('overlay site ' + s.id + ' has a bad item');
            if (!cast.every((c) => c && str(c.id, 64) && str(c.archetype, 64))) throw new Error('overlay site ' + s.id + ' has a bad cast member');
            let dungeon = null;
            if (s.dungeon !== undefined) {
                if (!s.dungeon || !str(s.dungeon.archetype, 64) || !str(s.dungeon.seed, 20)) throw new Error('overlay site ' + s.id + ' has a bad dungeon');
                dungeon = { archetype: s.dungeon.archetype, seed: s.dungeon.seed };
            }
            sites.push({ id: s.id, kind: s.kind, x: s.x, y: s.y, items: items.slice(), cast: cast.map((c) => ({ id: c.id, archetype: c.archetype })), dungeon });
        }
        return { size: o.size, sites };
    }

    // Site nearest to cell (x, y) within `radius` cells, or null. Ties go to the earlier site.
    function nearestSite(sites, x, y, radius) {
        let best = null;
        let bestD = radius * radius;
        for (const s of sites) {
            const d = (s.x + 0.5 - x) * (s.x + 0.5 - x) + (s.y + 0.5 - y) * (s.y + 0.5 - y);
            if (d <= bestD && (best === null || d < bestD)) {
                best = s;
                bestD = d;
            }
        }
        return best;
    }

    const api = { layerHash, parseManifest, parseOverlay, nearestSite, sampleText, KINDS };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    else root.ViewerCore = api;
})(typeof self !== 'undefined' ? self : this);
