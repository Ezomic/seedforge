(function () {
    'use strict';
    const core = window.ViewerCore;
    const $ = (id) => document.getElementById(id);

    const state = { files: new Map(), manifest: null, run: null, layers: [], selected: -1, token: 0 };

    function showError(msg) {
        $('error').textContent = msg;
    }

    function setHidden(ids, hidden) {
        for (const id of ids) $(id).hidden = hidden;
    }

    async function onFolder(ev) {
        showError('');
        state.files = new Map();
        for (const f of ev.target.files) {
            const rel = f.webkitRelativePath.split('/').slice(1).join('/');
            state.files.set(rel, f);
        }
        await load();
    }

    async function load() {
        const mf = state.files.get('manifest.json');
        if (!mf) return showError('No manifest.json at the top of that folder.');
        try {
            state.manifest = core.parseManifest(await mf.text());
        } catch (e) {
            return showError(e.message);
        }
        const select = $('seed');
        select.textContent = '';
        for (const r of state.manifest.runs) {
            const o = document.createElement('option');
            o.value = String(r.seed);
            o.textContent = 'seed ' + r.seed + (r.error ? ' (failed)' : '');
            select.appendChild(o);
        }
        if (!state.manifest.runs.length) return showError('The manifest has no runs.');
        setHidden(['seedBox'], false);
        await selectRun(0);
    }

    async function decode(file, layer) {
        const bmp = await createImageBitmap(file, { colorSpaceConversion: 'none', premultiplyAlpha: 'none' });
        if (bmp.width !== layer.width || bmp.height !== layer.height) throw new Error('size differs from the manifest');
        const c = document.createElement('canvas');
        c.width = bmp.width;
        c.height = bmp.height;
        const ctx = c.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(bmp, 0, 0);
        const rgba = ctx.getImageData(0, 0, c.width, c.height).data;
        const n = layer.width * layer.height;
        const pixels = new Uint8Array(n * layer.channels);
        for (let i = 0; i < n; i++) {
            for (let k = 0; k < layer.channels; k++) pixels[i * layer.channels + k] = rgba[i * 4 + k];
        }
        return { canvas: c, pixels };
    }

    async function selectRun(index) {
        const token = ++state.token;
        const run = state.manifest.runs[index];
        state.run = run;
        $('runError').textContent = run.error;
        const loaded = [];
        for (const l of run.layers) {
            const entry = { meta: l, canvas: null, pixels: null, on: false, alpha: 1, blend: 'source-over', verified: null, problem: '' };
            const file = state.files.get(l.file);
            if (!file) {
                entry.problem = 'missing file ' + l.file;
            } else {
                try {
                    const d = await decode(file, l);
                    entry.canvas = d.canvas;
                    entry.pixels = d.pixels;
                    entry.verified = core.layerHash(l.name, l.width, l.height, l.channels, d.pixels) === l.hash;
                } catch (e) {
                    entry.problem = 'cannot read ' + l.file + ': ' + e.message;
                }
            }
            loaded.push(entry);
            if (token !== state.token) return;
        }
        state.layers = loaded;
        const first = loaded.findIndex((l) => l.canvas);
        if (first >= 0) loaded[first].on = true;
        state.selected = first;
        $('empty').hidden = true;
        setHidden(['stage', 'layerBox', 'viewBox', 'hashBox'], false);
        buildLayerList();
        resize();
        draw();
        showHash();
    }

    function buildLayerList() {
        const box = $('layers');
        box.textContent = '';
        state.layers.forEach((layer, i) => {
            const div = document.createElement('div');
            div.className = 'layer' + (layer.on ? ' on' : '') + (i === state.selected ? ' selected' : '');
            const row = document.createElement('div');
            row.className = 'row';
            const cb = document.createElement('input');
            cb.type = 'checkbox';
            cb.checked = layer.on;
            cb.disabled = !layer.canvas;
            cb.setAttribute('aria-label', 'show ' + layer.meta.name);
            cb.addEventListener('change', () => {
                layer.on = cb.checked;
                state.selected = i;
                buildLayerList();
                draw();
                showHash();
            });
            const name = document.createElement('span');
            name.className = 'name';
            name.textContent = layer.meta.name;
            name.style.cursor = 'pointer';
            name.addEventListener('click', () => {
                state.selected = i;
                buildLayerList();
                showHash();
            });
            const phase = document.createElement('span');
            phase.className = 'phase';
            phase.textContent = layer.meta.phase;
            row.append(cb, name, phase);
            div.appendChild(row);
            if (layer.canvas) {
                const sub = document.createElement('div');
                sub.className = 'row sub';
                const alpha = document.createElement('input');
                alpha.type = 'range';
                alpha.min = '0';
                alpha.max = '100';
                alpha.value = String(Math.round(layer.alpha * 100));
                alpha.setAttribute('aria-label', 'opacity of ' + layer.meta.name);
                alpha.addEventListener('input', () => {
                    layer.alpha = Number(alpha.value) / 100;
                    draw();
                });
                const blend = document.createElement('select');
                blend.setAttribute('aria-label', 'blend of ' + layer.meta.name);
                for (const [value, label] of [['source-over', 'normal'], ['screen', 'screen'], ['multiply', 'multiply']]) {
                    const o = document.createElement('option');
                    o.value = value;
                    o.textContent = label;
                    blend.appendChild(o);
                }
                blend.value = layer.blend;
                blend.addEventListener('change', () => {
                    layer.blend = blend.value;
                    draw();
                });
                sub.append(alpha, blend);
                div.appendChild(sub);
            }
            box.appendChild(div);
        });
    }

    function mapSize() {
        const ref = state.layers.find((l) => l.canvas);
        return ref ? [ref.meta.width, ref.meta.height] : [1, 1];
    }

    function resize() {
        const [w, h] = mapSize();
        const canvas = $('map');
        canvas.width = w;
        canvas.height = h;
        const zoom = Number($('zoom').value);
        canvas.style.width = w * zoom + 'px';
        canvas.style.height = h * zoom + 'px';
        $('zoomLabel').textContent = zoom + 'x';
    }

    function draw() {
        const canvas = $('map');
        const ctx = canvas.getContext('2d');
        ctx.globalCompositeOperation = 'source-over';
        ctx.globalAlpha = 1;
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        for (const l of state.layers) {
            if (!l.on || !l.canvas) continue;
            ctx.globalAlpha = l.alpha;
            ctx.globalCompositeOperation = l.blend;
            ctx.drawImage(l.canvas, 0, 0);
        }
        ctx.globalAlpha = 1;
        ctx.globalCompositeOperation = 'source-over';
    }

    function showHash() {
        const l = state.layers[state.selected];
        const box = $('hashInfo');
        box.textContent = '';
        if (!l) return;
        const line = (text, cls) => {
            const d = document.createElement('div');
            d.textContent = text;
            if (cls) d.className = cls;
            box.appendChild(d);
        };
        line(l.meta.name + ' (' + l.meta.width + 'x' + l.meta.height + ', ' + l.meta.channels + ' ch)');
        line('manifest ' + l.meta.hash);
        if (l.problem) line(l.problem, 'bad');
        else if (l.verified) line('pixels match the manifest hash', 'ok');
        else line('pixels do NOT match the manifest hash', 'bad');
    }

    function onMove(ev) {
        const canvas = $('map');
        const rect = canvas.getBoundingClientRect();
        const x = Math.floor(((ev.clientX - rect.left) / rect.width) * canvas.width);
        const y = Math.floor(((ev.clientY - rect.top) / rect.height) * canvas.height);
        const table = $('values');
        table.textContent = '';
        $('cell').textContent = '(' + x + ', ' + y + ')';
        for (const l of state.layers) {
            if (!l.on || !l.pixels) continue;
            const text = core.sampleText(l.pixels, l.meta.width, l.meta.height, l.meta.channels, x, y);
            if (!text) continue;
            const tr = document.createElement('tr');
            const a = document.createElement('td');
            a.textContent = l.meta.name;
            const b = document.createElement('td');
            b.className = 'mono';
            b.textContent = text;
            tr.append(a, b);
            table.appendChild(tr);
        }
    }

    // Optional, only when the page is served over http: viewer/index.html?src=fixture/
    async function loadFromSrc(src) {
        try {
            const base = new URL(src.endsWith('/') ? src : src + '/', location.href);
            if (base.origin !== location.origin) return showError('src must be on the same origin as the viewer.');
            const get = async (rel) => {
                const r = await fetch(new URL(rel, base));
                if (!r.ok) throw new Error(rel + ': HTTP ' + r.status);
                return r.blob();
            };
            const mblob = await get('manifest.json');
            state.files.set('manifest.json', mblob);
            const manifest = core.parseManifest(await mblob.text());
            for (const run of manifest.runs) {
                for (const l of run.layers) if (l.file) state.files.set(l.file, await get(l.file));
            }
            await load();
        } catch (e) {
            showError(e.message);
        }
    }

    $('folder').addEventListener('change', onFolder);
    $('seed').addEventListener('change', () => selectRun($('seed').selectedIndex));
    $('zoom').addEventListener('input', resize);
    $('map').addEventListener('mousemove', onMove);
    $('map').addEventListener('mouseleave', () => {
        $('cell').textContent = '';
        $('values').textContent = '';
    });
    const src = new URLSearchParams(location.search).get('src');
    if (src && location.protocol !== 'file:') loadFromSrc(src);
})();
