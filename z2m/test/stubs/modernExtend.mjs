// Records what the converter asked light() and deviceAddCustomCluster() for,
// so the test can assert on arguments it otherwise has no way to see.
export const calls = [];

export const light = (args) => {
    calls.push({fn: 'light', args});
    return {isModernExtend: true, kind: 'light', args};
};

export const deviceAddCustomCluster = (name, definition) => {
    calls.push({fn: 'deviceAddCustomCluster', name, definition});
    return {isModernExtend: true, kind: 'cluster', name, definition};
};

export const identify = (args) => {
    calls.push({fn: 'identify', args});
    return {isModernExtend: true, kind: 'identify', args};
};
