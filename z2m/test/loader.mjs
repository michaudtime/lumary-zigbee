// Resolves the three zigbee-herdsman-converters imports the converter makes to
// the stubs in ./stubs, so the test runs with nothing installed. Registered as
// a module hook by converter.test.mjs before it imports the converter.
import {fileURLToPath, pathToFileURL} from 'node:url';
import path from 'node:path';

const STUBS = {
    'zigbee-herdsman': 'zigbee-herdsman.mjs',
    'zigbee-herdsman-converters/lib/modernExtend': 'modernExtend.mjs',
    'zigbee-herdsman-converters/lib/exposes': 'exposes.mjs',
    'zigbee-herdsman-converters/converters/toZigbee': 'toZigbee.mjs',
};

const here = path.dirname(fileURLToPath(import.meta.url));

export function resolve(specifier, context, next) {
    const stub = STUBS[specifier];
    if (!stub) return next(specifier, context);
    return {url: pathToFileURL(path.join(here, 'stubs', stub)).href, shortCircuit: true};
}
