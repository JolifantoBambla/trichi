export const webgpuNotSupported = () => {
    const dialogBox = document.createElement('dialog');
    document.body.append(dialogBox);
    const dialogText = document.createElement('pre');
    dialogText.style.whiteSpace = 'pre-wrap';
    dialogBox.append('WebGPU not supported by this browser');
    dialogBox.showModal();
}

export function showStatusMessage(message) {
    const dialogBox = document.createElement('dialog');
    document.body.append(dialogBox);
    const dialogText = document.createElement('pre');
    dialogText.style.whiteSpace = 'pre-wrap';
    dialogBox.append(message);
    dialogBox.showModal();
    return dialogBox;
}

export async function textureFromUrl(device, url, format = 'rgba8unorm') {
    const source = await createImageBitmap(await (await fetch(url)).blob());
    const size = { width: source.width, height: source.height };
    const texture = device.createTexture({
        size,
        format,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT,
    });
    device.queue.copyExternalImageToTexture({ source }, { texture }, size);
    return texture;
}
