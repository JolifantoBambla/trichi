import {Pane} from 'https://cdn.jsdelivr.net/npm/tweakpane@4.0.3/dist/tweakpane.min.js';

export function makeUi() {
    const params = {
        renderSettings: {
            updateCullingCamera: true,
            errorThreshold: 0.009,
            renderMode: 'clusterId',
        },
    };

    const pane = new Pane({
        title: 'WebGPU LOD Cluster Hierarchy',
        expanded: true,
        container: document.getElementById('ui'),
    });
    pane.addBinding({
        info: `This demo renders a 4.3 million triangle model
(courtesy of Raphael Gerlach) processed by the
Tri Chi (work in progress) to generate a
triangle cluster hierarchy. LODs are chosen
per surface patch based on their size on
screen in a compute pass. The chosen clusters
are frustum culled and then rendered via a
single indirect draw call.

Usage:
WASD: move horizontally
Arrow Up / Down: move vertically
Space (hold): move faster
Mouse (click on canvas first): look around
Escape: exit pointer lock on canvas

Drag & Drop 3d models onto the canvas to try a
WebAssembly version of the library in the
browser. Make sure the model is rather small
contiguous as faceted models are currently not
supported.
Also, processing a model in the browser is
much slower than the native version. E.g.,
processing the 4.3 million triangle model
takes 75 seconds on native vs. 15 minutes in
the browser.
Keep that in mind when choosing a model to try
it out - models like Lucy, Happy Buddha, etc.
are good choices.

Have fun :)
`,
    }, 'info', {
        label: null,
        readonly: true,
        multiline: true,
        rows: 32,
    });

    const renderSettingsFolder = pane.addFolder({
        title: 'Render settings',
        expanded: true,
    });
    renderSettingsFolder.addBinding(params.renderSettings, 'updateCullingCamera', {label: 'Update culling camera'});
    renderSettingsFolder.addBinding(params.renderSettings, 'errorThreshold', {label: 'Error threshold', min: 0.0, max: 1.0, step: 0.001});
    renderSettingsFolder.addBinding(params.renderSettings, 'renderMode', {label: 'Render mode', options: {'Cluster Ids': 'clusterId', 'Triangle Ids': 'triangleId', 'Normals': 'smooth'}});

    return params;
}
