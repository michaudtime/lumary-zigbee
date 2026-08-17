// Stands in for the real colour converter the definition delegates to. Returns
// a state plus an unrelated field, so the test can prove the wrapper merges
// rather than replaces.
export const light_color_colortemp = {
    key: ['color', 'color_temp', 'color_temp_percent'],
    convertSet: async (entity, key, value) => ({state: {[key]: value}, readAfterWriteTime: 100}),
    convertGet: async (entity, key) => { light_color_colortemp.lastGet = key; },
};
