import terser from '@rollup/plugin-terser';
import {wasm} from "@rollup/plugin-wasm";
import typescript from '@rollup/plugin-typescript';
import copy from '@rollup-extras/plugin-copy';
import {nodeResolve} from '@rollup/plugin-node-resolve';
import fs from 'fs';

const pkg = JSON.parse(fs.readFileSync('package.json', {encoding: 'utf8'}));
const banner = `/* trichi@${pkg.version}, license MIT */`;
const major = pkg.version.split('.')[0];
const dist = `dist/${major}.x`;

const plugins = [
    nodeResolve(),
    typescript({ tsconfig: './tsconfig.json' }),
    copy({targets: [{src: 'src/wasm/*', dest: 'wasm'}]}),
    wasm({sync: ['src/**/*.wasm']}),
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
                file: `${dist}/trichi.module.js`,
                format: 'esm',
                sourcemap: true,
                freeze: false,
                banner,
            },
        ],
        plugins,
        ...shared,
    },
    {
        input: 'src/trichi.ts',
        output: [
            {
                file: `${dist}/trichi.module.min.js`,
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
    {
        input: 'src/trichi.ts',
        output: [
            {
                name: 'trichi',
                file: `${dist}/trichi.js`,
                format: 'umd',
                sourcemap: true,
                freeze: false,
                banner,
            },
        ],
        plugins,
        ...shared,
    },
    {
        input: 'src/trichi.ts',
        output: [
            {
                name: 'trichi',
                file: `${dist}/trichi.min.js`,
                format: 'umd',
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
