import {Pane} from 'https://cdn.jsdelivr.net/npm/tweakpane@4.0.3/dist/tweakpane.min.js';

export function makeUi(numLods) {
    const params = {
        renderSettings: {
            lod: 0,
            updateCullingCamera: true,
            drawDirect: false,
            errorThreshold: 1.0,
        },
    };

    const pane = new Pane({
        title: 'WebGPU LOD Cluster Hierarchy',
        expanded: true,
        container: document.getElementById('ui'),
    });
    pane.addBinding({
        info: `WASD: move horizontally
Arrow Up / Down: move vertically
Space (hold): move faster
Mouse (click on canvas first): look around
Escape: exit pointer lock on canvas`,
    }, 'info', {
        label: null,
        readonly: true,
        multiline: true,
        rows: 5,
    });

    const renderSettingsFolder = pane.addFolder({
        title: 'Render settings',
        expanded: true,
    });
    renderSettingsFolder.addBinding(params.renderSettings, 'lod', {label: 'LOD', min: 0, max: numLods - 1, step: 1});
    renderSettingsFolder.addBinding(params.renderSettings, 'updateCullingCamera', {label: 'Update culling camera'});
    renderSettingsFolder.addBinding(params.renderSettings, 'drawDirect', {label: 'Draw clusters directly'});
    renderSettingsFolder.addBinding(params.renderSettings, 'errorThreshold', {label: 'Error threshold', min: 0.0, max: 5.0, step: 0.001});

    return params;
}
