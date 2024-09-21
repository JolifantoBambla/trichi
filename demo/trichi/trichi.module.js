/* trichi@0.1.0, license MIT */
const threads=()=>(async e=>{try{return "undefined"!=typeof MessageChannel&&(new MessageChannel).port1.postMessage(new SharedArrayBuffer(1)),WebAssembly.validate(e)}catch(e){return !1}})(new Uint8Array([0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,5,4,1,3,1,1,10,11,1,9,0,65,0,254,16,2,0,26,11]));

/**
 * Initializes a {@link Trichi} module.
 *
 * @param maxThreadPoolSize sets the maximum number of threads in the module's thread pool. In environments that do not support multithreading, this is ignored.
 */
async function initTrichiJs(maxThreadPoolSize = navigator.hardwareConcurrency) {
    const moduleName = (await threads()) ? './wasm/trichi-wasm-threads.js' : './wasm/trichi-wasm.js';
    const module = await import(moduleName);
    // @ts-expect-error we don't care if the module's type is unknown here
    return new module.default({ maxThreads: Math.min(maxThreadPoolSize, navigator.hardwareConcurrency) });
}

export { initTrichiJs as default };
//# sourceMappingURL=trichi.module.js.map
