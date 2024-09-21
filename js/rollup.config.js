import terser from '@rollup/plugin-terser';
import {wasm} from "@rollup/plugin-wasm";
import typescript from '@rollup/plugin-typescript';
import commonjs from '@rollup/plugin-commonjs';
import copy from '@rollup-extras/plugin-copy';
import dynamicImportVars from '@rollup/plugin-dynamic-import-vars';
import { nodeResolve } from '@rollup/plugin-node-resolve';
import fs from 'fs';

const pkg = JSON.parse(fs.readFileSync('package.json', {encoding: 'utf8'}));
const banner = `/* trichi@${pkg.version}, license MIT */`;
const major = pkg.version.split('.')[0];
const dist = `dist/${major}.x`;

const plugins = [
    nodeResolve({
        browser: true,
        extensions: [".ts"]
    }),
    typescript({ tsconfig: './tsconfig.json' }),
    commonjs(),
    copy({
        targets: [
            {src: 'src/wasm/*', dest: 'wasm'},
        ],
    }),
    wasm({
        sync: ['src/**/*.wasm'],
    }),
];
const shared = {
    watch: {
        clearScreen: false,
    },
};

export default [
    {
        input: 'src/trichi.ts',
        output: [
            {
                dir: `${dist}/`,
                format: 'esm',
                sourcemap: true,
                freeze: false,
                banner,
            },
        ],
        plugins: [
            ...plugins,
            terser(),
        ],
        ...shared,
    },
];
